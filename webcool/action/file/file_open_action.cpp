#include "stdafx.h"
#include "file_common.h"

namespace {

void set_display_env_for_open()
{
#ifdef _WIN32
	return;
#else
	const char *display = getenv("DISPLAY");
	if (!display || display[0] == '\0') {
		struct stat st{};
		if (stat("/tmp/.X11-unix/X0", &st) == 0) {
			setenv("DISPLAY", ":0", 1);
		} else if (stat("/tmp/.X11-unix", &st) == 0) {
			DIR *d = opendir("/tmp/.X11-unix");
			if (d) {
				dirent *de;
				while ((de = readdir(d)) != nullptr) {
					if (de->d_name[0] == 'X') {
						char buf[16];
						snprintf(buf, sizeof(buf), ":%s",
							de->d_name + 1);
						setenv("DISPLAY", buf, 1);
						break;
					}
				}
				closedir(d);
			}
		}
	}
	const char *dbus = getenv("DBUS_SESSION_BUS_ADDRESS");
	if (!dbus || dbus[0] == '\0') {
		const char *uid_s = getenv("UID");
		const uid_t uid = uid_s ? static_cast<uid_t>(action::safe_atoi(uid_s, 0)) : getuid();
		char addr[128];
		snprintf(addr, sizeof(addr),
			"unix:path=/run/user/%u/bus", static_cast<unsigned>(uid));
		setenv("DBUS_SESSION_BUS_ADDRESS", addr, 1);
	}
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (!runtime || runtime[0] == '\0') {
		char rd[64];
		snprintf(rd, sizeof(rd), "/run/user/%u", static_cast<unsigned>(getuid()));
		setenv("XDG_RUNTIME_DIR", rd, 1);
	}
#endif
}

bool run_open_file_command(const std::string& path,
	const bool choose_app, std::string& err)
{
	err.clear();
#ifdef _WIN32
	(void) choose_app;
	return webcool_shell_open(path, err);
#else
	const pid_t pid = fork();
	if (pid < 0) {
		err = strerror(errno);
		return false;
	}
	if (pid == 0) {
		set_display_env_for_open();
#ifdef __APPLE__
		if (choose_app) {
			execlp("osascript", "osascript",
				"-e", "on run argv",
				"-e", "set targetPath to item 1 of argv",
				"-e", "set chosenApp to choose application with prompt \"选择本地播放器\"",
				"-e", "set appName to name of chosenApp",
				"-e", R"(do shell script "open -a " & quoted form of appName & " " & quoted form of targetPath)",
				"-e", "end run",
				path.c_str(), static_cast<char *>(nullptr));
		}
		execlp("open", "open", path.c_str(), static_cast<char *>(nullptr));
#else
		if (choose_app) {
			char cmd[4096];
			snprintf(cmd, sizeof(cmd),
				"zenity --file-selection "
				"--title='选择本地播放器' "
				"--filename=/usr/bin/ "
				"2>/dev/null");
			FILE *fp = popen(cmd, "r");
			if (!fp) {
				_exit(127);
			}
			char chosen[2048];
			if (!fgets(chosen, sizeof(chosen), fp)) {
				pclose(fp);
				_exit(1);
			}
			pclose(fp);
			size_t len = strlen(chosen);
			while (len > 0 && (chosen[len-1] == '\n'
				|| chosen[len-1] == '\r')) {
				chosen[--len] = '\0';
			}
			if (len == 0) {
				_exit(0);
			}
			execlp(chosen, chosen, path.c_str(), (char*) nullptr);
			_exit(127);
		}
		execlp("xdg-open", "xdg-open", path.c_str(), (char*) nullptr);
		execlp("gio", "gio", "open", path.c_str(), (char*) nullptr);
#endif
		_exit(127);
	}

	int status = 0;
	if (waitpid(pid, &status, 0) < 0) {
		err = strerror(errno);
		return false;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		err = choose_app
			? "failed to choose local player"
			: "failed to open file with local player";
		return false;
	}
	return true;
#endif
}

std::string get_tmp_open_dir()
{
	char buf[256];
	snprintf(buf, sizeof(buf), "/tmp/aicool-open-%u",
		static_cast<unsigned>(getuid()));
	return {buf};
}

} // anonymous namespace

bool action::OpenFileAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	const bool choose_app = req.getParameter("chooser") != nullptr
		&& strcmp(req.getParameter("chooser"), "1") == 0;
	std::string file_path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("file"), file_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_protected_virtual_path(file_path)) {
		json_error(res, 403, "system file is protected", req.isKeepAlive());
		return true;
	}
	// Check folder lock
	bool lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, parent_relative_path(file_path),
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!lock_allowed) {
		json_error(res, 403, "folder is locked", req.isKeepAlive());
		return true;
	}
	// Check file lock
	bool file_lock_allowed = false;
	if (!file_lock_path_allows(upload_dir, remote_file_lock_key(file_path),
		req.getParameter("file_password") ? req.getParameter("file_password") : "",
		file_lock_allowed, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!file_lock_allowed) {
		json_error(res, 403, "file is locked", req.isKeepAlive());
		return true;
	}
	// Resolve the full path on disk
	const std::string fullpath = join_upload_path(upload_dir, file_path);
	struct stat st{};
	if (stat(fullpath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		json_error(res, 404, "file not found", req.isKeepAlive());
		return true;
	}
	// Copy to a temp directory so the player can access it by name
	const std::string tmp_dir = get_tmp_open_dir();
	mkdir(tmp_dir.c_str(), 0755);
	const std::string basename = base_name_from_relative_path(file_path);
	const std::string tmp_path = tmp_dir + "/" + basename;
	// Remove old temp copy if exists
	unlink(tmp_path.c_str());
	// Copy file
	{
		FILE *src = fopen(fullpath.c_str(), "rb");
		if (!src) {
			json_error(res, 500, "cannot read source file", req.isKeepAlive());
			return true;
		}
		FILE *dst = fopen(tmp_path.c_str(), "wb");
		if (!dst) {
			fclose(src);
			json_error(res, 500, "cannot create temp file", req.isKeepAlive());
			return true;
		}
		char buf[65536];
		size_t n;
		while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
			if (fwrite(buf, 1, n, dst) != n) {
				fclose(src);
				fclose(dst);
				unlink(tmp_path.c_str());
				json_error(res, 500, "write error", req.isKeepAlive());
				return true;
			}
		}
		fclose(src);
		fclose(dst);
	}
	if (!run_open_file_command(tmp_path, choose_app, err)) {
		unlink(tmp_path.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("file", file_path.c_str());
	root.add_text("message", choose_app ? "local player chooser opened" : "file opened");
	return sendJson(res, 200, root, req.isKeepAlive());
}

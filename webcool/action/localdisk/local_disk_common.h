#include "../actions.h"
#include "../action_util.h"

#ifdef _WIN32
#include "../../platform_compat.h"
#include <shlobj.h>
#else
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <time.h>

#include <algorithm>
#include <map>
#include "common/webcool_mutex.h"
#include <set>
#include <stdlib.h>
#include <string>
#include <thread>
#include <vector>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

namespace action {

namespace {

struct local_entry_t {
	std::string name;
	std::string path;
	bool directory;
	bool empty_directory;
	long long size;
	long long created_at;
	std::string created_time;
	long long modified_at;
	std::string modified_time;
};

static bool seek_file64(FILE* fp, long long offset)
{
#ifdef _WIN32
	return _fseeki64(fp, offset, SEEK_SET) == 0;
#else
	return fseeko(fp, (off_t) offset, SEEK_SET) == 0;
#endif
}

#ifdef _WIN32
static std::string windows_home_path()
{
	return webcool_windows_home_path();
}

static std::string windows_last_error_message(DWORD code)
{
	wchar_t* buffer = NULL;
	const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER
		| FORMAT_MESSAGE_FROM_SYSTEM
		| FORMAT_MESSAGE_IGNORE_INSERTS;
	const DWORD len = FormatMessageW(flags, NULL, code, 0,
		(LPWSTR) &buffer, 0, NULL);
	std::string message;
	if (len > 0 && buffer != NULL) {
		webcool_wide_to_utf8(buffer, message);
		while (!message.empty()
			&& (message[message.size() - 1] == '\r'
				|| message[message.size() - 1] == '\n'
				|| message[message.size() - 1] == '.'
				|| message[message.size() - 1] == ' '))
		{
			message.erase(message.size() - 1);
		}
	}
	if (buffer != NULL) {
		LocalFree(buffer);
	}
	if (message.empty()) {
		char tmp[64];
		snprintf(tmp, sizeof(tmp), "Windows error %lu", (unsigned long) code);
		message = tmp;
	}
	return message;
}

static std::wstring windows_dir_pattern_from_utf8(const std::string& path)
{
	std::wstring pattern;
	if (!webcool_utf8_path_to_wide(path.c_str(), pattern)) {
		return std::wstring();
	}
	const wchar_t tail = pattern.empty() ? L'\0' : pattern[pattern.size() - 1];
	if (tail != L'/' && tail != L'\\') {
		pattern += L"\\";
	}
	pattern += L"*";
	return pattern;
}

static bool collect_windows_directory_entries(const std::string& path,
	bool show_hidden, std::vector<local_entry_t>& entries, std::string& err);
#endif

struct local_import_file_t {
	std::string source;
	std::string name;
	std::string relative_path;
	long long size;
};

struct local_import_task_t {
	std::string state;
	std::string message;
	std::string error;
	std::vector<std::string> names;
	std::vector<std::string> remote_paths;
	std::vector<long long> sizes;
	std::vector<long long> copied_sizes;
	std::vector<std::string> file_states;
	long long total_bytes;
	long long copied_bytes;
	int total_files;
	int saved_count;
	bool pause_requested;
	bool cancel_requested;
};

static webcool::mutex g_local_import_mutex;
static std::map<std::string, local_import_task_t> g_local_import_tasks;
static unsigned long long g_local_import_seq = 0;

static std::string parent_path(const std::string& path);

#if 0
static void json_error(response_t& res, int status, const char* msg,
	bool keep_alive)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", msg ? msg : "unknown error");
	sendJson(res, status, root, keep_alive);
}
#endif

static void json_locked_dir_error(response_t& res, const char* msg,
	const std::string& path, const std::string& locked_path, bool keep_alive)
{
	const std::string display_path = locked_path.empty() ? path : locked_path;
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", msg ? msg : "directory is locked");
	root.add_text("path", display_path.c_str());
	const std::string parent = parent_path(display_path);
	root.add_text("parent_path", parent.c_str());
	sendJson(res, 403, root, keep_alive);
}

static bool normalize_local_path(const char* input, std::string& out,
	std::string& err)
{
	err.clear();
#ifdef _WIN32
	const bool has_input = input != NULL && *input != '\0';
	std::string text = has_input ? input : windows_home_path();
	if (text == "/") {
		text = "\\";
	}
	const bool absolute = (text.size() >= 3
			&& ((text[0] >= 'A' && text[0] <= 'Z')
				|| (text[0] >= 'a' && text[0] <= 'z'))
			&& text[1] == ':'
			&& (text[2] == '/' || text[2] == '\\'))
		|| (text.size() >= 2
			&& (text[0] == '/' || text[0] == '\\')
			&& (text[1] == '/' || text[1] == '\\'));
	if (has_input && !absolute && text != "\\" && text != "/") {
		err = "absolute path is required";
		return false;
	}
#else
	const char* home = getenv("HOME");
	std::string text = input && *input ? input : (home && *home ? home : "/");
	if (text[0] != '/') {
		err = "absolute path is required";
		return false;
	}
#endif

	char resolved[PATH_MAX];
	if (realpath(text.c_str(), resolved) == NULL) {
		err = strerror(errno);
		return false;
	}
	out = resolved;
	return true;
}

static std::string current_home_path() {
#ifdef _WIN32
	const std::string home_path = windows_home_path();
	const char* home = home_path.c_str();
#else
	const char* home = getenv("HOME");
	if (home == NULL || *home == '\0') {
		return "/";
	}
#endif
	char resolved[PATH_MAX];
	if (realpath(home, resolved) == NULL) {
		return home;
	}
#ifdef _WIN32
	DIR* dir = opendir(resolved);
	if (dir != NULL) {
		closedir(dir);
		return resolved;
	}
	if (realpath(".", resolved) == NULL) {
		return ".";
	}
#endif
	return resolved;
}

#ifdef _WIN32
static bool is_windows_virtual_root_request(const char* input)
{
	return input != NULL && strcmp(input, "/") == 0;
}

static bool is_windows_drive_root_path(const std::string& path)
{
	std::string text = path;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\\') {
			text[i] = '/';
		}
	}
	while (text.size() > 3 && text[text.size() - 1] == '/') {
		text.erase(text.size() - 1);
	}
	return text.size() >= 2 && text.size() <= 3
		&& ((text[0] >= 'A' && text[0] <= 'Z')
			|| (text[0] >= 'a' && text[0] <= 'z'))
		&& text[1] == ':';
}

static void collect_windows_drive_entries(std::vector<local_entry_t>& entries)
{
	DWORD len = GetLogicalDriveStringsW(0, NULL);
	if (len == 0) {
		return;
	}

	std::vector<wchar_t> buffer((size_t) len + 1, L'\0');
	DWORD written = GetLogicalDriveStringsW(len + 1, &buffer[0]);
	if (written == 0 || written > len) {
		return;
	}

	for (const wchar_t* drive = &buffer[0]; *drive != L'\0';
		drive += lstrlenW(drive) + 1)
	{
		const UINT type = GetDriveTypeW(drive);
		if (type == DRIVE_NO_ROOT_DIR) {
			continue;
		}
		std::string drive_path;
		if (!webcool_wide_to_utf8(drive, drive_path) || drive_path.empty()) {
			continue;
		}

		local_entry_t item;
		item.name = drive_path;
		item.path = drive_path;
		item.directory = true;
		item.empty_directory = false;
		item.size = 0;
		item.created_at = 0;
		item.created_time = "";
		item.modified_at = 0;
		item.modified_time = "";
		entries.push_back(item);
	}

	std::sort(entries.begin(), entries.end(),
		[](const local_entry_t& a, const local_entry_t& b) {
			return a.path < b.path;
		});
}
#endif

static std::string parent_path(const std::string& path) {
#ifdef _WIN32
	if (path.empty()) {
		return current_home_path();
	}
	std::string text = path;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\\') {
			text[i] = '/';
		}
	}
	while (text.size() > 3 && text[text.size() - 1] == '/') {
		text.erase(text.size() - 1);
	}
	if (is_windows_drive_root_path(text)) {
		return "/";
	}
	if (text.size() >= 2 && text[0] == '/' && text[1] == '/') {
		std::string::size_type server_end = text.find('/', 2);
		std::string::size_type share_end = server_end == std::string::npos
			? std::string::npos
			: text.find('/', server_end + 1);
		if (share_end == std::string::npos || share_end == text.size() - 1) {
			return text;
		}
	}
	std::string::size_type pos = text.rfind('/');
	if (pos == std::string::npos) {
		return current_home_path();
	}
	return pos == 2 && text[1] == ':' ? text.substr(0, 3) : text.substr(0, pos);
#else
	if (path.empty() || path == "/") {
		return "/";
	}
	std::string text = path;
	while (text.size() > 1 && text[text.size() - 1] == '/') {
		text.erase(text.size() - 1);
	}
	std::string::size_type pos = text.rfind('/');
	if (pos == std::string::npos || pos == 0) {
		return "/";
	}
	return text.substr(0, pos);
#endif
}

static std::string join_local_path(const std::string& parent,
	const char* name)
{
#ifdef _WIN32
	if (parent.empty()) {
		return name ? name : "";
	}
	const char tail = parent[parent.size() - 1];
	if (tail == '/' || tail == '\\') {
		return parent + name;
	}
	return parent + "\\" + name;
#else
	if (parent == "/") {
		return std::string("/") + name;
	}
	return parent + "/" + name;
#endif
}

static std::string local_base_name(const std::string& path) {
#ifdef _WIN32
	if (path.empty()) {
		return "";
	}
	std::string text = path;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\\') {
			text[i] = '/';
		}
	}
	while (text.size() > 3 && text[text.size() - 1] == '/') {
		text.erase(text.size() - 1);
	}
	if (text.size() <= 3 && text.size() >= 2 && text[1] == ':') {
		return text;
	}
	std::string::size_type pos = text.rfind('/');
	return pos == std::string::npos ? text : text.substr(pos + 1);
#else
	if (path.empty() || path == "/") {
		return "";
	}
	std::string text = path;
	while (text.size() > 1 && text[text.size() - 1] == '/') {
		text.erase(text.size() - 1);
	}
	std::string::size_type pos = text.rfind('/');
	return pos == std::string::npos ? text : text.substr(pos + 1);
#endif
}

static bool validate_local_name_segment(const std::string& name,
	std::string& err)
{
	err.clear();
	if (name.empty()) {
		err = "name is empty";
		return false;
	}
	if (name == "." || name == "..") {
		err = "invalid name";
		return false;
	}
	for (size_t i = 0; i < name.size(); ++i) {
		unsigned char ch = (unsigned char) name[i];
		if (ch < 32 || ch == 127) {
			err = "name contains control character";
			return false;
		}
		if (name[i] == '/' || name[i] == '\\') {
			err = "name must not contain path separator";
			return false;
		}
	}
	return true;
}

static bool is_same_or_child_path(const std::string& base,
	const std::string& candidate)
{
	if (base == candidate) {
		return true;
	}
	if (base == "/") {
		return !candidate.empty() && candidate[0] == '/';
	}
	return candidate.size() > base.size()
		&& candidate.compare(0, base.size(), base) == 0
		&& candidate[base.size()] == '/';
}

static bool is_system_level_directory_path(const std::string& path)
{
	static const char* protected_paths[] = {
		"/",
		"/Applications",
		"/Library",
		"/System",
		"/Users",
		"/Volumes",
		"/bin",
		"/boot",
		"/dev",
		"/etc",
		"/home",
		"/lib",
		"/lib64",
		"/opt",
		"/private",
		"/private/etc",
		"/private/tmp",
		"/private/var",
		"/proc",
		"/root",
		"/run",
		"/sbin",
		"/sys",
		"/tmp",
		"/usr",
		"/var",
	};
	for (size_t i = 0; i < sizeof(protected_paths) / sizeof(protected_paths[0]); ++i) {
		if (path == protected_paths[i]) {
			return true;
		}
	}
	return false;
}

static void format_time(time_t ts, char* buf, size_t size);

static bool ensure_directory(const std::string& path, std::string& err)
{
	struct stat st;
	if (stat(path.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
			return true;
		}
		err = "trash path exists but is not a directory";
		return false;
	}
	if (::mkdir(path.c_str(), 0700) != 0 && errno != EEXIST) {
		err = strerror(errno);
		return false;
	}
	return true;
}

#ifdef __APPLE__
static bool ensure_directory_mode(const std::string& path, mode_t mode,
	std::string& err)
{
	struct stat st;
	if (stat(path.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
			return true;
		}
		err = "trash path exists but is not a directory";
		return false;
	}
	if (::mkdir(path.c_str(), mode) != 0 && errno != EEXIST) {
		err = strerror(errno);
		return false;
	}
	return true;
}

static std::string device_root_for_path(const std::string& path)
{
	struct stat st;
	if (stat(path.c_str(), &st) != 0) {
		return parent_path(path);
	}
	const dev_t dev = st.st_dev;
	std::string current = S_ISDIR(st.st_mode) ? path : parent_path(path);
	while (!current.empty() && current != "/") {
		const std::string parent = parent_path(current);
		struct stat parent_st;
		if (stat(parent.c_str(), &parent_st) != 0
			|| parent_st.st_dev != dev) {
			break;
		}
		current = parent;
	}
	return current.empty() ? "/" : current;
}
#endif

static bool current_trash_files_path(std::string& path, std::string& err)
{
	err.clear();
	path.clear();
	const std::string home = current_home_path();
	if (home.empty() || home == "/") {
		err = "user home directory not found";
		return false;
	}

#ifdef __APPLE__
	path = join_local_path(home, ".Trash");
	return ensure_directory(path, err);
#else
	const std::string trash_root = join_local_path(home, ".local");
	const std::string share_dir = join_local_path(trash_root, "share");
	const std::string trash_dir = join_local_path(share_dir, "Trash");
	const std::string files_dir = join_local_path(trash_dir, "files");
	const std::string info_dir = join_local_path(trash_dir, "info");
	if (!ensure_directory(trash_root, err)
		|| !ensure_directory(share_dir, err)
		|| !ensure_directory(trash_dir, err)
		|| !ensure_directory(files_dir, err)
		|| !ensure_directory(info_dir, err))
	{
		return false;
	}
	path = files_dir;
	return true;
#endif
}

#ifdef __APPLE__
static bool trash_files_path_for_source(const std::string& source,
	std::string& path, std::string& err)
{
	err.clear();
	path.clear();
	const std::string home = current_home_path();
	if (home.empty() || home == "/") {
		err = "user home directory not found";
		return false;
	}

	struct stat source_st;
	struct stat home_st;
	if (stat(source.c_str(), &source_st) != 0) {
		err = strerror(errno);
		return false;
	}
	if (stat(home.c_str(), &home_st) == 0 && source_st.st_dev == home_st.st_dev) {
		path = join_local_path(home, ".Trash");
		return ensure_directory(path, err);
	}

	const std::string volume_root = device_root_for_path(source);
	if (volume_root.empty() || volume_root == "/") {
		path = join_local_path(home, ".Trash");
		return ensure_directory(path, err);
	}
	const std::string trashes = join_local_path(volume_root, ".Trashes");
	if (!ensure_directory_mode(trashes, 01777, err)) {
		return false;
	}
	path = join_local_path(trashes, std::to_string((unsigned) getuid()).c_str());
	return ensure_directory_mode(path, 0700, err);
}
#endif

#ifdef __APPLE__
static bool is_current_trash_files_path(const std::string& path)
{
	std::string trash_path;
	std::string err;
	return current_trash_files_path(trash_path, err) && path == trash_path;
}
#endif

static std::string unique_child_path(const std::string& parent,
	const std::string& name)
{
	std::string dest = join_local_path(parent, name.c_str());
	struct stat st;
	if (stat(dest.c_str(), &st) != 0 && errno == ENOENT) {
		return dest;
	}
	for (int i = 1; i < 10000; ++i) {
		const std::string candidate = join_local_path(parent,
			(name + "." + std::to_string(i)).c_str());
		if (stat(candidate.c_str(), &st) != 0 && errno == ENOENT) {
			return candidate;
		}
	}
	return "";
}

static bool move_file_to_trash(const std::string& path, std::string& trash_path,
	std::string& err)
{
	err.clear();
	trash_path.clear();
	const std::string home = current_home_path();
	if (home.empty() || home == "/") {
		err = "user home directory not found";
		return false;
	}

#ifdef __APPLE__
	std::string trash_dir;
	if (!trash_files_path_for_source(path, trash_dir, err)) {
		return false;
	}
	trash_path = unique_child_path(trash_dir, local_base_name(path));
	if (trash_path.empty()) {
		err = "cannot create unique trash file name";
		return false;
	}
	if (::rename(path.c_str(), trash_path.c_str()) != 0) {
		err = errno == EXDEV
			? "cannot move file to Trash across different file systems"
			: strerror(errno);
		return false;
	}
	return true;
#else
	const auto write_trash_info_file = [](const std::string& info_path,
		const std::string& original_path, std::string& info_err) -> bool
	{
		FILE* fp = fopen(info_path.c_str(), "w");
		if (fp == NULL) {
			info_err = strerror(errno);
			return false;
		}

		char time_buf[32];
		format_time(time(NULL), time_buf, sizeof(time_buf));
		if (fprintf(fp, "[Trash Info]\nPath=%s\nDeletionDate=%s\n",
			original_path.c_str(), time_buf) < 0)
		{
			info_err = strerror(errno);
			fclose(fp);
			return false;
		}
		if (fclose(fp) != 0) {
			info_err = strerror(errno);
			return false;
		}
		return true;
	};

	std::string files_dir;
	if (!current_trash_files_path(files_dir, err)) {
		return false;
	}
	const std::string info_dir = join_local_path(parent_path(files_dir), "info");
	trash_path = unique_child_path(files_dir, local_base_name(path));
	if (trash_path.empty()) {
		err = "cannot create unique trash file name";
		return false;
	}
	if (::rename(path.c_str(), trash_path.c_str()) != 0) {
		err = errno == EXDEV
			? "cannot move file to Trash across different file systems"
			: strerror(errno);
		return false;
	}
	const std::string info_name = local_base_name(trash_path) + ".trashinfo";
	if (!write_trash_info_file(join_local_path(info_dir, info_name.c_str()),
		path, err))
	{
		return false;
	}
	return true;
#endif
}

static bool run_open_command(std::string& err)
{
	err.clear();
#ifdef _WIN32
	return webcool_shell_open_trash(err);
#else
	pid_t pid = fork();
	if (pid < 0) {
		err = strerror(errno);
		return false;
	}
	if (pid == 0) {
#ifdef __APPLE__
		std::string trash_path;
		std::string trash_err;
		if (!current_trash_files_path(trash_path, trash_err)) {
			_exit(127);
		}
		execlp("open", "open", trash_path.c_str(), (char*) NULL);
#else
		execlp("gio", "gio", "open", "trash:///", (char*) NULL);
		execlp("xdg-open", "xdg-open", "trash:///", (char*) NULL);
#endif
		_exit(127);
	}

	int status = 0;
	if (waitpid(pid, &status, 0) < 0) {
		err = strerror(errno);
		return false;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		err = "failed to open system Trash";
		return false;
	}
	return true;
#endif
}

static void set_display_env(void)
{
#ifdef _WIN32
	return;
#else
	// When webcool runs as a background service (e.g. via systemd or
	// a wrapper script), the DISPLAY and DBUS_SESSION_BUS_ADDRESS
	// variables are typically missing.  Without them xdg-open and
	// other GUI helpers cannot launch graphical programs.  Try to
	// inherit from the desktop session; fall back to :0.
	const char *display = getenv("DISPLAY");
	if (!display || display[0] == '\0') {
		// Try to detect the active X11 display from the desktop session.
		// loginctl stores the display for each session.
		struct stat st;
		if (stat("/tmp/.X11-unix/X0", &st) == 0) {
			setenv("DISPLAY", ":0", 1);
		} else if (stat("/tmp/.X11-unix", &st) == 0) {
			// Find the first X socket available
			DIR *d = opendir("/tmp/.X11-unix");
			if (d) {
				struct dirent *de;
				while ((de = readdir(d)) != NULL) {
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
		// Default per-user D-Bus address
		const char *uid_s = getenv("UID");
		uid_t uid = uid_s ? (uid_t)atoi(uid_s) : getuid();
		char addr[128];
		snprintf(addr, sizeof(addr),
			"unix:path=/run/user/%u/bus", (unsigned)uid);
		setenv("DBUS_SESSION_BUS_ADDRESS", addr, 1);
	}
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (!runtime || runtime[0] == '\0') {
		char rd[64];
		snprintf(rd, sizeof(rd), "/run/user/%u", (unsigned)getuid());
		setenv("XDG_RUNTIME_DIR", rd, 1);
	}
#endif
}

static bool run_open_file_command(const std::string& path,
	bool choose_app, std::string& err)
{
	err.clear();
#ifdef _WIN32
	(void) choose_app;
	return webcool_shell_open(path, err);
#else
	pid_t pid = fork();
	if (pid < 0) {
		err = strerror(errno);
		return false;
	}
	if (pid == 0) {
		set_display_env();
#ifdef __APPLE__
		if (choose_app) {
			execlp("osascript", "osascript",
				"-e", "on run argv",
				"-e", "set targetPath to item 1 of argv",
			"-e", "set chosenApp to choose application with prompt \"选择本地播放器\"",
			"-e", "set appName to name of chosenApp",
			"-e", "do shell script \"open -a \" & quoted form of appName & \" \" & quoted form of targetPath",
				"-e", "end run",
				path.c_str(), (char*) NULL);
		}
		execlp("open", "open", path.c_str(), (char*) NULL);
#else
		if (choose_app) {
			// Use zenity to show an application chooser dialog,
			// then open the file with the selected application.
			// zenity --file-selection --filename can pick a .desktop
			// or binary; we use a simple approach: let the user
			// pick an executable, then run it with the file path.
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
			// Strip trailing newline
			size_t len = strlen(chosen);
			while (len > 0 && (chosen[len-1] == '\n'
				|| chosen[len-1] == '\r')) {
				chosen[--len] = '\0';
			}
			if (len == 0) {
				// User cancelled
				_exit(0);
			}
			execlp(chosen, chosen, path.c_str(), (char*) NULL);
			_exit(127);
		}
		execlp("xdg-open", "xdg-open", path.c_str(), (char*) NULL);
		execlp("gio", "gio", "open", path.c_str(), (char*) NULL);
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

static std::string local_file_lock_key(const std::string& path) {
	return std::string("local:") + path;
}

static std::string local_dir_lock_key(const std::string& path) {
	return std::string("local-dir:") + path;
}

static bool ensure_local_dir_unlocked_for_request(const std::string& upload_dir,
	request_t& req, response_t& res, const std::string& path,
	const char* error_message, const char* param_name = "local_dir_password")
{
	bool allowed = false;
	std::string locked_path;
	std::string err;
	const char* password = req.getParameter(param_name);
	if (!local_dir_lock_path_allows(upload_dir, path,
		password ? password : "",
		allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return false;
	}
	if (!allowed) {
		json_error(res, 403, error_message ? error_message : "directory is locked",
			req.isKeepAlive());
		return false;
	}
	return true;
}

static void split_local_paths(const char* input, std::vector<std::string>& paths)
{
	paths.clear();
	std::string text = input ? input : "";
	std::string item;
	for (size_t i = 0; i <= text.size(); ++i) {
		if (i < text.size() && text[i] != '\n') {
			item.push_back(text[i]);
			continue;
		}
		if (!item.empty()) {
			paths.push_back(item);
		}
		item.clear();
	}
}

static std::string unique_upload_path(const std::string& upload_dir,
	const std::string& folder_path, const std::string& name,
	std::string& relative_path)
{
	std::string candidate = folder_path.empty() ? name : (folder_path + "/" + name);
	std::string full = join_upload_path(upload_dir, candidate);
	struct stat st;
	if (stat(full.c_str(), &st) != 0 && errno == ENOENT) {
		relative_path = candidate;
		return full;
	}
	for (int i = 1; i < 10000; ++i) {
		const std::string next_name = name + "." + std::to_string(i);
		candidate = folder_path.empty() ? next_name : (folder_path + "/" + next_name);
		full = join_upload_path(upload_dir, candidate);
		if (stat(full.c_str(), &st) != 0 && errno == ENOENT) {
			relative_path = candidate;
			return full;
		}
	}
	relative_path.clear();
	return "";
}

static std::string unique_upload_directory_relative(
	const std::string& upload_dir, const std::string& folder_path,
	const std::string& name)
{
	std::string candidate = folder_path.empty() ? name : (folder_path + "/" + name);
	std::string full = join_upload_path(upload_dir, candidate);
	struct stat st;
	if (stat(full.c_str(), &st) != 0 && errno == ENOENT) {
		return candidate;
	}
	for (int i = 1; i < 10000; ++i) {
		const std::string next_name = name + "." + std::to_string(i);
		candidate = folder_path.empty() ? next_name : (folder_path + "/" + next_name);
		full = join_upload_path(upload_dir, candidate);
		if (stat(full.c_str(), &st) != 0 && errno == ENOENT) {
			return candidate;
		}
	}
	return "";
}

static std::string join_relative_path(const std::string& parent,
	const std::string& name)
{
	return parent.empty() ? name : (parent + "/" + name);
}

static bool collect_local_import_directory(const std::string& source_dir,
	const std::string& remote_dir, std::vector<std::string>& dirs,
	std::vector<local_import_file_t>& files, std::string& err)
{
	dirs.push_back(remote_dir);
	DIR* dir = opendir(source_dir.c_str());
	if (dir == NULL) {
		err = strerror(errno);
		return false;
	}

	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		const std::string child_source = join_local_path(source_dir, entry->d_name);
		struct stat st;
		if (lstat(child_source.c_str(), &st) != 0) {
			err = strerror(errno);
			closedir(dir);
			return false;
		}
		if (S_ISLNK(st.st_mode)) {
			continue;
		}
		const std::string child_remote = join_relative_path(remote_dir, entry->d_name);
		if (S_ISDIR(st.st_mode)) {
			if (!collect_local_import_directory(child_source, child_remote,
				dirs, files, err))
			{
				closedir(dir);
				return false;
			}
			continue;
		}
		if (!S_ISREG(st.st_mode)) {
			continue;
		}
		local_import_file_t file;
		file.source = child_source;
		file.name = child_remote;
		file.relative_path = child_remote;
		file.size = regular_file_size(file.source);
		files.push_back(file);
	}
	closedir(dir);
	return true;
}

static std::string create_local_import_task_id()
{
	std::lock_guard<webcool::mutex> guard(g_local_import_mutex);
	g_local_import_seq++;
	return std::string("local-import-") + std::to_string((long long) time(NULL))
		+ "-" + std::to_string((long long) getpid())
		+ "-" + std::to_string((long long) g_local_import_seq);
}

static void update_local_import_task(const std::string& task_id,
	const local_import_task_t& task)
{
	std::lock_guard<webcool::mutex> guard(g_local_import_mutex);
	local_import_task_t merged = task;
	std::map<std::string, local_import_task_t>::const_iterator it =
		g_local_import_tasks.find(task_id);
	if (it != g_local_import_tasks.end()) {
		merged.pause_requested = task.pause_requested || it->second.pause_requested;
		merged.cancel_requested = task.cancel_requested || it->second.cancel_requested;
	}
	g_local_import_tasks[task_id] = merged;
}

static local_import_task_t current_local_import_task(const std::string& task_id)
{
	std::lock_guard<webcool::mutex> guard(g_local_import_mutex);
	std::map<std::string, local_import_task_t>::const_iterator it =
		g_local_import_tasks.find(task_id);
	if (it != g_local_import_tasks.end()) {
		return it->second;
	}
	local_import_task_t empty;
	empty.total_bytes = 0;
	empty.copied_bytes = 0;
	empty.total_files = 0;
	empty.saved_count = 0;
	empty.pause_requested = false;
	empty.cancel_requested = true;
	return empty;
}

static bool local_import_wait_if_paused_or_cancelled(const std::string& task_id,
	local_import_task_t& task)
{
	while (true) {
		local_import_task_t snapshot = current_local_import_task(task_id);
		task.pause_requested = snapshot.pause_requested;
		task.cancel_requested = snapshot.cancel_requested;
		if (task.cancel_requested) {
			task.state = "cancelled";
			task.message = "上传已取消";
			update_local_import_task(task_id, task);
			return false;
		}
		if (!task.pause_requested) {
			if (task.state == "paused") {
				task.state = "running";
				task.message = "继续上传";
				update_local_import_task(task_id, task);
			}
			return true;
		}
		task.state = "paused";
		task.message = "上传已暂停";
		update_local_import_task(task_id, task);
		acl_doze(200);
	}
}

static bool copy_regular_file_with_progress(const std::string& source,
	const std::string& dest, const std::string& task_id, size_t file_index,
	local_import_task_t& task, std::string& err)
{
	FILE* in = fopen(source.c_str(), "rb");
	if (in == NULL) {
		err = strerror(errno);
		return false;
	}
	FILE* out = fopen(dest.c_str(), "wb");
	if (out == NULL) {
		err = strerror(errno);
		fclose(in);
		return false;
	}

	char buf[1024 * 64];
	bool ok = true;
	while (true) {
		if (!local_import_wait_if_paused_or_cancelled(task_id, task)) {
			err = "cancelled";
			ok = false;
			break;
		}
		const size_t n = fread(buf, 1, sizeof(buf), in);
		if (n > 0 && fwrite(buf, 1, n, out) != n) {
			err = strerror(errno);
			ok = false;
			break;
		}
		if (n > 0) {
			task.copied_bytes += (long long) n;
			if (file_index < task.copied_sizes.size()) {
				task.copied_sizes[file_index] += (long long) n;
			}
			update_local_import_task(task_id, task);
		}
		if (n < sizeof(buf)) {
			if (ferror(in)) {
				err = strerror(errno);
				ok = false;
			}
			break;
		}
	}
	if (fclose(out) != 0 && ok) {
		err = strerror(errno);
		ok = false;
	}
	fclose(in);
	if (!ok) {
		::unlink(dest.c_str());
	}
	return ok;
}

static bool copy_regular_file_plain(const std::string& source,
	const std::string& dest, mode_t mode, std::string& err)
{
#ifdef _WIN32
	if (webcool_copy_file(source.c_str(), dest.c_str(), true)) {
		(void) chmod(dest.c_str(), mode & 0777);
		return true;
	}
	err = strerror(errno);
	return false;
#else
	FILE* in = fopen(source.c_str(), "rb");
	if (in == NULL) {
		err = strerror(errno);
		return false;
	}
	FILE* out = fopen(dest.c_str(), "wb");
	if (out == NULL) {
		err = strerror(errno);
		fclose(in);
		return false;
	}

	char buf[1024 * 64];
	bool ok = true;
	while (true) {
		const size_t n = fread(buf, 1, sizeof(buf), in);
		if (n > 0 && fwrite(buf, 1, n, out) != n) {
			err = strerror(errno);
			ok = false;
			break;
		}
		if (n < sizeof(buf)) {
			if (ferror(in)) {
				err = strerror(errno);
				ok = false;
			}
			break;
		}
	}
	if (fclose(out) != 0 && ok) {
		err = strerror(errno);
		ok = false;
	}
	fclose(in);
	if (!ok) {
		::unlink(dest.c_str());
		return false;
	}
	(void) chmod(dest.c_str(), mode & 0777);
	return true;
#endif
}

static bool remove_local_path_recursive(const std::string& path,
	std::string& err)
{
	struct stat st;
	if (lstat(path.c_str(), &st) != 0) {
		if (errno == ENOENT) {
			return true;
		}
		err = strerror(errno);
		return false;
	}
	if (!S_ISDIR(st.st_mode)) {
		if (::unlink(path.c_str()) != 0) {
			err = strerror(errno);
			return false;
		}
		return true;
	}

	DIR* dir = opendir(path.c_str());
	if (dir == NULL) {
		err = strerror(errno);
		return false;
	}
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0
			|| strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		const std::string child = join_local_path(path, entry->d_name);
		if (!remove_local_path_recursive(child, err)) {
			closedir(dir);
			return false;
		}
	}
	closedir(dir);
	if (::rmdir(path.c_str()) != 0) {
		err = strerror(errno);
		return false;
	}
	return true;
}

static bool copy_local_path_recursive(const std::string& source,
	const std::string& dest, std::string& err)
{
	struct stat st;
	if (lstat(source.c_str(), &st) != 0) {
		err = strerror(errno);
		return false;
	}
	if (S_ISREG(st.st_mode)) {
		return copy_regular_file_plain(source, dest, st.st_mode, err);
	}
	if (S_ISLNK(st.st_mode)) {
		char target[PATH_MAX];
		const ssize_t n = readlink(source.c_str(), target, sizeof(target) - 1);
		if (n < 0) {
			err = strerror(errno);
			return false;
		}
		target[n] = '\0';
		if (::symlink(target, dest.c_str()) != 0) {
			err = strerror(errno);
			return false;
		}
		return true;
	}
	if (!S_ISDIR(st.st_mode)) {
		err = "unsupported file type in directory copy";
		return false;
	}

	if (::mkdir(dest.c_str(), st.st_mode & 0777) != 0) {
		err = strerror(errno);
		return false;
	}
	bool ok = true;
	DIR* dir = opendir(source.c_str());
	if (dir == NULL) {
		err = strerror(errno);
		ok = false;
	} else {
		struct dirent* entry;
		while ((entry = readdir(dir)) != NULL) {
			if (strcmp(entry->d_name, ".") == 0
				|| strcmp(entry->d_name, "..") == 0) {
				continue;
			}
			const std::string child_source = join_local_path(source, entry->d_name);
			const std::string child_dest = join_local_path(dest, entry->d_name);
			if (!copy_local_path_recursive(child_source, child_dest, err)) {
				ok = false;
				break;
			}
		}
		closedir(dir);
	}
	if (!ok) {
		std::string cleanup_err;
		remove_local_path_recursive(dest, cleanup_err);
		return false;
	}
	(void) chmod(dest.c_str(), st.st_mode & 0777);
	return true;
}

static void run_local_import_task(const std::string& task_id,
	const std::string& upload_dir, const std::string& folder_path,
	const std::vector<std::string>& dirs,
	const std::vector<local_import_file_t>& files)
{
	local_import_task_t task;
	task.state = "running";
	task.message = "准备上传";
	task.total_bytes = 0;
	task.copied_bytes = 0;
	task.total_files = (int) files.size();
	task.saved_count = 0;
	task.pause_requested = false;
	task.cancel_requested = false;
	for (size_t i = 0; i < files.size(); ++i) {
		task.total_bytes += files[i].size;
		task.names.push_back(files[i].name);
		task.remote_paths.push_back("");
		task.sizes.push_back(files[i].size);
		task.copied_sizes.push_back(0);
		task.file_states.push_back("pending");
	}
	update_local_import_task(task_id, task);

	for (size_t i = 0; i < dirs.size(); ++i) {
		if (!local_import_wait_if_paused_or_cancelled(task_id, task)) {
			return;
		}
		const std::string full_dir = join_upload_path(upload_dir, dirs[i]);
		if (!make_dir_recursive(full_dir.c_str())) {
			task.state = "failed";
			task.error = "cannot create target directory";
			update_local_import_task(task_id, task);
			return;
		}
	}

	for (size_t i = 0; i < files.size(); ++i) {
		if (!local_import_wait_if_paused_or_cancelled(task_id, task)) {
			return;
		}
		task.message = std::string("上传中：") + files[i].name;
		task.file_states[i] = "running";
		update_local_import_task(task_id, task);

		const std::string relative_path = files[i].relative_path.empty()
			? (folder_path.empty() ? files[i].name : (folder_path + "/" + files[i].name))
			: files[i].relative_path;
		const std::string dest = join_upload_path(upload_dir, relative_path);
		if (!make_dir_recursive(parent_path(dest).c_str())) {
			task.state = "failed";
			task.error = "cannot create target parent directory";
			update_local_import_task(task_id, task);
			return;
		}
		task.remote_paths[i] = relative_path;
		update_local_import_task(task_id, task);

		std::string err;
		if (!copy_regular_file_with_progress(files[i].source, dest,
			task_id, i, task, err))
		{
			task.file_states[i] = task.cancel_requested ? "cancelled" : "failed";
			task.state = task.cancel_requested ? "cancelled" : "failed";
			task.error = err;
			update_local_import_task(task_id, task);
			return;
		}
		task.copied_sizes[i] = files[i].size;
		task.file_states[i] = "done";
		task.saved_count++;
		update_local_import_task(task_id, task);
	}

	task.state = "done";
	task.message = "上传完成";
	task.copied_bytes = task.total_bytes;
	update_local_import_task(task_id, task);
	std::string sync_err;
	if (storage_backup_upload_auto_sync_enabled(upload_dir, sync_err)) {
		std::vector<std::string> sync_paths;
		std::vector<std::string> delete_paths;
		for (size_t i = 0; i < task.remote_paths.size(); ++i) {
			if (!task.remote_paths[i].empty()) {
				sync_paths.push_back(task.remote_paths[i]);
			}
		}
		for (size_t i = 0; i < dirs.size(); ++i) {
			if (!dirs[i].empty()) {
				sync_paths.push_back(dirs[i]);
			}
		}
		(void) storage_backup_sync_paths(upload_dir, sync_paths, delete_paths, sync_err);
	}
}

static bool validate_local_name(const std::string& name, std::string& err) {
	err.clear();
	if (name.empty()) {
		err = "directory name is empty";
		return false;
	}
	if (name == "." || name == "..") {
		err = "invalid directory name";
		return false;
	}
	if (name.size() > 120) {
		err = "directory name is too long";
		return false;
	}
	for (size_t i = 0; i < name.size(); ++i) {
		unsigned char c = (unsigned char) name[i];
		if (c < 32 || c == 127) {
			err = "directory name contains control character";
			return false;
		}
		if (name[i] == '/' || name[i] == '\\') {
			err = "directory name cannot contain slash";
			return false;
		}
	}
	return true;
}

static bool directory_is_empty(const std::string& path) {
#ifdef _WIN32
	std::wstring pattern = windows_dir_pattern_from_utf8(path);
	if (pattern.empty()) {
		return false;
	}
	WIN32_FIND_DATAW data;
	HANDLE handle = FindFirstFileW(pattern.c_str(), &data);
	if (handle == INVALID_HANDLE_VALUE) {
		return false;
	}
	do {
		std::string name;
		if (!webcool_wide_to_utf8(data.cFileName, name)) {
			continue;
		}
		if (name == "." || name == "..") {
			continue;
		}
		FindClose(handle);
		return false;
	} while (FindNextFileW(handle, &data));
	FindClose(handle);
	return true;
#else
	DIR* dir = opendir(path.c_str());
	if (dir == NULL) {
		return false;
	}
	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		closedir(dir);
		return false;
	}
	closedir(dir);
	return true;
#endif
}

static void format_time(time_t ts, char* buf, size_t size) {
	if (buf == NULL || size == 0) {
		return;
	}
	struct tm tmv;
	acl_localtime_r(&ts, &tmv);
	strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tmv);
}

#ifdef _WIN32
static time_t windows_file_time_to_unix(const FILETIME& ft)
{
	ULARGE_INTEGER value;
	value.HighPart = ft.dwHighDateTime;
	value.LowPart = ft.dwLowDateTime;
	const unsigned long long ticks_per_second = 10000000ULL;
	const unsigned long long unix_epoch = 11644473600ULL;
	const unsigned long long seconds = value.QuadPart / ticks_per_second;
	return seconds > unix_epoch ? (time_t) (seconds - unix_epoch) : 0;
}

static bool collect_windows_directory_entries(const std::string& path,
	bool show_hidden, std::vector<local_entry_t>& entries, std::string& err)
{
	err.clear();
	const std::wstring pattern = windows_dir_pattern_from_utf8(path);
	if (pattern.empty()) {
		err = "invalid UTF-8 path";
		return false;
	}

	WIN32_FIND_DATAW data;
	HANDLE handle = FindFirstFileW(pattern.c_str(), &data);
	if (handle == INVALID_HANDLE_VALUE) {
		err = windows_last_error_message(GetLastError());
		return false;
	}

	do {
		std::string name;
		if (!webcool_wide_to_utf8(data.cFileName, name)) {
			continue;
		}
		if (name == "." || name == "..") {
			continue;
		}
		const DWORD attrs = data.dwFileAttributes;
		if (!show_hidden
			&& ((attrs & FILE_ATTRIBUTE_HIDDEN) != 0
				|| (attrs & FILE_ATTRIBUTE_SYSTEM) != 0))
		{
			continue;
		}
		if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0
			&& (attrs & FILE_ATTRIBUTE_ARCHIVE) == 0
			&& (attrs & FILE_ATTRIBUTE_NORMAL) == 0)
		{
			continue;
		}

		const std::string child_path = join_local_path(path, name.c_str());
		local_entry_t item;
		item.name = name;
		item.path = child_path;
		item.directory = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
		item.empty_directory = item.directory && directory_is_empty(child_path);
		ULARGE_INTEGER size;
		size.HighPart = data.nFileSizeHigh;
		size.LowPart = data.nFileSizeLow;
		item.size = item.directory ? 0 : (long long) size.QuadPart;
		item.created_at = (long long) windows_file_time_to_unix(data.ftCreationTime);
		item.modified_at = (long long) windows_file_time_to_unix(data.ftLastWriteTime);
		char created_buf[32];
		char modified_buf[32];
		format_time((time_t) item.created_at, created_buf, sizeof(created_buf));
		format_time((time_t) item.modified_at, modified_buf, sizeof(modified_buf));
		item.created_time = created_buf;
		item.modified_time = modified_buf;
		entries.push_back(item);
	} while (FindNextFileW(handle, &data));

	const DWORD last_error = GetLastError();
	FindClose(handle);
	if (last_error != ERROR_NO_MORE_FILES) {
		err = windows_last_error_message(last_error);
		return false;
	}

	std::sort(entries.begin(), entries.end(),
		[](const local_entry_t& a, const local_entry_t& b) {
			if (a.directory != b.directory) {
				return a.directory > b.directory;
			}
			return a.name < b.name;
		});
	return true;
}
#endif

static bool ends_with_ignore_case(const std::string& text, const char* suffix) {
	size_t suffix_len = suffix ? strlen(suffix) : 0;
	if (suffix_len == 0 || text.size() < suffix_len) {
		return false;
	}
	const size_t start = text.size() - suffix_len;
	for (size_t i = 0; i < suffix_len; ++i) {
		unsigned char a = (unsigned char) text[start + i];
		unsigned char b = (unsigned char) suffix[i];
		if (a >= 'A' && a <= 'Z') {
			a = (unsigned char) (a - 'A' + 'a');
		}
		if (b >= 'A' && b <= 'Z') {
			b = (unsigned char) (b - 'A' + 'a');
		}
		if (a != b) {
			return false;
		}
	}
	return true;
}

static const char* content_type_for_file(const std::string& name) {
	if (ends_with_ignore_case(name, ".png")) return "image/png";
	if (ends_with_ignore_case(name, ".jpg") || ends_with_ignore_case(name, ".jpeg")) return "image/jpeg";
	if (ends_with_ignore_case(name, ".gif")) return "image/gif";
	if (ends_with_ignore_case(name, ".heic")) return "image/heic";
	if (ends_with_ignore_case(name, ".heif")) return "image/heif";
	if (ends_with_ignore_case(name, ".mp4")) return "video/mp4";
	if (ends_with_ignore_case(name, ".mkv")) return "video/x-matroska";
	if (ends_with_ignore_case(name, ".avi")) return "video/x-msvideo";
	if (ends_with_ignore_case(name, ".mp3")) return "audio/mpeg";
	if (ends_with_ignore_case(name, ".m4a")) return "audio/mp4";
	if (ends_with_ignore_case(name, ".aac")) return "audio/aac";
	if (ends_with_ignore_case(name, ".wav")) return "audio/wav";
	if (ends_with_ignore_case(name, ".ogg")) return "audio/ogg";
	if (ends_with_ignore_case(name, ".flac")) return "audio/flac";
	if (ends_with_ignore_case(name, ".pdf")) return "application/pdf";
	if (ends_with_ignore_case(name, ".txt") || ends_with_ignore_case(name, ".md")
		|| ends_with_ignore_case(name, ".log") || ends_with_ignore_case(name, ".csv")
		|| ends_with_ignore_case(name, ".json") || ends_with_ignore_case(name, ".xml")
		|| ends_with_ignore_case(name, ".js") || ends_with_ignore_case(name, ".ts")
		|| ends_with_ignore_case(name, ".cpp") || ends_with_ignore_case(name, ".h")
		|| ends_with_ignore_case(name, ".py") || ends_with_ignore_case(name, ".sh"))
	{
		return "text/plain; charset=utf-8";
	}
	return "application/octet-stream";
}

static bool parse_range_value(const char* s, long long& out) {
	if (s == NULL || *s == '\0') {
		return false;
	}
	errno = 0;
	char* end = NULL;
	long long v = strtoll(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0' || v < 0) {
		return false;
	}
	out = v;
	return true;
}

static bool parse_range_header(const char* range, long long size,
	long long& begin, long long& end)
{
	if (range == NULL || size <= 0) {
		return false;
	}
	if (strncasecmp(range, "bytes=", 6) != 0) {
		return false;
	}
	const char* expr = range + 6;
	if (*expr == '\0' || strchr(expr, ',') != NULL) {
		return false;
	}
	const char* dash = strchr(expr, '-');
	if (dash == NULL) {
		return false;
	}
	if (dash == expr) {
		long long suffix = 0;
		if (!parse_range_value(dash + 1, suffix) || suffix <= 0) {
			return false;
		}
		if (suffix > size) {
			suffix = size;
		}
		begin = size - suffix;
		end = size - 1;
		return true;
	}

	acl::string left(expr, (size_t) (dash - expr));
	long long start = 0;
	if (!parse_range_value(left.c_str(), start) || start >= size) {
		return false;
	}
	if (*(dash + 1) == '\0') {
		begin = start;
		end = size - 1;
		return true;
	}

	long long stop = 0;
	if (!parse_range_value(dash + 1, stop) || stop < start) {
		return false;
	}
	if (stop >= size) {
		stop = size - 1;
	}
	begin = start;
	end = stop;
	return true;
}

} // namespace

} // namespace action

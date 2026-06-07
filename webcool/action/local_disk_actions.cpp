#include "actions.h"
#include "action_util.h"

#ifdef _WIN32
#include "../platform_compat.h"
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
#include <mutex>
#include <set>
#include <stdlib.h>
#include <string>
#include <thread>
#include <vector>

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
	wchar_t path[MAX_PATH];
	memset(path, 0, sizeof(path));
	if (SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT, path)
		== S_OK && path[0] != L'\0')
	{
		std::string utf8;
		if (webcool_wide_to_utf8(path, utf8) && !utf8.empty()) {
			return utf8;
		}
	}

	const char* home = getenv("USERPROFILE");
	if (home == NULL || *home == '\0') {
		home = getenv("HOME");
	}
	return home && *home ? home : ".";
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
	if (!webcool_utf8_to_wide(path.c_str(), pattern)) {
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
};

static std::mutex g_local_import_mutex;
static std::map<std::string, local_import_task_t> g_local_import_tasks;
static unsigned long long g_local_import_seq = 0;

static std::string parent_path(const std::string& path);

static void json_error(response_t& res, int status, const char* msg,
	bool keep_alive)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", msg ? msg : "unknown error");
	sendJson(res, status, root, keep_alive);
}

static void json_locked_dir_error(response_t& res, const char* msg,
	const std::string& path, const std::string& locked_path, bool keep_alive)
{
	const std::string display_path = locked_path.empty() ? path : locked_path;
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", msg ? msg : "directory is locked");
	root.add_text("path", display_path.c_str());
	root.add_text("parent_path", parent_path(display_path).c_str());
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
	std::lock_guard<std::mutex> guard(g_local_import_mutex);
	g_local_import_seq++;
	return std::string("local-import-") + std::to_string((long long) time(NULL))
		+ "-" + std::to_string((long long) getpid())
		+ "-" + std::to_string((long long) g_local_import_seq);
}

static void update_local_import_task(const std::string& task_id,
	const local_import_task_t& task)
{
	std::lock_guard<std::mutex> guard(g_local_import_mutex);
	g_local_import_tasks[task_id] = task;
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
		const std::string full_dir = join_upload_path(upload_dir, dirs[i]);
		if (!make_dir_recursive(full_dir.c_str())) {
			task.state = "failed";
			task.error = "cannot create target directory";
			update_local_import_task(task_id, task);
			return;
		}
	}

	for (size_t i = 0; i < files.size(); ++i) {
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
			task.file_states[i] = "failed";
			task.state = "failed";
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

bool LocalDiskListAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string path;
	std::string err;
	std::vector<local_entry_t> entries;
	bool virtual_root = false;
#ifdef _WIN32
	if (is_windows_virtual_root_request(req.getParameter("path"))) {
		path = "/";
		virtual_root = true;
		collect_windows_drive_entries(entries);
	} else
#endif
	{
		if (!normalize_local_path(req.getParameter("path"), path, err)) {
			json_error(res, 400, err.c_str(), req.isKeepAlive());
			return true;
		}

		struct stat st;
		if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
			json_error(res, 404, "directory not found", req.isKeepAlive());
			return true;
		}
		bool allowed = false;
		std::string locked_path;
		const char* password = req.getParameter("local_dir_password");
		if (!local_dir_lock_path_allows(upload_dir, path,
			password ? password : "", allowed, locked_path, err))
		{
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!allowed)
		{
			json_locked_dir_error(res, "directory is locked", path, locked_path,
				req.isKeepAlive());
			return true;
		}

		const char* show_hidden_text = req.getParameter("show_hidden");
		const bool show_hidden = show_hidden_text != NULL
			&& (strcmp(show_hidden_text, "1") == 0
				|| strcasecmp(show_hidden_text, "true") == 0
				|| strcasecmp(show_hidden_text, "yes") == 0);
#ifdef _WIN32
		if (!collect_windows_directory_entries(path, show_hidden, entries, err)) {
			json_error(res, 403, err.c_str(), req.isKeepAlive());
			return true;
		}
#else
		DIR* dir = opendir(path.c_str());
		if (dir == NULL) {
#ifdef __APPLE__
			if ((errno == EPERM || errno == EACCES)
				&& is_current_trash_files_path(path))
			{
				json_error(res, 403,
					"macOS blocked access to Trash. Please grant Full Disk Access to the program or terminal that starts webcool, then restart webcool.",
					req.isKeepAlive());
				return true;
			}
#endif
			json_error(res, 403, strerror(errno), req.isKeepAlive());
			return true;
		}

		struct dirent* entry = NULL;
		while ((entry = readdir(dir)) != NULL) {
			if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
				continue;
			}
			if (!show_hidden && entry->d_name[0] == '.') {
				continue;
			}
			const std::string child_path = join_local_path(path, entry->d_name);
			struct stat child_st;
			if (stat(child_path.c_str(), &child_st) != 0) {
				continue;
			}
			if (!S_ISDIR(child_st.st_mode) && !S_ISREG(child_st.st_mode)) {
				continue;
			}

			char modified_buf[32];
			format_time(child_st.st_mtime, modified_buf, sizeof(modified_buf));
#ifdef __APPLE__
			const time_t created_ts = child_st.st_birthtimespec.tv_sec > 0
				? child_st.st_birthtimespec.tv_sec
				: child_st.st_ctime;
#else
			const time_t created_ts = child_st.st_ctime;
#endif
			char created_buf[32];
			format_time(created_ts, created_buf, sizeof(created_buf));
			local_entry_t item;
			item.name = entry->d_name;
			item.path = child_path;
			item.directory = S_ISDIR(child_st.st_mode);
			item.empty_directory = item.directory && directory_is_empty(child_path);
			item.size = item.directory ? 0 : regular_file_size(child_path);
			item.created_at = (long long) created_ts;
			item.created_time = created_buf;
			item.modified_at = (long long) child_st.st_mtime;
			item.modified_time = modified_buf;
			entries.push_back(item);
		}
		closedir(dir);

		std::sort(entries.begin(), entries.end(),
			[](const local_entry_t& a, const local_entry_t& b) {
				if (a.directory != b.directory) {
					return a.directory > b.directory;
				}
				return a.name < b.name;
			});
#endif
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", path.c_str());
	const std::string parent = virtual_root ? "/" : parent_path(path);
	root.add_text("parent_path", parent.c_str());
	root.add_text("home_path", current_home_path().c_str());
	root.add_bool("virtual_root", virtual_root);
	std::string trash_path;
	if (current_trash_files_path(trash_path, err)) {
		root.add_text("trash_path", trash_path.c_str());
	}
	acl::json_node& items = json.create_array();
	root.add_child("items", items);
	for (size_t i = 0; i < entries.size(); ++i) {
		acl::json_node& item = items.add_child(false, true);
		item.add_text("name", entries[i].name.c_str());
		item.add_text("path", entries[i].path.c_str());
		item.add_bool("directory", entries[i].directory);
		item.add_bool("empty_directory", entries[i].empty_directory);
		if (entries[i].directory && !virtual_root) {
			bool locked = false;
			std::string lock_err;
			if (local_dir_lock_path_has_lock(upload_dir, entries[i].path, locked, lock_err)) {
				item.add_bool("locked", locked);
			}
		} else {
			bool locked = false;
			std::string lock_err;
			if (file_lock_path_has_lock(upload_dir, local_file_lock_key(entries[i].path), locked, lock_err)) {
				item.add_bool("locked", locked);
			}
		}
		item.add_number("size", entries[i].size);
		item.add_number("created_at", entries[i].created_at);
		item.add_text("created_time", entries[i].created_time.c_str());
		item.add_number("modified_at", entries[i].modified_at);
		item.add_text("modified_time", entries[i].modified_time.c_str());
	}
	root.add_number("count", (long long) entries.size());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskDownloadAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string path;
	std::string err;
	if (!normalize_local_path(req.getParameter("path"), path, err)) {
		return sendText(res, 400, err.c_str(), req.isKeepAlive());
	}

	struct stat st;
	if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		return sendText(res, 404, "file not found\n", req.isKeepAlive());
	}
	bool dir_allowed = false;
	std::string locked_dir;
	std::string dir_lock_err;
	if (!local_dir_lock_path_allows(upload_dir, parent_path(path),
		req.getParameter("local_dir_password") ? req.getParameter("local_dir_password") : "",
		dir_allowed, locked_dir, dir_lock_err))
	{
		return sendText(res, 500, dir_lock_err.c_str(), req.isKeepAlive());
	}
	if (!dir_allowed) {
		return sendText(res, 403, "directory is locked\n", req.isKeepAlive());
	}
	bool file_lock_allowed = false;
	std::string lock_err;
	if (!file_lock_path_allows(upload_dir, local_file_lock_key(path),
		req.getParameter("file_password") ? req.getParameter("file_password") : "",
		file_lock_allowed, lock_err))
	{
		return sendText(res, 500, lock_err.c_str(), req.isKeepAlive());
	}
	if (!file_lock_allowed) {
		return sendText(res, 403, "file is locked\n", req.isKeepAlive());
	}

	FILE* in = fopen(path.c_str(), "rb");
	if (in == NULL) {
		return sendText(res, 403, "file cannot be read\n", req.isKeepAlive());
	}

	const long long fsize = regular_file_size(path);
	if (fsize < 0) {
		fclose(in);
		return sendText(res, 500, "cannot read file size\n", req.isKeepAlive());
	}
	if (fsize == 0) {
		fclose(in);
		return sendText(res, 409, "file is empty\n", req.isKeepAlive());
	}
	const std::string name = local_base_name(path);
	const char* ctype = content_type_for_file(name);
	acl::string dispo;
	dispo.format("inline; filename=\"%s\"", name.c_str());
	const char* range = req.getHeader("Range");
	long long range_begin = 0;
	long long range_end = 0;
	const bool has_range = parse_range_header(range, fsize, range_begin, range_end);
	const bool want_range = range != NULL && *range != '\0';
	if (want_range && !has_range) {
		acl::string cr;
		cr.format("bytes */%lld", fsize);
		res.setStatus(416)
			.setKeepAlive(req.isKeepAlive())
			.setHeader("Content-Range", cr.c_str())
			.setHeader("Accept-Ranges", "bytes")
			.setContentType("text/plain; charset=utf-8");
		const char* msg = "invalid range\n";
		res.setContentLength((long long) strlen(msg));
		fclose(in);
		return res.write(msg, strlen(msg)) && res.write(NULL, 0);
	}

	const long long send_begin = has_range ? range_begin : 0;
	const long long send_end = has_range ? range_end : (fsize - 1);
	const long long send_size = send_end - send_begin + 1;
	if (send_size < 0) {
		fclose(in);
		return sendText(res, 500, "invalid send size\n", req.isKeepAlive());
	}
	if (send_begin > 0 && !seek_file64(in, send_begin)) {
		fclose(in);
		return sendText(res, 500, "seek file failed\n", req.isKeepAlive());
	}

	if (has_range) {
		acl::string content_range;
		content_range.format("bytes %lld-%lld/%lld", send_begin, send_end, fsize);
		res.setStatus(206)
			.setKeepAlive(req.isKeepAlive())
			.setContentType(ctype)
			.setHeader("Content-Disposition", dispo.c_str())
			.setHeader("Accept-Ranges", "bytes")
			.setHeader("Content-Range", content_range.c_str())
			.setContentLength(send_size);
	} else {
		res.setStatus(200)
			.setKeepAlive(req.isKeepAlive())
			.setContentType(ctype)
			.setHeader("Content-Disposition", dispo.c_str())
			.setHeader("Accept-Ranges", "bytes")
			.setContentLength(fsize);
	}

	ctype = "application/octet-stream";
	res.setContentType(ctype);

	char buf[8192];
	long long remain = send_size;
	while (remain > 0) {
		size_t want = sizeof(buf);
		if ((long long) want > remain) {
			want = (size_t) remain;
		}
		const size_t n = fread(buf, 1, want, in);
		if (n == 0) {
			break;
		}
		if (!res.write(buf, (size_t) n)) {
			fclose(in);
			return false;
		}
		remain -= (long long) n;
	}
	fclose(in);
	return res.write(NULL, 0);
}

bool LocalDiskDeleteAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string path;
	std::string err;
	if (!normalize_local_path(req.getParameter("path"), path, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	struct stat st;
	if (stat(path.c_str(), &st) != 0) {
		json_error(res, 404, "path not found", req.isKeepAlive());
		return true;
	}
	const bool is_dir = S_ISDIR(st.st_mode);
	const bool is_file = S_ISREG(st.st_mode);
	if (!is_dir && !is_file) {
		json_error(res, 400, "only files and directories can be deleted", req.isKeepAlive());
		return true;
	}
	if (!ensure_local_dir_unlocked_for_request(upload_dir, req, res,
		is_dir ? path : parent_path(path),
		is_dir ? "directory is locked" : "parent directory is locked"))
	{
		return true;
	}
	if (is_file) {
		bool file_lock_allowed = false;
		std::string lock_err;
		if (!file_lock_path_allows(upload_dir, local_file_lock_key(path),
			req.getParameter("file_password") ? req.getParameter("file_password") : "",
			file_lock_allowed, lock_err))
		{
			json_error(res, 500, lock_err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!file_lock_allowed) {
			json_error(res, 403, "file is locked", req.isKeepAlive());
			return true;
		}
	}
	if (path == "/") {
		json_error(res, 409, "root directory cannot be deleted", req.isKeepAlive());
		return true;
	}

	if (is_dir && is_system_level_directory_path(path)) {
		json_error(res, 409, "system directory cannot be deleted",
			req.isKeepAlive());
		return true;
	}

	std::string trash_path;
	if (!move_file_to_trash(path, trash_path, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	std::string rename_err;
	const std::string old_local_key = std::string("local:") + path;
	const std::string new_local_key = std::string("local:") + trash_path;
	if (is_dir) {
		if (!tag_rename_folder_prefix(upload_dir, old_local_key, new_local_key,
			rename_err)
			|| !video_resume_rename_folder_prefix(upload_dir, old_local_key,
				new_local_key, rename_err)
			|| !file_lock_rename_prefix(upload_dir, local_dir_lock_key(path),
				local_dir_lock_key(trash_path), rename_err)
			|| !file_lock_rename_prefix(upload_dir, local_file_lock_key(path),
				local_file_lock_key(trash_path), rename_err))
		{
			(void) ::rename(trash_path.c_str(), path.c_str());
			json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
			return true;
		}
	} else {
		if (!tag_rename_file(upload_dir, old_local_key, new_local_key, rename_err)
			|| !video_resume_rename_file(upload_dir, old_local_key,
				new_local_key, rename_err)
			|| !file_lock_rename_key(upload_dir, local_file_lock_key(path),
				local_file_lock_key(trash_path), rename_err))
		{
			(void) ::rename(trash_path.c_str(), path.c_str());
			json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
			return true;
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", path.c_str());
	root.add_text("trash_path", trash_path.c_str());
	root.add_text("message", is_dir ? "directory moved to trash" : "file moved to trash");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskCreateDirAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string parent;
	std::string err;
	if (!normalize_local_path(req.getParameter("path"), parent, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	struct stat st;
	if (stat(parent.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
		json_error(res, 404, "parent directory not found", req.isKeepAlive());
		return true;
	}
	if (!ensure_local_dir_unlocked_for_request(upload_dir, req, res, parent,
		"parent directory is locked"))
	{
		return true;
	}

	const char* name_param = req.getParameter("name");
	std::string name = name_param ? name_param : "";
	while (!name.empty() && (name[0] == ' ' || name[0] == '\t')) {
		name.erase(0, 1);
	}
	while (!name.empty() && (name[name.size() - 1] == ' ' || name[name.size() - 1] == '\t')) {
		name.erase(name.size() - 1);
	}
	if (!validate_local_name(name, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	const std::string new_path = join_local_path(parent, name.c_str());
	if (::mkdir(new_path.c_str(), 0755) != 0) {
		json_error(res, errno == EEXIST ? 409 : 500,
			errno == EEXIST ? "directory already exists" : strerror(errno),
			req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", new_path.c_str());
	root.add_text("name", name.c_str());
	root.add_text("message", "directory created");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskMoveAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string source;
	std::string target;
	std::string err;
	const char* source_param = req.getParameter("path");
	const char* target_param = req.getParameter("target");
	if (source_param == NULL || *source_param == '\0'
		|| target_param == NULL || *target_param == '\0')
	{
		json_error(res, 400, "source path and target directory are required",
			req.isKeepAlive());
		return true;
	}
	if (!normalize_local_path(source_param, source, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!normalize_local_path(target_param, target, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	struct stat source_st;
	if (stat(source.c_str(), &source_st) != 0) {
		json_error(res, 404, "source path not found", req.isKeepAlive());
		return true;
	}
	const bool source_is_dir = S_ISDIR(source_st.st_mode);
	if (!source_is_dir && !S_ISREG(source_st.st_mode)) {
		json_error(res, 400, "only files and directories can be moved",
			req.isKeepAlive());
		return true;
	}
	if (!source_is_dir) {
		bool file_lock_allowed = false;
		std::string lock_err;
		if (!file_lock_path_allows(upload_dir, local_file_lock_key(source),
			req.getParameter("file_password") ? req.getParameter("file_password") : "",
			file_lock_allowed, lock_err))
		{
			json_error(res, 500, lock_err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!file_lock_allowed) {
			json_error(res, 403, "file is locked", req.isKeepAlive());
			return true;
		}
	}
	if (source == "/") {
		json_error(res, 409, "root directory cannot be moved", req.isKeepAlive());
		return true;
	}
	if (source_is_dir && is_system_level_directory_path(source)) {
		json_error(res, 409, "system directory cannot be moved",
			req.isKeepAlive());
		return true;
	}
	if (!ensure_local_dir_unlocked_for_request(upload_dir, req, res,
		source_is_dir ? source : parent_path(source),
		source_is_dir ? "source directory is locked" : "source parent directory is locked"))
	{
		return true;
	}

	struct stat target_st;
	if (stat(target.c_str(), &target_st) != 0 || !S_ISDIR(target_st.st_mode)) {
		json_error(res, 404, "target directory not found", req.isKeepAlive());
		return true;
	}
	if (!ensure_local_dir_unlocked_for_request(upload_dir, req, res, target,
		"target directory is locked", "target_local_dir_password"))
	{
		return true;
	}
	if (source_is_dir && is_same_or_child_path(source, target)) {
		json_error(res, 409, "directory cannot be moved into itself",
			req.isKeepAlive());
		return true;
	}
	if (parent_path(source) == target) {
		json_error(res, 409, "source is already in target directory",
			req.isKeepAlive());
		return true;
	}

	const std::string name = local_base_name(source);
	if (name.empty()) {
		json_error(res, 400, "invalid source path", req.isKeepAlive());
		return true;
	}
	const std::string dest = join_local_path(target, name.c_str());
	struct stat dest_st;
	if (stat(dest.c_str(), &dest_st) == 0) {
		json_error(res, 409, "target already contains a path with same name",
			req.isKeepAlive());
		return true;
	}
	if (errno != ENOENT) {
		json_error(res, 500, strerror(errno), req.isKeepAlive());
		return true;
	}

	if (::rename(source.c_str(), dest.c_str()) != 0) {
		json_error(res, errno == EXDEV ? 409 : 500,
			errno == EXDEV ? "cannot move across different file systems" : strerror(errno),
			req.isKeepAlive());
		return true;
	}
	if (!source_is_dir && !file_lock_rename_key(upload_dir,
		local_file_lock_key(source), local_file_lock_key(dest), err))
	{
		(void) ::rename(dest.c_str(), source.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (source_is_dir && !file_lock_rename_key(upload_dir,
		local_dir_lock_key(source), local_dir_lock_key(dest), err))
	{
		(void) ::rename(dest.c_str(), source.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", dest.c_str());
	root.add_text("old_path", source.c_str());
	root.add_text("target", target.c_str());
	root.add_text("message", source_is_dir ? "directory moved" : "file moved");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskCopyAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string source;
	std::string target;
	std::string err;
	const char* source_param = req.getParameter("path");
	const char* target_param = req.getParameter("target");
	const bool async = req.getParameter("async") != NULL
		&& strcmp(req.getParameter("async"), "1") == 0;
	const bool overwrite = req.getParameter("overwrite") != NULL
		&& strcmp(req.getParameter("overwrite"), "1") == 0;
	if (source_param == NULL || *source_param == '\0'
		|| target_param == NULL || *target_param == '\0')
	{
		json_error(res, 400, "source path and target directory are required",
			req.isKeepAlive());
		return true;
	}
	if (!normalize_local_path(source_param, source, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!normalize_local_path(target_param, target, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	struct stat source_st;
	if (stat(source.c_str(), &source_st) != 0) {
		json_error(res, 404, "source path not found", req.isKeepAlive());
		return true;
	}
	const bool source_is_dir = S_ISDIR(source_st.st_mode);
	const bool source_is_file = S_ISREG(source_st.st_mode);
	if (!source_is_dir && !source_is_file) {
		json_error(res, 400, "only files and directories can be copied",
			req.isKeepAlive());
		return true;
	}
	if (source == "/") {
		json_error(res, 409, "root directory cannot be copied", req.isKeepAlive());
		return true;
	}
	if (source_is_dir && is_system_level_directory_path(source)) {
		json_error(res, 409, "system directory cannot be copied",
			req.isKeepAlive());
		return true;
	}
	if (!source_is_dir) {
		bool file_lock_allowed = false;
		std::string lock_err;
		if (!file_lock_path_allows(upload_dir, local_file_lock_key(source),
			req.getParameter("file_password") ? req.getParameter("file_password") : "",
			file_lock_allowed, lock_err))
		{
			json_error(res, 500, lock_err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!file_lock_allowed) {
			json_error(res, 403, "file is locked", req.isKeepAlive());
			return true;
		}
	}
	if (!ensure_local_dir_unlocked_for_request(upload_dir, req, res,
		source_is_dir ? source : parent_path(source),
		source_is_dir ? "source directory is locked" : "source parent directory is locked"))
	{
		return true;
	}

	struct stat target_st;
	if (stat(target.c_str(), &target_st) != 0 || !S_ISDIR(target_st.st_mode)) {
		json_error(res, 404, "target directory not found", req.isKeepAlive());
		return true;
	}
	if (!ensure_local_dir_unlocked_for_request(upload_dir, req, res, target,
		"target directory is locked", "target_local_dir_password"))
	{
		return true;
	}
	if (source_is_dir && is_same_or_child_path(source, target)) {
		json_error(res, 409, "directory cannot be copied into itself",
			req.isKeepAlive());
		return true;
	}

	const std::string name = local_base_name(source);
	if (name.empty()) {
		json_error(res, 400, "invalid source path", req.isKeepAlive());
		return true;
	}
	const std::string dest = join_local_path(target, name.c_str());
	struct stat dest_st;
	if (stat(dest.c_str(), &dest_st) == 0) {
		if (!overwrite) {
			json_error(res, 409, "target already contains a path with same name",
				req.isKeepAlive());
			return true;
		}
		if (dest == source) {
			json_error(res, 409, "source and destination are the same",
				req.isKeepAlive());
			return true;
		}
		if (S_ISDIR(dest_st.st_mode)
			&& !ensure_local_dir_unlocked_for_request(upload_dir, req, res, dest,
				"destination directory is locked", "target_local_dir_password"))
		{
			return true;
		}
		if (S_ISREG(dest_st.st_mode)) {
			bool dest_file_allowed = false;
			std::string lock_err;
			if (!file_lock_path_allows(upload_dir, local_file_lock_key(dest),
				req.getParameter("file_password") ? req.getParameter("file_password") : "",
				dest_file_allowed, lock_err))
			{
				json_error(res, 500, lock_err.c_str(), req.isKeepAlive());
				return true;
			}
			if (!dest_file_allowed) {
				json_error(res, 403, "destination file is locked", req.isKeepAlive());
				return true;
			}
		}
		if (!remove_local_path_recursive(dest, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	}
	else if (errno != ENOENT) {
		json_error(res, 500, strerror(errno), req.isKeepAlive());
		return true;
	}

	if (async) {
		const std::string task_id = start_remote_copy_task(source, dest, dest,
			source_is_dir);
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("task_id", task_id.c_str());
		root.add_text("path", dest.c_str());
		root.add_text("source", source.c_str());
		root.add_text("target", target.c_str());
		root.add_bool("overwritten", overwrite);
		root.add_bool("directory", source_is_dir);
		root.add_text("message", "copy task started");
		return sendJson(res, 200, root, req.isKeepAlive());
	}

	if (!copy_local_path_recursive(source, dest, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", dest.c_str());
	root.add_text("source", source.c_str());
	root.add_text("target", target.c_str());
	root.add_bool("overwritten", overwrite);
	root.add_text("message", source_is_dir ? "directory copied" : "file copied");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskRenameAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string source;
	std::string err;
	const char* source_param = req.getParameter("path");
	if (source_param == NULL || *source_param == '\0') {
		json_error(res, 400, "source path is required", req.isKeepAlive());
		return true;
	}
	if (!normalize_local_path(source_param, source, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	struct stat source_st;
	if (stat(source.c_str(), &source_st) != 0) {
		json_error(res, 404, "source path not found", req.isKeepAlive());
		return true;
	}
	const bool source_is_dir = S_ISDIR(source_st.st_mode);
	const bool source_is_file = S_ISREG(source_st.st_mode);
	if (!source_is_dir && !source_is_file) {
		json_error(res, 400, "only files and directories can be renamed",
			req.isKeepAlive());
		return true;
	}
	if (source == "/") {
		json_error(res, 409, "root directory cannot be renamed", req.isKeepAlive());
		return true;
	}
	if (source_is_dir && is_system_level_directory_path(source)) {
		json_error(res, 409, "system directory cannot be renamed",
			req.isKeepAlive());
		return true;
	}

	std::string new_name = req.getParameter("name") ? req.getParameter("name") : "";
	while (!new_name.empty() && new_name[0] == ' ') {
		new_name.erase(0, 1);
	}
	while (!new_name.empty() && new_name[new_name.size() - 1] == ' ') {
		new_name.erase(new_name.size() - 1);
	}
	if (!validate_local_name_segment(new_name, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	if (source_is_file) {
		bool file_lock_allowed = false;
		std::string lock_err;
		if (!file_lock_path_allows(upload_dir, local_file_lock_key(source),
			req.getParameter("file_password") ? req.getParameter("file_password") : "",
			file_lock_allowed, lock_err))
		{
			json_error(res, 500, lock_err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!file_lock_allowed) {
			json_error(res, 403, "file is locked", req.isKeepAlive());
			return true;
		}
	}
	if (!ensure_local_dir_unlocked_for_request(upload_dir, req, res,
		source_is_dir ? source : parent_path(source),
		source_is_dir ? "directory is locked" : "source parent directory is locked"))
	{
		return true;
	}

	const std::string dest = join_local_path(parent_path(source), new_name.c_str());
	if (dest == source) {
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("path", source.c_str());
		root.add_text("old_path", source.c_str());
		root.add_text("name", new_name.c_str());
		root.add_text("message", "file unchanged");
		return sendJson(res, 200, root, req.isKeepAlive());
	}

	struct stat dest_st;
	if (stat(dest.c_str(), &dest_st) == 0) {
		json_error(res, 409, "target path already exists", req.isKeepAlive());
		return true;
	}
	if (errno != ENOENT) {
		json_error(res, 500, strerror(errno), req.isKeepAlive());
		return true;
	}

	if (::rename(source.c_str(), dest.c_str()) != 0) {
		json_error(res, errno == EXDEV ? 409 : 500,
			errno == EXDEV ? "cannot rename across different file systems" : strerror(errno),
			req.isKeepAlive());
		return true;
	}

	std::string rename_err;
	const std::string old_local_key = std::string("local:") + source;
	const std::string new_local_key = std::string("local:") + dest;
	if (source_is_dir) {
		if (!tag_rename_folder_prefix(upload_dir, old_local_key, new_local_key,
			rename_err)
			|| !video_resume_rename_folder_prefix(upload_dir, old_local_key,
				new_local_key, rename_err)
			|| !file_lock_rename_prefix(upload_dir, local_dir_lock_key(source),
				local_dir_lock_key(dest), rename_err)
			|| !file_lock_rename_prefix(upload_dir, local_file_lock_key(source),
				local_file_lock_key(dest), rename_err))
		{
			(void) ::rename(dest.c_str(), source.c_str());
			json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
			return true;
		}
	} else {
		if (!tag_rename_file(upload_dir, old_local_key, new_local_key, rename_err)
			|| !video_resume_rename_file(upload_dir, old_local_key, new_local_key,
				rename_err)
			|| !file_lock_rename_key(upload_dir,
				local_file_lock_key(source), local_file_lock_key(dest), rename_err))
		{
			(void) ::rename(dest.c_str(), source.c_str());
			json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
			return true;
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", dest.c_str());
	root.add_text("old_path", source.c_str());
	root.add_text("name", new_name.c_str());
	root.add_bool("directory", source_is_dir);
	root.add_text("message", source_is_dir ? "directory renamed" : "file renamed");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskOpenTrashAction::run(request_t& req, response_t& res)
{
	std::string err;
	if (!run_open_command(err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("message", "system Trash opened");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskOpenFileAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	const bool choose_app = req.getParameter("chooser") != NULL
		&& strcmp(req.getParameter("chooser"), "1") == 0;
	std::string path;
	std::string err;
	if (!normalize_local_path(req.getParameter("path"), path, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	struct stat st;
	if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		json_error(res, 404, "file not found", req.isKeepAlive());
		return true;
	}
	if (!ensure_local_dir_unlocked_for_request(upload_dir, req, res,
		parent_path(path), "parent directory is locked"))
	{
		return true;
	}
	bool file_lock_allowed = false;
	if (!file_lock_path_allows(upload_dir, local_file_lock_key(path),
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
	if (!run_open_file_command(path, choose_app, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", path.c_str());
	root.add_text("message", choose_app ? "local player chooser opened" : "file opened");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskImportAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string folder_path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("folder"), folder_path, err, true)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!folder_path.empty() && !upload_directory_exists(upload_dir, folder_path)) {
		json_error(res, 404, "target folder not found", req.isKeepAlive());
		return true;
	}
	if (!make_dir_recursive(upload_dir.c_str())) {
		json_error(res, 500, "cannot access upload dir", req.isKeepAlive());
		return true;
	}

	bool lock_allowed = false;
	std::string locked_path;
	std::string lock_err;
	if (!folder_lock_path_allows(upload_dir, folder_path,
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		lock_allowed, locked_path, lock_err))
	{
		json_error(res, 500, lock_err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!lock_allowed) {
		json_error(res, 403, "folder is locked", req.isKeepAlive());
		return true;
	}

	std::vector<std::string> raw_paths;
	split_local_paths(req.getParameter("paths"), raw_paths);
	if (raw_paths.empty()) {
		json_error(res, 400, "no local files selected", req.isKeepAlive());
		return true;
	}

	std::vector<std::string> sources;
	std::vector<bool> source_is_dir;
	for (size_t i = 0; i < raw_paths.size(); ++i) {
		std::string source;
		if (!normalize_local_path(raw_paths[i].c_str(), source, err)) {
			json_error(res, 400, err.c_str(), req.isKeepAlive());
			return true;
		}

		struct stat st;
		if (stat(source.c_str(), &st) != 0) {
			json_error(res, 400, "local path not found", req.isKeepAlive());
			return true;
		}
		if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) {
			json_error(res, 400, "unsupported local path", req.isKeepAlive());
			return true;
		}
		sources.push_back(source);
		source_is_dir.push_back(S_ISDIR(st.st_mode));
	}

	std::vector<std::string> filtered_sources;
	std::vector<bool> filtered_is_dir;
	for (size_t i = 0; i < sources.size(); ++i) {
		bool covered_by_parent = false;
		for (size_t j = 0; j < sources.size(); ++j) {
			if (i == j || !source_is_dir[j]) {
				continue;
			}
			if (is_same_or_child_path(sources[j], sources[i])
				&& sources[j] != sources[i])
			{
				covered_by_parent = true;
				break;
			}
		}
		if (!covered_by_parent) {
			filtered_sources.push_back(sources[i]);
			filtered_is_dir.push_back(source_is_dir[i]);
		}
	}

	std::vector<std::string> dirs;
	std::vector<local_import_file_t> files;
	std::set<std::string> used_relative_paths;
	for (size_t i = 0; i < filtered_sources.size(); ++i) {
		const std::string source = filtered_sources[i];
		if (filtered_is_dir[i]) {
			std::string remote_dir = unique_upload_directory_relative(
				upload_dir, folder_path, local_base_name(source));
			if (remote_dir.empty()) {
				json_error(res, 500, "cannot create unique target directory name",
					req.isKeepAlive());
				return true;
			}
			if (used_relative_paths.find(remote_dir) != used_relative_paths.end()) {
				const std::string base_name = local_base_name(source);
				for (int n = 1; n < 10000; ++n) {
					const std::string candidate = folder_path.empty()
						? (base_name + "." + std::to_string(n))
						: (folder_path + "/" + base_name + "." + std::to_string(n));
					if (used_relative_paths.find(candidate) == used_relative_paths.end()) {
						remote_dir = candidate;
						break;
					}
				}
			}
			if (!collect_local_import_directory(source, remote_dir, dirs, files, err)) {
				json_error(res, 500, err.c_str(), req.isKeepAlive());
				return true;
			}
			used_relative_paths.insert(remote_dir);
			for (size_t j = 0; j < files.size(); ++j) {
				used_relative_paths.insert(files[j].relative_path);
			}
			continue;
		}

		struct stat st;
		if (stat(source.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
			json_error(res, 400, "local file not found", req.isKeepAlive());
			return true;
		}
		std::string relative_path;
		if (unique_upload_path(upload_dir, folder_path, local_base_name(source),
			relative_path).empty())
		{
			json_error(res, 500, "cannot create unique target file name",
				req.isKeepAlive());
			return true;
		}
		if (used_relative_paths.find(relative_path) != used_relative_paths.end()) {
			const std::string base_name = local_base_name(source);
			for (int n = 1; n < 10000; ++n) {
				const std::string candidate = folder_path.empty()
					? (base_name + "." + std::to_string(n))
					: (folder_path + "/" + base_name + "." + std::to_string(n));
				if (used_relative_paths.find(candidate) == used_relative_paths.end()) {
					relative_path = candidate;
					break;
				}
			}
		}
		local_import_file_t file;
		file.source = source;
		file.name = local_base_name(source);
		file.relative_path = relative_path;
		file.size = regular_file_size(file.source);
		files.push_back(file);
		used_relative_paths.insert(relative_path);
	}

	const std::string task_id = create_local_import_task_id();
	local_import_task_t task;
	task.state = "queued";
	task.message = "等待上传";
	task.total_bytes = 0;
	task.copied_bytes = 0;
	task.total_files = (int) files.size();
	task.saved_count = 0;
	for (size_t i = 0; i < files.size(); ++i) {
		task.total_bytes += files[i].size;
		task.names.push_back(files[i].name);
		task.sizes.push_back(files[i].size);
		task.copied_sizes.push_back(0);
		task.file_states.push_back("pending");
	}
	update_local_import_task(task_id, task);
	std::thread(run_local_import_task, task_id, upload_dir, folder_path, dirs, files).detach();

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", task_id.c_str());
	root.add_number("count", 0);
	root.add_number("total_files", (long long) files.size());
	root.add_number("total_dirs", (long long) dirs.size());
	root.add_number("total_bytes", task.total_bytes);
	root.add_text("folder_path", folder_path.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskImportProgressAction::run(request_t& req, response_t& res)
{
	const char* task_id_param = req.getParameter("task_id");
	std::string task_id = task_id_param ? task_id_param : "";
	if (task_id.empty()) {
		json_error(res, 400, "task_id is required", req.isKeepAlive());
		return true;
	}

	local_import_task_t task;
	{
		std::lock_guard<std::mutex> guard(g_local_import_mutex);
		std::map<std::string, local_import_task_t>::const_iterator it =
			g_local_import_tasks.find(task_id);
		if (it == g_local_import_tasks.end()) {
			json_error(res, 404, "task not found", req.isKeepAlive());
			return true;
		}
		task = it->second;
	}

	const double progress = task.total_bytes > 0
		? ((double) task.copied_bytes * 100.0 / (double) task.total_bytes)
		: (task.state == "done" ? 100.0 : 0.0);

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", task_id.c_str());
	root.add_text("state", task.state.c_str());
	root.add_text("message", task.message.c_str());
	root.add_text("error", task.error.c_str());
	root.add_number("progress", (long long) progress);
	root.add_number("copied_bytes", task.copied_bytes);
	root.add_number("total_bytes", task.total_bytes);
	root.add_number("count", (long long) task.saved_count);
	root.add_number("saved_count", (long long) task.saved_count);
	root.add_number("total_files", (long long) task.total_files);
	acl::json_node& files = json.create_array();
	root.add_child("files", files);
	for (size_t i = 0; i < task.names.size(); ++i) {
		acl::json_node& item = files.add_child(false, true);
		const long long size = i < task.sizes.size() ? task.sizes[i] : 0;
		const long long copied = i < task.copied_sizes.size() ? task.copied_sizes[i] : 0;
		const std::string state = i < task.file_states.size() ? task.file_states[i] : "";
		const std::string remote_path = i < task.remote_paths.size() ? task.remote_paths[i] : "";
		const double file_progress = size > 0
			? ((double) copied * 100.0 / (double) size)
			: (state == "done" ? 100.0 : 0.0);
		item.add_text("name", task.names[i].c_str());
		item.add_text("path", remote_path.c_str());
		item.add_text("remote_path", remote_path.c_str());
		item.add_text("state", state.c_str());
		item.add_bool("saved", state == "done");
		item.add_number("size", size);
		item.add_number("copied", copied);
		item.add_number("progress", (long long) file_progress);
	}
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

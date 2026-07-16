#include "stdafx.h"
#include <atomic>
#include <cerrno>
#include <fstream>
#include "common/webcool_mutex.h"
#include "action/action_util.h"
#include "platform_compat.h"
#include "config.h"

namespace {
#ifdef _WIN32
const size_t k_default_fiber_stack_size = 512 * 1024;
const size_t k_minimum_fiber_stack_size = 256 * 1024;
#else
const size_t k_default_fiber_stack_size = 256000;
const size_t k_minimum_fiber_stack_size = 256000;
#endif
}

size_t                g_stack_size = k_default_fiber_stack_size;
int                   g_rw_timeout = 0;
#ifdef _WIN32
acl::fiber_event_t    g_event_type = acl::FIBER_EVENT_T_POLL;
#else
acl::fiber_event_t    g_event_type = acl::FIBER_EVENT_T_KERNEL;
#endif

char                  g_upload_dir[4096] = "";
char                  g_html_home[4096] = "";
char                  g_sqlite_lib[4096] = "";
char                  g_ffmpeg_path[4096] = "";
char                  g_codeformer_dir[4096] = "";
std::atomic<bool>     g_service_stopping(false);

namespace {

webcool::mutex g_primary_storage_state_mutex;

// primary_storage.path 所在目录：Windows 为 %APPDATA%/webcool 或 %USERPROFILE%/.webcool；
// macOS 为 ~/Library/Application Support/webcool；Linux 为 $HOME/.webcool。
std::string primary_storage_state_dir() {
#ifdef _WIN32
	const char* appdata = getenv("APPDATA");
	if (appdata != nullptr && *appdata != '\0') {
		return join_config_path(appdata, "webcool");
	}
	const char* home = getenv("USERPROFILE");
	if (home != nullptr && *home != '\0') {
		return join_config_path(home, ".webcool");
	}
	return ".webcool";
#elif defined(MACOSX)
	const char* home = getenv("HOME");
	if (home != nullptr && *home != '\0') {
		return join_config_path(
			join_config_path(join_config_path(home, "Library"),
				"Application Support"), "webcool");
	}
	return ".webcool";
#else
	const char* home = getenv("HOME");
	if (home != nullptr && *home != '\0') {
		return join_config_path(home, ".webcool");
	}
	return ".webcool";
#endif
}

std::string primary_storage_state_file() {
	return join_config_path(primary_storage_state_dir(), "primary_storage.path");
}

bool read_primary_storage_path_unlocked(std::string& path, std::string& err) {
	path.clear();
	const std::string state_file = primary_storage_state_file();
	std::ifstream in(state_file.c_str(), std::ios::in);
	if (!in.good()) {
		return true;
	}
	std::getline(in, path);
	if (!in.eof() && !in.good()) {
		err = "cannot read primary storage state";
		logger_error("read primary storage state error=%s, file=%s",
			acl::last_serror(), state_file.c_str());
		return false;
	}
	while (!path.empty()
		&& (path[path.size() - 1] == '\r' || path[path.size() - 1] == '\n'))
	{
		path.erase(path.size() - 1);
	}
	return true;
}

bool write_primary_storage_path_unlocked(const std::string& path, std::string& err) {
	const std::string dir = primary_storage_state_dir();
	if (!action::make_dir_recursive(dir.c_str())) {
		err = "cannot create primary storage state directory:";
		logger_error("create primary storage state dir error=%s, dir=%s, path=%s",
			acl::last_serror(), dir.c_str(), path.c_str());
		return false;
	}
	const std::string state_path = primary_storage_state_file();
	const std::string tmp = state_path + ".tmp";
	std::ofstream out(tmp.c_str(), std::ios::out | std::ios::trunc);
	if (!out.good()) {
		err = "cannot write primary storage state";
		logger_error("write primary storage state error=%s, tmp path=%s",
			acl::last_serror(), tmp.c_str());
		return false;
	}
	out << path << '\n';
	out.close();
	if (!out.good()) {
		err = "cannot flush primary storage state";
		logger_error("flush primary storage state error=%s, tmp path=%s",
			acl::last_serror(), tmp.c_str());
		return false;
	}
	if (rename(tmp.c_str(), state_path.c_str()) != 0) {
		err = strerror(errno);
		logger_error("rename from %s to %s error=%s", tmp.c_str(),
			state_path.c_str(), acl::last_serror());
		return false;
	}

	logger("write primary storage state ok, state file=%s, upload dir=%s",
		state_path.c_str(), path.c_str());
	return true;
}

void apply_default_upload_dir() {
#ifdef MACOSX
	const char* home = getenv("HOME");
	if (home != nullptr && *home != '\0') {
		snprintf(g_upload_dir, sizeof(g_upload_dir),
			"%s/Library/Application Support/webcool/data", home);
	} else {
		snprintf(g_upload_dir, sizeof(g_upload_dir), "%s", "./uploads");
	}
#endif
}

} // namespace

size_t default_fiber_stack_size() {
	return k_default_fiber_stack_size;
}

size_t minimum_fiber_stack_size() {
	return k_minimum_fiber_stack_size;
}

size_t normalize_fiber_stack_size(size_t stack_size) {
	if (stack_size == 0) {
		return default_fiber_stack_size();
	}
	if (stack_size < minimum_fiber_stack_size()) {
		return minimum_fiber_stack_size();
	}
	return stack_size;
}

// 连接读写超时（秒）：0 表示不限制，适用于大文件慢速上传。
// 优先级：webcool.cf io_timeout > 命令行 -r > 0（无超时）
int connection_rw_timeout(int cfg_io_timeout) {
	if (cfg_io_timeout != 0) {
		return cfg_io_timeout;
	}
	if (g_rw_timeout != 0) {
		return g_rw_timeout;
	}
	return 0;
}

bool set_config_text(char* dst, size_t dst_size,
	  const std::string& value, const char* label, std::string& err) {
	if (dst == nullptr || dst_size == 0) {
		err = "invalid config buffer";
		return false;
	}
	if (value.size() >= dst_size) {
		err = label ? label : "config value";
		err += " is too long";
		return false;
	}
#ifdef _WIN32
	std::wstring wide;
	if (!value.empty() && !webcool_utf8_to_wide(value.c_str(), wide)) {
		err = label ? label : "config value";
		err += " is not valid UTF-8";
		return false;
	}
#endif
	ACL_SAFE_STRNCPY(dst, value.c_str(), dst_size);
	return true;
}

std::string join_config_path(const std::string& parent, const char* name) {
	if (parent.empty()) {
		return name ? name : "";
	}
	const char tail = parent[parent.size() - 1];
	if (tail == '/' || tail == '\\') {
		return parent + name;
	}
#ifdef _WIN32
	return parent + "\\" + name;
#else
	return parent + "/" + name;
#endif
}

bool read_primary_storage_path(std::string& path, std::string& err) {
	std::lock_guard<webcool::mutex> guard(g_primary_storage_state_mutex);
	return read_primary_storage_path_unlocked(path, err);
}

bool write_primary_storage_path(const std::string& canonical_path, std::string& err) {
	std::lock_guard<webcool::mutex> guard(g_primary_storage_state_mutex);
	return write_primary_storage_path_unlocked(canonical_path, err);
}

// 按优先级解析 upload_dir 并写入 g_upload_dir：
// 1. primary_storage.path  2. 命令行 -d  3. webcool.cf upload_dir  4. 平台默认值
bool resolve_upload_dir(bool cli_specified, const char* cfg_upload_dir,
	  bool& upload_dir_specified, std::string& err) {

	err.clear();

	std::string primary_path;
	if (!read_primary_storage_path(primary_path, err)) {
		return false;
	}
	if (!primary_path.empty()) {
		if (!set_config_text(g_upload_dir, sizeof(g_upload_dir),
			primary_path, "primary storage path", err)) {
			return false;
		}
		upload_dir_specified = false;
		return true;
	}

	if (cli_specified && g_upload_dir[0] != '\0') {
		upload_dir_specified = true;
		return true;
	}

	if (cfg_upload_dir != nullptr && *cfg_upload_dir != '\0') {
		if (!set_config_text(g_upload_dir, sizeof(g_upload_dir),
			cfg_upload_dir, "file save directory", err)) {
			return false;
		}
		upload_dir_specified = false;
		return true;
	}

	apply_default_upload_dir();
	if (g_upload_dir[0] == '\0') {
		if (!set_config_text(g_upload_dir, sizeof(g_upload_dir),
				"./uploads", "file save directory", err)) {
			return false;
		}
	}
	upload_dir_specified = false;
	return true;
}

bool readable_regular_file(const std::string& path) {
	struct stat st{};
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string normalize_static_home_path(const std::string& path) {
	if (readable_regular_file(join_config_path(path, "main.html"))) {
		return path;
	}
	std::string child = join_config_path(path, "html");
	if (readable_regular_file(join_config_path(child, "main.html"))) {
		return child;
	}
	return path;
}

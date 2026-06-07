#include "action_util.h"

#ifdef _WIN32
#include "../platform_compat.h"
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <errno.h>

#include <cstdlib>
#include <climits>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

namespace action {

namespace {

static const char* kRecycleFolderName = "回收站";
static std::mutex g_runtime_sqlite_mutex;
static std::string g_runtime_sqlite_lib;
static std::mutex g_runtime_ffmpeg_mutex;
static std::string g_runtime_ffmpeg_path;
static std::mutex g_remote_copy_mutex;
static unsigned long g_remote_copy_seq = 0;
static std::map<std::string, remote_copy_task_snapshot_t> g_remote_copy_tasks;

} // namespace

static std::string make_remote_copy_task_id()
{
	std::lock_guard<std::mutex> guard(g_remote_copy_mutex);
	++g_remote_copy_seq;
	return std::string("remote-copy-") + std::to_string((long long) time(NULL))
		+ "-" + std::to_string((long long) getpid())
		+ "-" + std::to_string((long long) g_remote_copy_seq);
}

static void update_remote_copy_task(const remote_copy_task_snapshot_t& task)
{
	std::lock_guard<std::mutex> guard(g_remote_copy_mutex);
	remote_copy_task_snapshot_t merged = task;
	std::map<std::string, remote_copy_task_snapshot_t>::const_iterator it =
		g_remote_copy_tasks.find(task.id);
	if (it != g_remote_copy_tasks.end() && it->second.cancel_requested) {
		merged.cancel_requested = true;
		if (merged.state == "pending" || merged.state == "running") {
			merged.state = "cancelled";
			merged.message = "已取消";
			merged.error.clear();
		}
	}
	g_remote_copy_tasks[task.id] = merged;
}

static bool is_remote_copy_cancel_requested(const std::string& task_id)
{
	std::lock_guard<std::mutex> guard(g_remote_copy_mutex);
	std::map<std::string, remote_copy_task_snapshot_t>::const_iterator it =
		g_remote_copy_tasks.find(task_id);
	return it != g_remote_copy_tasks.end() && it->second.cancel_requested;
}

static bool remove_path_recursive_plain(const std::string& path,
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
	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (!remove_path_recursive_plain(path + "/" + entry->d_name, err)) {
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

static bool measure_copy_bytes(const std::string& path, const std::string& task_id,
	long long& total, std::string& err)
{
	if (is_remote_copy_cancel_requested(task_id)) {
		err = "cancelled";
		return false;
	}
	struct stat st;
	if (lstat(path.c_str(), &st) != 0) {
		err = strerror(errno);
		return false;
	}
	if (S_ISREG(st.st_mode)) {
		long long size = regular_file_size(path);
		total += size > 0 ? size : 0;
		return true;
	}
	if (S_ISLNK(st.st_mode)) {
		return true;
	}
	if (!S_ISDIR(st.st_mode)) {
		return true;
	}
	DIR* dir = opendir(path.c_str());
	if (dir == NULL) {
		err = strerror(errno);
		return false;
	}
	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (!measure_copy_bytes(path + "/" + entry->d_name, task_id, total, err)) {
			closedir(dir);
			return false;
		}
	}
	closedir(dir);
	return true;
}

static bool copy_regular_file_with_remote_progress(const std::string& source,
	const std::string& dest, mode_t mode, remote_copy_task_snapshot_t& task,
	std::string& err)
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
		if (is_remote_copy_cancel_requested(task.id)) {
			task.cancel_requested = true;
			task.state = "cancelled";
			task.message = "已取消";
			update_remote_copy_task(task);
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
			task.message = "拷贝中";
			update_remote_copy_task(task);
			if (is_remote_copy_cancel_requested(task.id)) {
				task.cancel_requested = true;
				task.state = "cancelled";
				task.message = "已取消";
				update_remote_copy_task(task);
				err = "cancelled";
				ok = false;
				break;
			}
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
	if (ok && is_remote_copy_cancel_requested(task.id)) {
		task.cancel_requested = true;
		task.state = "cancelled";
		task.message = "已取消";
		update_remote_copy_task(task);
		err = "cancelled";
		ok = false;
	}
	if (!ok) {
		::unlink(dest.c_str());
		return false;
	}
	(void) chmod(dest.c_str(), mode & 0777);
	return true;
}

static bool copy_path_with_remote_progress(const std::string& source,
	const std::string& dest, remote_copy_task_snapshot_t& task,
	std::string& err)
{
	if (is_remote_copy_cancel_requested(task.id)) {
		task.cancel_requested = true;
		task.state = "cancelled";
		task.message = "已取消";
		update_remote_copy_task(task);
		err = "cancelled";
		return false;
	}
	struct stat st;
	if (lstat(source.c_str(), &st) != 0) {
		err = strerror(errno);
		return false;
	}
	if (S_ISREG(st.st_mode)) {
		return copy_regular_file_with_remote_progress(source, dest, st.st_mode, task, err);
	}
	if (S_ISLNK(st.st_mode)) {
		char target[4096];
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
		return true;
	}
	if (::mkdir(dest.c_str(), st.st_mode & 0777) != 0) {
		err = strerror(errno);
		return false;
	}
	if (is_remote_copy_cancel_requested(task.id)) {
		task.cancel_requested = true;
		task.state = "cancelled";
		task.message = "已取消";
		update_remote_copy_task(task);
		err = "cancelled";
		return false;
	}
	DIR* dir = opendir(source.c_str());
	if (dir == NULL) {
		err = strerror(errno);
		return false;
	}
	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (is_remote_copy_cancel_requested(task.id)) {
			task.cancel_requested = true;
			task.state = "cancelled";
			task.message = "已取消";
			update_remote_copy_task(task);
			err = "cancelled";
			closedir(dir);
			return false;
		}
		const std::string child_source = source + "/" + entry->d_name;
		const std::string child_dest = dest + "/" + entry->d_name;
		if (!copy_path_with_remote_progress(child_source, child_dest, task, err)) {
			closedir(dir);
			return false;
		}
	}
	closedir(dir);
	if (is_remote_copy_cancel_requested(task.id)) {
		task.cancel_requested = true;
		task.state = "cancelled";
		task.message = "已取消";
		update_remote_copy_task(task);
		err = "cancelled";
		return false;
	}
	(void) chmod(dest.c_str(), st.st_mode & 0777);
	return true;
}

static void run_remote_copy_task(std::string task_id, std::string source_full,
	std::string target_full)
{
	remote_copy_task_snapshot_t task;
	if (!remote_copy_task_snapshot(task_id, task)) {
		return;
	}
	if (task.cancel_requested || is_remote_copy_cancel_requested(task.id)) {
		task.state = "cancelled";
		task.message = "已取消";
		task.cancel_requested = true;
		update_remote_copy_task(task);
		return;
	}
	task.state = "running";
	task.message = "准备拷贝";
	update_remote_copy_task(task);

	std::string err;
	long long total = 0;
	if (!measure_copy_bytes(source_full, task.id, total, err)) {
		task.cancel_requested = task.cancel_requested || is_remote_copy_cancel_requested(task.id);
		task.state = task.cancel_requested || err == "cancelled" ? "cancelled" : "failed";
		task.error = task.state == "cancelled" ? "" : err;
		task.message = task.state == "cancelled" ? "已取消" : "拷贝失败";
		update_remote_copy_task(task);
		return;
	}
	task.total_bytes = total;
	update_remote_copy_task(task);
	if (is_remote_copy_cancel_requested(task.id)) {
		task.state = "cancelled";
		task.message = "已取消";
		task.cancel_requested = true;
		update_remote_copy_task(task);
		return;
	}

	if (!copy_path_with_remote_progress(source_full, target_full, task, err)) {
		task.state = task.cancel_requested || is_remote_copy_cancel_requested(task.id)
			? "cancelled"
			: "failed";
		if (task.state == "failed" || task.state == "cancelled") {
			std::string cleanup_err;
			remove_path_recursive_plain(target_full, cleanup_err);
		}
		task.cancel_requested = task.cancel_requested || task.state == "cancelled";
		task.error = task.state == "cancelled" ? "" : err;
		task.message = task.state == "cancelled" ? "已取消" : "拷贝失败";
		update_remote_copy_task(task);
		return;
	}
	if (is_remote_copy_cancel_requested(task.id)) {
		std::string cleanup_err;
		remove_path_recursive_plain(target_full, cleanup_err);
		task.state = "cancelled";
		task.message = "已取消";
		task.error.clear();
		task.cancel_requested = true;
		update_remote_copy_task(task);
		return;
	}

	task.state = "done";
	task.message = "拷贝完成";
	task.copied_bytes = task.total_bytes;
	update_remote_copy_task(task);
}

std::string start_remote_copy_task(const std::string& source_full,
	const std::string& target_full, const std::string& path,
	bool directory)
{
	const std::string task_id = make_remote_copy_task_id();
	remote_copy_task_snapshot_t task;
	task.id = task_id;
	task.state = "pending";
	task.message = "准备拷贝";
	task.error.clear();
	task.source = source_full;
	task.target = target_full;
	task.path = path;
	task.total_bytes = 0;
	task.copied_bytes = 0;
	task.directory = directory;
	task.cancel_requested = false;
	update_remote_copy_task(task);
	std::thread(run_remote_copy_task, task_id, source_full, target_full).detach();
	return task_id;
}

bool remote_copy_task_snapshot(const std::string& task_id,
	remote_copy_task_snapshot_t& snapshot)
{
	std::lock_guard<std::mutex> guard(g_remote_copy_mutex);
	std::map<std::string, remote_copy_task_snapshot_t>::const_iterator it =
		g_remote_copy_tasks.find(task_id);
	if (it == g_remote_copy_tasks.end()) {
		return false;
	}
	snapshot = it->second;
	return true;
}

bool remote_copy_task_cancel(const std::string& task_id,
	remote_copy_task_snapshot_t& snapshot)
{
	std::lock_guard<std::mutex> guard(g_remote_copy_mutex);
	std::map<std::string, remote_copy_task_snapshot_t>::iterator it =
		g_remote_copy_tasks.find(task_id);
	if (it == g_remote_copy_tasks.end()) {
		return false;
	}
	it->second.cancel_requested = true;
	it->second.error.clear();
	if (it->second.state != "done" && it->second.state != "failed") {
		it->second.state = "cancelled";
		it->second.message = "已取消";
	}
	snapshot = it->second;
	return true;
}

void runtime_sqlite_lib_set(const std::string& sqlite_lib_path) {
	std::lock_guard<std::mutex> guard(g_runtime_sqlite_mutex);
	g_runtime_sqlite_lib = sqlite_lib_path;
}

std::string runtime_sqlite_lib_get() {
	std::lock_guard<std::mutex> guard(g_runtime_sqlite_mutex);
	return g_runtime_sqlite_lib;
}

void runtime_ffmpeg_path_set(const std::string& ffmpeg_path) {
	std::lock_guard<std::mutex> guard(g_runtime_ffmpeg_mutex);
	g_runtime_ffmpeg_path = ffmpeg_path;
}

std::string runtime_ffmpeg_path_get() {
	std::lock_guard<std::mutex> guard(g_runtime_ffmpeg_mutex);
	return g_runtime_ffmpeg_path;
}

static bool file_exists_readable(const char* path) {
	if (path == NULL || *path == '\0') {
		return false;
	}
	return access(path, R_OK) == 0;
}

static bool file_exists_executable(const char* path) {
	if (path == NULL || *path == '\0') {
		return false;
	}
	return access(path, X_OK) == 0;
}

std::string choose_sqlite_lib_path() {
	const std::string runtime_path = runtime_sqlite_lib_get();
	if (!runtime_path.empty()) {
		if (file_exists_readable(runtime_path.c_str())) {
			return runtime_path;
		}
		return std::string();
	}

	const char* env_path = getenv("AICOOL_SQLITE_LIB");
	if (env_path && *env_path) {
		if (file_exists_readable(env_path)) {
			return std::string(env_path);
		}
		return std::string();
	}

	const std::vector<std::string> candidates = {
#ifdef _WIN32
		"sqlite.dll",
		"sqlite3.dll",
		"..\\tools\\windows\\sqlite.dll",
		"tools\\windows\\sqlite.dll",
#else
		"/opt/webcool/lib/sqlite3.so",
		"/usr/local/lib/sqlite3.so",
		"../third-party/sqlite/lib/sqlite3.so",
		"third-party/sqlite/lib/sqlite3.so",
#endif
	};

	for (size_t i = 0; i < candidates.size(); ++i) {
		if (file_exists_readable(candidates[i].c_str())) {
			return candidates[i];
		}
	}

	return std::string();
}

std::string choose_ffmpeg_path() {
	const std::string runtime_path = runtime_ffmpeg_path_get();
	if (!runtime_path.empty()) {
		if (file_exists_executable(runtime_path.c_str())) {
			return runtime_path;
		}
		return std::string();
	}

	const char* env_ffmpeg = getenv("AICOOL_FFMPEG");
	if (env_ffmpeg && *env_ffmpeg) {
		if (file_exists_executable(env_ffmpeg)) {
			return std::string(env_ffmpeg);
		}
		return std::string();
	}

	std::vector<std::string> candidates;
#ifdef __APPLE__
	candidates.push_back("/opt/webcool/bin/ffmpeg");
	candidates.push_back("/usr/local/bin/ffmpeg");
	candidates.push_back("../tools/mac/ffmpeg");
	candidates.push_back("tools/mac/ffmpeg");
#elif defined(__linux__)
	candidates.push_back("/opt/webcool/bin/ffmpeg");
	candidates.push_back("/usr/local/bin/ffmpeg");
	candidates.push_back("../tools/linux/ffmpeg");
	candidates.push_back("tools/linux/ffmpeg");
#elif defined(_WIN32)
	candidates.push_back("..\\tools\\windows\\ffmpeg.exe");
	candidates.push_back("tools\\windows\\ffmpeg.exe");
	candidates.push_back("ffmpeg.exe");
#endif

	for (size_t i = 0; i < candidates.size(); ++i) {
		if (file_exists_executable(candidates[i].c_str())) {
			return candidates[i];
		}
	}

	return std::string();
}

bool make_dir(const char* path) {
	struct stat st;
	if (stat(path, &st) == 0) {
		return S_ISDIR(st.st_mode);
	}
	return mkdir(path, 0755) == 0;
}

bool make_dirs(const char* file, int line, const char* path) {
#ifdef _WIN32
	bool ret = webcool_make_dirs_utf8(path, 0755);
#else
	bool ret = acl_make_dirs(path, 0755) == 0;
#endif
#ifdef DEBUG
	printf("%s(%d): path=%s, res=%s\r\n", file, line, path, ret ? "ok" : "error");
#else
	(void) file;
	(void) line;
#endif
	return ret;
}

#ifndef DEBUG
bool make_dir_recursive(const char* path) {
	return make_dirs(__FILE__, __LINE__, path);
}
#endif

bool normalize_relative_path(const char* input, std::string& normalized,
	std::string& err, bool allow_empty)
{
	normalized.clear();
	err.clear();

	std::string text = input ? input : "";
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\\') {
			text[i] = '/';
		}
	}

	while (!text.empty() && text[0] == ' ') {
		text.erase(0, 1);
	}
	while (!text.empty() && text[text.size() - 1] == ' ') {
		text.erase(text.size() - 1);
	}

	if (text.empty()) {
		if (allow_empty) {
			return true;
		}
		err = "path is empty";
		return false;
	}
	if (text[0] == '/') {
		err = "absolute path is not allowed";
		return false;
	}

	std::string segment;
	for (size_t i = 0; i <= text.size(); ++i) {
		const bool at_end = i == text.size();
		const char ch = at_end ? '/' : text[i];
		if (ch != '/') {
			unsigned char c = (unsigned char) ch;
			if (c < 32 || c == 127) {
				err = "path contains control character";
				return false;
			}
			segment.push_back(ch);
			continue;
		}

		if (segment.empty()) {
			err = "path contains empty segment";
			return false;
		}
		if (segment == "." || segment == "..") {
			err = "path contains invalid segment";
			return false;
		}
		if (!normalized.empty()) {
			normalized.push_back('/');
		}
		normalized.append(segment);
		segment.clear();
	}

	return true;
}

std::string join_upload_path(const std::string& upload_dir,
	const std::string& relative_path)
{
	if (relative_path.empty()) {
		return upload_dir;
	}
	return upload_dir + "/" + relative_path;
}

std::string parent_relative_path(const std::string& relative_path) {
	if (relative_path.empty()) {
		return std::string();
	}
	std::string::size_type pos = relative_path.rfind('/');
	if (pos == std::string::npos) {
		return std::string();
	}
	return relative_path.substr(0, pos);
}

std::string base_name_from_relative_path(const std::string& relative_path) {
	if (relative_path.empty()) {
		return std::string();
	}
	std::string::size_type pos = relative_path.rfind('/');
	if (pos == std::string::npos) {
		return relative_path;
	}
	return relative_path.substr(pos + 1);
}

bool upload_regular_file_exists(const std::string& upload_dir,
	const std::string& relative_path)
{
	if (relative_path.empty()) {
		return false;
	}
	struct stat st;
	std::string full = join_upload_path(upload_dir, relative_path);
	return stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool upload_directory_exists(const std::string& upload_dir,
	const std::string& relative_path)
{
	struct stat st;
	std::string full = join_upload_path(upload_dir, relative_path);
	return stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

long long regular_file_size(const std::string& full_path)
{
#ifdef _WIN32
	std::wstring wpath;
	if (!webcool_utf8_to_wide(full_path.c_str(), wpath)) {
		return -1;
	}
	WIN32_FILE_ATTRIBUTE_DATA data;
	if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &data)) {
		return -1;
	}
	if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
		return -1;
	}
	ULARGE_INTEGER size;
	size.HighPart = data.nFileSizeHigh;
	size.LowPart = data.nFileSizeLow;
	if (size.QuadPart > (ULONGLONG) LLONG_MAX) {
		return -1;
	}
	return (long long) size.QuadPart;
#else
	struct stat st;
	if (stat(full_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		return -1;
	}
	return (long long) st.st_size;
#endif
}

#ifdef _WIN32
static std::string utf8_bytes_as_ansi_mojibake(const std::string& text)
{
	if (text.empty()) {
		return std::string();
	}
	const int wide_len = MultiByteToWideChar(CP_ACP, 0, text.c_str(),
		(int) text.size(), NULL, 0);
	if (wide_len <= 0) {
		return std::string();
	}
	std::vector<wchar_t> wide((size_t) wide_len + 1);
	if (MultiByteToWideChar(CP_ACP, 0, text.c_str(), (int) text.size(),
		&wide[0], wide_len) <= 0) {
		return std::string();
	}
	wide[(size_t) wide_len] = L'\0';
	std::string out;
	if (!webcool_wide_to_utf8(&wide[0], out)) {
		return std::string();
	}
	return out;
}
#endif

bool resolve_upload_regular_file_path(const std::string& upload_dir,
	const std::string& requested_relative_path, std::string& resolved_relative_path)
{
	resolved_relative_path = requested_relative_path;
	if (upload_regular_file_exists(upload_dir, requested_relative_path)) {
		return true;
	}

	const std::string parent = parent_relative_path(requested_relative_path);
	const std::string requested_base = base_name_from_relative_path(requested_relative_path);
	if (requested_base.empty()) {
		return false;
	}

	const std::string dir_path = join_upload_path(upload_dir, parent);
	DIR* dir = opendir(dir_path.c_str());
	if (dir == NULL) {
		return false;
	}

	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		const char* name = entry->d_name;
		if (name == NULL || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
			continue;
		}
		const std::string candidate = parent.empty()
			? std::string(name) : (parent + "/" + name);
		if (!upload_regular_file_exists(upload_dir, candidate)) {
			continue;
		}
		if (requested_base == name) {
			resolved_relative_path = candidate;
			closedir(dir);
			return true;
		}
#ifdef _WIN32
		if (requested_base == utf8_bytes_as_ansi_mojibake(name)) {
			resolved_relative_path = candidate;
			closedir(dir);
			return true;
		}
#endif
	}

	closedir(dir);
	return false;
}

const char* recycle_folder_name() {
	return kRecycleFolderName;
}

bool is_recycle_root_path(const std::string& relative_path) {
	return relative_path == recycle_folder_name();
}

bool is_recycle_file_path(const std::string& relative_path) {
	if (is_recycle_root_path(relative_path)) {
		return true;
	}
	const std::string prefix = std::string(recycle_folder_name()) + "/";
	return relative_path.size() > prefix.size()
		&& relative_path.compare(0, prefix.size(), prefix) == 0;
}

bool sendData(response_t& res, const acl::string& data,
	  const acl::string& type, bool keep_alive) {
	res.setContentType(type);
	res.setHeader("Cache-Control", "no-store, no-cache, must-revalidate");
	res.setHeader("Pragma", "no-cache");
	res.setHeader("Expires", "0");
	res.setKeepAlive(keep_alive);
	res.setContentLength(data.size());
	return res.write(data) && res.write(NULL, 0);
}

bool sendHtml(response_t& res, const acl::string& html, bool keep_alive) {
	return sendData(res, html, "text/html; charset=utf-8", keep_alive);
}

bool sendText(response_t& res, int status, const char* text, bool keep_alive) {
	res.setStatus(status);
	res.setContentType("text/plain; charset=utf-8");
	res.setKeepAlive(keep_alive);
	res.setContentLength((long long) strlen(text));
	return res.write(text, strlen(text)) && res.write(NULL, 0);
}

bool sendJson(response_t& res, int status,
	const acl::json_node& json, bool keep_alive)
{
	const acl::string& text = json.to_string();
	return sendJson(res, status, text, keep_alive);
}

bool sendJson(response_t& res, int status,
	const acl::string& json, bool keep_alive)
{
	res.setStatus(status);
	res.setContentType("application/json; charset=utf-8");
	res.setKeepAlive(keep_alive);
	res.setContentLength((long long) json.size());
	return res.write(json) && res.write(NULL, 0);
}

} // namespace action

#include "stdafx.h"
#include "action_util.h"
#include "actions.h"

#ifdef _WIN32
#include "../platform_compat.h"
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <cerrno>

#include <map>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

namespace action {

namespace {

const auto kRecycleFolderName = "回收站";
const auto kSharedFolderName = "共享目录";
const char* const kSharedFixedSubfolderNames[] = {
	"图片",
	"文档",
	"视频",
	"音频",
};
std::mutex g_runtime_sqlite_mutex;
std::string g_runtime_sqlite_lib;
std::mutex g_runtime_ffmpeg_mutex;
std::string g_runtime_ffmpeg_path;
std::mutex g_remote_copy_mutex;
unsigned long g_remote_copy_seq = 0;
std::map<std::string, remote_copy_task_snapshot_t> g_remote_copy_tasks;

} // namespace

static std::string make_remote_copy_task_id()
{
	std::lock_guard<std::mutex> guard(g_remote_copy_mutex);
	++g_remote_copy_seq;
	return std::string("remote-copy-")
		+ std::to_string(static_cast<long long>(time(nullptr)))
		+ "-" + std::to_string(static_cast<long long>(getpid()))
		+ "-" + std::to_string(static_cast<long long>(g_remote_copy_seq));
}

static void update_remote_copy_task(const remote_copy_task_snapshot_t& task)
{
	std::lock_guard<std::mutex> guard(g_remote_copy_mutex);
	remote_copy_task_snapshot_t merged = task;
	const auto it = g_remote_copy_tasks.find(task.id);
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
	const auto it = g_remote_copy_tasks.find(task_id);
	return it != g_remote_copy_tasks.end() && it->second.cancel_requested;
}

static bool remove_path_recursive_plain(const std::string& path,
	std::string& err)
{
	struct stat st{};
	if (lstat(path.c_str(), &st) != 0) {
		if (errno == ENOENT) {
			return true;
		}
		err = strerror(errno);
		return false;
	}
	if (!S_ISDIR(st.st_mode)) {
		if (unlink(path.c_str()) != 0) {
			err = strerror(errno);
			return false;
		}
		return true;
	}
	DIR* dir = opendir(path.c_str());
	if (dir == nullptr) {
		err = strerror(errno);
		return false;
	}
	const dirent* entry = nullptr;
	while ((entry = readdir(dir)) != nullptr) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (!remove_path_recursive_plain(path + "/" + entry->d_name, err)) {
			closedir(dir);
			return false;
		}
	}
	closedir(dir);
	if (rmdir(path.c_str()) != 0) {
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
	struct stat st{};
	if (lstat(path.c_str(), &st) != 0) {
		err = strerror(errno);
		return false;
	}
	if (S_ISREG(st.st_mode)) {
		const long long size = regular_file_size(path);
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
	if (dir == nullptr) {
		err = strerror(errno);
		return false;
	}
	const dirent* entry = nullptr;
	while ((entry = readdir(dir)) != nullptr) {
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
	const std::string& dest, const mode_t mode, remote_copy_task_snapshot_t& task,
	std::string& err)
{
	FILE* in = fopen(source.c_str(), "rb");
	if (in == nullptr) {
		err = strerror(errno);
		return false;
	}
	FILE* out = fopen(dest.c_str(), "wb");
	if (out == nullptr) {
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
			task.copied_bytes += static_cast<long long>(n);
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
		unlink(dest.c_str());
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
	struct stat st{};
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
		if (symlink(target, dest.c_str()) != 0) {
			err = strerror(errno);
			return false;
		}
		return true;
	}
	if (!S_ISDIR(st.st_mode)) {
		return true;
	}
	if (mkdir(dest.c_str(), st.st_mode & 0777) != 0) {
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
	if (dir == nullptr) {
		err = strerror(errno);
		return false;
	}
	const dirent* entry = nullptr;
	while ((entry = readdir(dir)) != nullptr) {
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

static void run_remote_copy_task(const std::string& task_id, const std::string& source_full,
	const std::string& target_full, const std::string& upload_dir)
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
	if (!upload_dir.empty()) {
		std::string sync_err;
		std::vector<std::string> sync_paths;
		const std::vector<std::string> delete_paths;
		if (!task.path.empty()) {
			sync_paths.push_back(task.path);
		}
		(void) storage_backup_sync_paths(upload_dir, sync_paths, delete_paths, sync_err);
	}
}

std::string start_remote_copy_task(const std::string& source_full,
	const std::string& target_full, const std::string& path,
	const bool directory, const std::string& upload_dir)
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
	std::thread(run_remote_copy_task, task_id, source_full, target_full,
		upload_dir).detach();
	return task_id;
}

bool remote_copy_task_snapshot(const std::string& task_id,
	remote_copy_task_snapshot_t& snapshot)
{
	std::lock_guard<std::mutex> guard(g_remote_copy_mutex);
	const std::map<std::string, remote_copy_task_snapshot_t>::const_iterator it =
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
	const auto it = g_remote_copy_tasks.find(task_id);
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
	if (path == nullptr || *path == '\0') {
		return false;
	}
	return access(path, R_OK) == 0;
}

static bool file_exists_executable(const char* path) {
	if (path == nullptr || *path == '\0') {
		return false;
	}
	return access(path, X_OK) == 0;
}

std::string choose_sqlite_lib_path() {
	std::string runtime_path = runtime_sqlite_lib_get();
	if (!runtime_path.empty()) {
		if (file_exists_readable(runtime_path.c_str())) {
			return runtime_path;
		}
		return "";
	}

	const char* env_path = getenv("AICOOL_SQLITE_LIB");
	if (env_path && *env_path) {
		if (file_exists_readable(env_path)) {
			return {env_path};
		}
		return {};
	}

	const std::vector<std::string> candidates = {
#ifdef _WIN32
		"sqlite.dll",
		"sqlite3.dll",
		"..\\tools\\windows\\sqlite.dll",
		"tools\\windows\\sqlite.dll",
#else
		"/opt/soft/webcool/lib/sqlite3.so",
		"/usr/local/lib/sqlite3.so",
		"../third-party/sqlite/lib/sqlite3.so",
		"third-party/sqlite/lib/sqlite3.so",
#endif
	};

	for (const auto & candidate : candidates) {
		if (file_exists_readable(candidate.c_str())) {
			return candidate;
		}
	}

	return {};
}

std::string choose_ffmpeg_path() {
	std::string runtime_path = runtime_ffmpeg_path_get();
	if (!runtime_path.empty()) {
		if (file_exists_executable(runtime_path.c_str())) {
			return runtime_path;
		}
		return "";
	}

	const char* env_ffmpeg = getenv("AICOOL_FFMPEG");
	if (env_ffmpeg && *env_ffmpeg) {
		if (file_exists_executable(env_ffmpeg)) {
			return {env_ffmpeg};
		}
		return {};
	}

	std::vector<std::string> candidates;
#ifdef __APPLE__
	candidates.emplace_back("/opt/soft/webcool/bin/ffmpeg");
	candidates.emplace_back("/usr/local/bin/ffmpeg");
	candidates.emplace_back("../tools/mac/ffmpeg");
	candidates.emplace_back("tools/mac/ffmpeg");
#elif defined(__linux__)
	candidates.push_back("/opt/soft/webcool/bin/ffmpeg");
	candidates.push_back("/usr/local/bin/ffmpeg");
	candidates.push_back("../tools/linux/ffmpeg");
	candidates.push_back("tools/linux/ffmpeg");
#elif defined(_WIN32)
	candidates.push_back("..\\tools\\windows\\ffmpeg.exe");
	candidates.push_back("tools\\windows\\ffmpeg.exe");
	candidates.push_back("ffmpeg.exe");
#endif

	for (const auto & candidate : candidates) {
		if (file_exists_executable(candidate.c_str())) {
			return candidate;
		}
	}

	return {};
}

bool make_dir(const char* path) {
	struct stat st{};
	if (stat(path, &st) == 0) {
		return S_ISDIR(st.st_mode);
	}
	return mkdir(path, 0755) == 0;
}

bool make_dirs(const char* file, const int line, const char* path) {
#ifdef _WIN32
	bool ret = webcool_make_dirs_utf8(path, 0755);
#else
	const bool ret = acl_make_dirs(path, 0755) == 0;
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
	std::string& err, const bool allow_empty)
{
	normalized.clear();
	err.clear();

	std::string text = input ? input : "";
	for (char & ch : text) {
		if (ch == '\\') {
			ch = '/';
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
			const auto c = static_cast<unsigned char>(ch);
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
	if (is_shared_file_path(relative_path)) {
		const std::string base = runtime_upload_dir_get();
		if (!base.empty()) {
			const std::string prefix = shared_folder_name();
			if (relative_path == prefix) {
				return base + "/" + prefix;
			}
			return base + "/" + prefix + relative_path.substr(prefix.size());
		}
	}
	return upload_dir + "/" + relative_path;
}

std::string parent_relative_path(const std::string& relative_path) {
	if (relative_path.empty()) {
		return "";
	}
	const std::string::size_type pos = relative_path.rfind('/');
	if (pos == std::string::npos) {
		return "";
	}
	return relative_path.substr(0, pos);
}

std::string base_name_from_relative_path(const std::string& relative_path) {
	if (relative_path.empty()) {
		return "";
	}
	const std::string::size_type pos = relative_path.rfind('/');
	if (pos == std::string::npos) {
		return relative_path;
	}
	return relative_path.substr(pos + 1);
}

const char* shared_folder_name() {
	return kSharedFolderName;
}

const std::vector<std::string>& shared_fixed_subfolder_names() {
	static const std::vector<std::string> names(
		kSharedFixedSubfolderNames,
		kSharedFixedSubfolderNames
			+ sizeof(kSharedFixedSubfolderNames) / sizeof(kSharedFixedSubfolderNames[0]));
	return names;
}

bool is_shared_root_path(const std::string& relative_path) {
	return relative_path == shared_folder_name();
}

bool is_shared_file_path(const std::string& relative_path) {
	if (is_shared_root_path(relative_path)) {
		return true;
	}
	const std::string prefix = std::string(shared_folder_name()) + "/";
	return relative_path.size() > prefix.size()
		&& relative_path.compare(0, prefix.size(), prefix) == 0;
}

bool is_shared_fixed_subfolder_path(const std::string& relative_path) {
	if (relative_path.empty()) {
		return false;
	}
	const std::string prefix = std::string(shared_folder_name()) + "/";
	if (relative_path.size() <= prefix.size()
		|| relative_path.compare(0, prefix.size(), prefix) != 0)
	{
		return false;
	}
	const std::string child_name = relative_path.substr(prefix.size());
	if (child_name.find('/') != std::string::npos) {
		return false;
	}
	const std::vector<std::string>& names = shared_fixed_subfolder_names();
	for (std::vector<std::string>::const_iterator it = names.begin();
		it != names.end(); ++it)
	{
		if (child_name == *it) {
			return true;
		}
	}
	return false;
}

bool is_root_fixed_folder_path(const std::string& relative_path) {
	if (relative_path.empty() || relative_path.find('/') != std::string::npos) {
		return false;
	}
	const std::vector<std::string>& names = shared_fixed_subfolder_names();
	for (std::vector<std::string>::const_iterator it = names.begin();
		it != names.end(); ++it)
	{
		if (relative_path == *it) {
			return true;
		}
	}
	return false;
}

bool ensure_shared_upload_dir(std::string& err) {
	err.clear();
	const std::string base = runtime_upload_dir_get();
	if (base.empty()) {
		err = "runtime upload directory is not initialized";
		return false;
	}
	const std::vector<std::string>& names = shared_fixed_subfolder_names();
	for (std::vector<std::string>::const_iterator it = names.begin();
		it != names.end(); ++it)
	{
		const std::string root_child_path = base + "/" + *it;
		if (!make_dir_recursive(root_child_path.c_str())) {
			err = "cannot create root fixed folder";
			return false;
		}
	}
	const std::string path = base + "/" + shared_folder_name();
	if (!make_dir_recursive(path.c_str())) {
		err = "cannot create shared folder";
		return false;
	}
	for (std::vector<std::string>::const_iterator it = names.begin();
		it != names.end(); ++it)
	{
		const std::string child_path = path + "/" + *it;
		if (!make_dir_recursive(child_path.c_str())) {
			err = "cannot create shared fixed folder";
			return false;
		}
	}
	return true;
}

bool upload_regular_file_exists(const std::string& upload_dir,
	const std::string& relative_path)
{
	if (relative_path.empty()) {
		return false;
	}
	struct stat st{};
	const std::string full = join_upload_path(upload_dir, relative_path);
	return stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool upload_directory_exists(const std::string& upload_dir,
	const std::string& relative_path)
{
	struct stat st{};
	const std::string full = join_upload_path(upload_dir, relative_path);
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
	struct stat st{};
	if (stat(full_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		return -1;
	}
	return st.st_size;
#endif
}

#ifdef _WIN32
static std::string utf8_bytes_as_ansi_mojibake(const std::string& text)
{
	if (text.empty()) {
		return std::string();
	}
	const int wide_len = MultiByteToWideChar(CP_ACP, 0, text.c_str(),
		(int) text.size(), nullptr, 0);
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

namespace {

bool is_protected_root_basename(const char* name)
{
	if (name == NULL || *name == '\0') {
		return false;
	}
	if (strcmp(name, ".folder_locks.txt") == 0
		|| strcmp(name, ".file_locks.txt") == 0
		|| strcmp(name, ".office_preview_cache") == 0)
	{
		return true;
	}
	static const char* db_names[] = {
		".video_resume.db",
		".tag_catalog.db",
		".recycle_bin.db",
		".folder_catalog.db",
	};
	for (size_t i = 0; i < sizeof(db_names) / sizeof(db_names[0]); ++i) {
		const char* db_name = db_names[i];
		if (strcmp(name, db_name) == 0) {
			return true;
		}
		const size_t db_len = strlen(db_name);
		if (strncmp(name, db_name, db_len) != 0) {
			continue;
		}
		const char* suffix = name + db_len;
		if (strcmp(suffix, "-wal") == 0
			|| strcmp(suffix, "-shm") == 0
			|| strcmp(suffix, "-journal") == 0)
		{
			return true;
		}
	}
	return false;
}

} // namespace

bool is_protected_virtual_path(const std::string& relative_path)
{
	if (relative_path.empty()) {
		return false;
	}
	if (relative_path == ".office_preview_cache"
		|| relative_path.find(".office_preview_cache/") == 0)
	{
		return true;
	}
	if (!parent_relative_path(relative_path).empty()) {
		return false;
	}
	return is_protected_root_basename(
		base_name_from_relative_path(relative_path).c_str());
}

bool resolve_upload_regular_file_path(const std::string& upload_dir,
	const std::string& requested_relative_path, std::string& resolved_relative_path)
{
	if (is_protected_virtual_path(requested_relative_path)) {
		return false;
	}
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
	if (dir == nullptr) {
		return false;
	}

	const dirent* entry = nullptr;
	while ((entry = readdir(dir)) != nullptr) {
		const char* name = entry->d_name;
		if (name == nullptr || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
			continue;
		}
		const std::string candidate = parent.empty()
			? std::string(name) : parent + "/" + name;
		if (is_protected_virtual_path(candidate)) {
			continue;
		}
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
	  const acl::string& type, const bool keep_alive) {
	res.setContentType(type);
	res.setHeader("Cache-Control", "no-store, no-cache, must-revalidate");
	res.setHeader("Pragma", "no-cache");
	res.setHeader("Expires", "0");
	res.setKeepAlive(keep_alive);
	if (data.size() > 10240) {
		res.setChunkedTransferEncoding(true);
		res.setContentEncoding(true);
	} else {
		res.setContentLength(static_cast<long long>(data.size()));
	}
	return res.write(data) && res.write(nullptr, 0);
}

bool sendHtml(response_t& res, const acl::string& html, const bool keep_alive) {
	return sendData(res, html, "text/html; charset=utf-8", keep_alive);
}

bool sendText(response_t& res, const int status, const char* text,
	  const bool keep_alive) {
	res.setStatus(status);
	res.setContentType("text/plain; charset=utf-8");
	res.setKeepAlive(keep_alive);
	res.setContentLength(static_cast<long long>(strlen(text)));
	return res.write(text, strlen(text)) && res.write(nullptr, 0);
}

bool sendJson(response_t& res, const int status,
	const acl::json_node& json, const bool keep_alive)
{
	const acl::string& text = json.to_string();
	return sendJson(res, status, text, keep_alive);
}

bool sendJson(response_t& res, const int status,
	const acl::string& json, const bool keep_alive)
{
	res.setStatus(status);
	res.setContentType("application/json; charset=utf-8");
	res.setKeepAlive(keep_alive);
	res.setContentLength(static_cast<long long>(json.size()));
	return res.write(json) && res.write(nullptr, 0);
}

int safe_atoi(const char* s, const int def) {
	char* end = nullptr;
	const int n = static_cast<int>(std::strtol(s, &end, 10));
	if (end && *end != 0) {
		return def;
	}
	return n;
}

long safe_atol(const char* s, const long def) {
	char* end = nullptr;
	const long n = std::strtol(s, &end, 10);
	if (end && *end != 0) {
		return def;
	}
	return n;
}

long long safe_atoll(const char* s, const long long def) {
	char* end = nullptr;
	const long long n = std::strtoll(s, &end, 10);
	if (end && *end != 0) {
		return def;
	}
	return n;
}

void json_error(response_t& res, int status, const char* msg,
	bool keep_alive)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", msg ? msg : "unknown error");
	sendJson(res, status, root, keep_alive);
}

} // namespace action

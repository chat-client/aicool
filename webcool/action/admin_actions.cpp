#include "actions.h"
#include "action_util.h"

#ifdef _WIN32
#include "../platform_compat.h"
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace action {
namespace {

std::mutex g_runtime_upload_mutex;
std::string g_runtime_upload_dir;

struct storage_move_item_t {
	std::string source;
	std::string target;
	bool directory;
	long long size;
};

struct storage_migrate_task_t {
	std::string id;
	std::string state;
	std::string message;
	std::string error;
	std::string source_dir;
	std::string target_dir;
	std::string conflict_source;
	std::string conflict_target;
	std::string conflict_name;
	std::string conflict_resolution;
	std::string conflict_default;
	bool cleanup_done;
	bool pause_requested;
	bool cancel_requested;
	long long total_bytes;
	long long moved_bytes;
	int total_files;
	int moved_files;
};

std::mutex g_storage_migrate_mutex;
storage_migrate_task_t g_storage_migrate_task;
unsigned long long g_storage_migrate_seq = 0;

struct webcool_settings_t {
	bool local_disk_admin;
	bool local_disk_user;
};

std::mutex g_settings_mutex;

static webcool_settings_t default_settings()
{
	webcool_settings_t settings;
	settings.local_disk_admin = true;
	settings.local_disk_user = true;
	return settings;
}

static void json_error(response_t& res, int status, const char* msg,
	bool keep_alive)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", msg ? msg : "unknown error");
	sendJson(res, status, root, keep_alive);
}

static std::string make_task_id()
{
	std::lock_guard<std::mutex> guard(g_storage_migrate_mutex);
	++g_storage_migrate_seq;
	return std::string("storage-migrate-") + std::to_string((long long) time(NULL))
		+ "-" + std::to_string((long long) getpid())
		+ "-" + std::to_string((long long) g_storage_migrate_seq);
}

static void update_task(const storage_migrate_task_t& task)
{
	std::lock_guard<std::mutex> guard(g_storage_migrate_mutex);
	storage_migrate_task_t merged = task;
	if (g_storage_migrate_task.id == task.id
		&& task.state != "done"
		&& task.state != "failed"
		&& task.state != "cancelled")
	{
		merged.pause_requested = task.pause_requested
			|| g_storage_migrate_task.pause_requested;
		merged.cancel_requested = task.cancel_requested
			|| g_storage_migrate_task.cancel_requested;
		if (merged.conflict_resolution.empty()) {
			merged.conflict_resolution = g_storage_migrate_task.conflict_resolution;
		}
		if (merged.conflict_default.empty()) {
			merged.conflict_default = g_storage_migrate_task.conflict_default;
		}
	}
	g_storage_migrate_task = merged;
}

static storage_migrate_task_t current_storage_task_snapshot()
{
	std::lock_guard<std::mutex> guard(g_storage_migrate_mutex);
	return g_storage_migrate_task;
}

static bool storage_task_wait_if_paused_or_cancelled(storage_migrate_task_t& task)
{
	while (true) {
		storage_migrate_task_t snapshot = current_storage_task_snapshot();
		if (snapshot.id != task.id) {
			return false;
		}
		task.pause_requested = snapshot.pause_requested;
		task.cancel_requested = snapshot.cancel_requested;
		if (task.cancel_requested) {
			task.state = "cancelled";
			task.message = "迁移已取消";
			update_task(task);
			return false;
		}
		if (!task.pause_requested) {
			if (task.state == "paused") {
				task.state = "running";
				task.message = "继续迁移";
				update_task(task);
			}
			return true;
		}
		task.state = "paused";
		task.message = "迁移已暂停";
		update_task(task);
		//usleep(200 * 1000);
		acl_doze(200);
	}
}

static bool canonical_existing_path(const std::string& input,
	std::string& out, std::string& err)
{
	char resolved[PATH_MAX];
	if (realpath(input.c_str(), resolved) == NULL) {
		err = strerror(errno);
		return false;
	}
	out = resolved;
	return true;
}

static bool is_absolute_storage_path(const std::string& path)
{
#ifdef _WIN32
	return (path.size() >= 3
			&& ((path[0] >= 'A' && path[0] <= 'Z')
				|| (path[0] >= 'a' && path[0] <= 'z'))
			&& path[1] == ':'
			&& (path[2] == '/' || path[2] == '\\'))
		|| (path.size() >= 2
			&& (path[0] == '/' || path[0] == '\\')
			&& (path[1] == '/' || path[1] == '\\'));
#else
	return !path.empty() && path[0] == '/';
#endif
}

static bool ensure_storage_target_path(const std::string& input,
	std::string& out, std::string& err)
{
	if (!is_absolute_storage_path(input)) {
		err = "target path must be absolute";
		return false;
	}
	if (!make_dir_recursive(input.c_str())) {
		err = "cannot create target storage path";
		return false;
	}
	return canonical_existing_path(input, out, err);
}

static bool is_same_or_child_path(const std::string& base,
	const std::string& candidate)
{
#ifdef _WIN32
	std::string left = base;
	std::string right = candidate;
	for (size_t i = 0; i < left.size(); ++i) {
		if (left[i] == '\\') {
			left[i] = '/';
		} else if (left[i] >= 'A' && left[i] <= 'Z') {
			left[i] = (char) (left[i] - 'A' + 'a');
		}
	}
	for (size_t i = 0; i < right.size(); ++i) {
		if (right[i] == '\\') {
			right[i] = '/';
		} else if (right[i] >= 'A' && right[i] <= 'Z') {
			right[i] = (char) (right[i] - 'A' + 'a');
		}
	}
	while (left.size() > 3 && left[left.size() - 1] == '/') {
		left.erase(left.size() - 1);
	}
	while (right.size() > 3 && right[right.size() - 1] == '/') {
		right.erase(right.size() - 1);
	}
	if (left == right) {
		return true;
	}
	return right.size() > left.size()
		&& right.compare(0, left.size(), left) == 0
		&& right[left.size()] == '/';
#else
	if (base == candidate) {
		return true;
	}
	if (base == "/") {
		return !candidate.empty() && candidate[0] == '/';
	}
	return candidate.size() > base.size()
		&& candidate.compare(0, base.size(), base) == 0
		&& candidate[base.size()] == '/';
#endif
}

static bool init_storage_databases(const std::string& path, std::string& err)
{
	return init_video_resume_db(path, err)
		&& init_tag_db(path, err)
		&& init_recycle_bin_db(path, err)
		&& init_category_folder_db(path, err);
}

static std::string settings_dir(const std::string& upload_dir)
{
	return join_upload_path(upload_dir, ".webcool_settings");
}

static std::string settings_file(const std::string& upload_dir)
{
	return settings_dir(upload_dir) + "/settings.db";
}

static bool parse_bool_text(const std::string& text, bool fallback)
{
	if (text == "1" || text == "true" || text == "yes" || text == "on") {
		return true;
	}
	if (text == "0" || text == "false" || text == "no" || text == "off") {
		return false;
	}
	return fallback;
}

static bool request_bool_param(request_t& req, const char* name, bool fallback)
{
	const char* value = req.getParameter(name);
	return value ? parse_bool_text(value, fallback) : fallback;
}

static bool load_settings_unlocked(const std::string& upload_dir,
	webcool_settings_t& settings, std::string& err)
{
	settings = default_settings();
	std::ifstream in(settings_file(upload_dir).c_str(), std::ios::in);
	if (!in.good()) {
		return true;
	}
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty()) {
			continue;
		}
		const std::string::size_type pos = line.find('=');
		if (pos == std::string::npos) {
			err = "invalid settings database";
			return false;
		}
		const std::string key = line.substr(0, pos);
		const std::string value = line.substr(pos + 1);
		if (key == "local_disk_admin") {
			settings.local_disk_admin = parse_bool_text(value, settings.local_disk_admin);
		} else if (key == "local_disk_user") {
			settings.local_disk_user = parse_bool_text(value, settings.local_disk_user);
		}
	}
	return true;
}

static bool save_settings_unlocked(const std::string& upload_dir,
	const webcool_settings_t& settings, std::string& err)
{
	const std::string dir = settings_dir(upload_dir);
	if (!make_dir_recursive(dir.c_str())) {
		err = "cannot create settings directory";
		return false;
	}
	const std::string path = settings_file(upload_dir);
	const std::string tmp = path + ".tmp";
	std::ofstream out(tmp.c_str(), std::ios::out | std::ios::trunc);
	if (!out.good()) {
		err = "cannot write settings database";
		return false;
	}
	out << "local_disk_admin=" << (settings.local_disk_admin ? "1" : "0") << '\n';
	out << "local_disk_user=" << (settings.local_disk_user ? "1" : "0") << '\n';
	out.close();
	if (!out.good()) {
		err = "cannot flush settings database";
		return false;
	}
	if (rename(tmp.c_str(), path.c_str()) != 0) {
		err = strerror(errno);
		return false;
	}
	return true;
}

static std::string join_storage_path(const std::string& parent,
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

static std::string storage_backup_date_suffix()
{
	char buf[32];
	time_t now = time(NULL);
	struct tm tm_now;

	acl_localtime_r(&now, &tm_now);

	strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm_now);
	return buf;
}

static bool copy_storage_file_plain(const std::string& source,
	const std::string& dest, std::string& err)
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
		(void) unlink(dest.c_str());
	}
	return ok;
}

static bool backup_project_db_files(const std::string& storage_dir,
	bool move_files, std::string& err)
{
	static const char* db_files[] = {
		".video_resume.db",
		".tag_catalog.db",
		".recycle_bin.db",
		".folder_catalog.db"
	};
	const std::string backup_dir = join_storage_path(storage_dir, ".backup");
	const std::string date_suffix = storage_backup_date_suffix();
	bool backup_ready = false;
	for (size_t i = 0; i < sizeof(db_files) / sizeof(db_files[0]); ++i) {
		const std::string source = join_storage_path(storage_dir, db_files[i]);
		struct stat st;
		if (lstat(source.c_str(), &st) != 0) {
			if (errno == ENOENT) {
				continue;
			}
			err = strerror(errno);
			return false;
		}
		if (!S_ISREG(st.st_mode)) {
			continue;
		}
		if (!backup_ready) {
			if (!make_dir_recursive(backup_dir.c_str())) {
				err = "cannot create backup directory";
				return false;
			}
			backup_ready = true;
		}
		const std::string backup_name = std::string(db_files[i]) + "." + date_suffix;
		std::string dest = join_storage_path(backup_dir, backup_name.c_str());
		struct stat dest_st;
		if (lstat(dest.c_str(), &dest_st) == 0) {
			bool found_free_name = false;
			for (int seq = 1; seq < 10000; ++seq) {
				const std::string candidate_name = backup_name + "." + std::to_string(seq);
				dest = join_storage_path(backup_dir, candidate_name.c_str());
				if (lstat(dest.c_str(), &dest_st) != 0 && errno == ENOENT) {
					found_free_name = true;
					break;
				}
			}
			if (!found_free_name) {
				err = "cannot allocate backup database file name";
				return false;
			}
		} else if (errno != ENOENT) {
			err = strerror(errno);
			return false;
		}
		if (move_files) {
			if (rename(source.c_str(), dest.c_str()) != 0) {
				err = strerror(errno);
				return false;
			}
		} else if (!copy_storage_file_plain(source, dest, err)) {
			return false;
		}
	}
	return true;
}

static bool collect_move_items(const std::string& source,
	const std::string& target, std::vector<storage_move_item_t>& items,
	std::string& err)
{
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
		if (strcmp(entry->d_name, ".backup") == 0) {
			continue;
		}
		const std::string child_source = join_storage_path(source, entry->d_name);
		const std::string child_target = join_storage_path(target, entry->d_name);
		struct stat st;
		if (lstat(child_source.c_str(), &st) != 0) {
			err = strerror(errno);
			closedir(dir);
			return false;
		}
		struct stat target_st;
		if (lstat(child_target.c_str(), &target_st) == 0) {
			if (!(S_ISDIR(st.st_mode) && S_ISDIR(target_st.st_mode))) {
				if (!(S_ISREG(st.st_mode) && S_ISREG(target_st.st_mode))) {
					err = "target already contains: ";
					err += entry->d_name;
					closedir(dir);
					return false;
				}
			}
		} else if (errno != ENOENT) {
			err = strerror(errno);
			closedir(dir);
			return false;
		}
		if (S_ISDIR(st.st_mode)) {
			storage_move_item_t dir_item;
			dir_item.source = child_source;
			dir_item.target = child_target;
			dir_item.directory = true;
			dir_item.size = 0;
			items.push_back(dir_item);
			if (!collect_move_items(child_source, child_target, items, err)) {
				closedir(dir);
				return false;
			}
		} else if (S_ISREG(st.st_mode)) {
			storage_move_item_t file_item;
			file_item.source = child_source;
			file_item.target = child_target;
			file_item.directory = false;
			file_item.size = regular_file_size(child_source);
			items.push_back(file_item);
		}
	}
	closedir(dir);
	return true;
}

static bool copy_file_with_progress(const storage_move_item_t& item,
	storage_migrate_task_t& task, std::string& err)
{
	FILE* in = fopen(item.source.c_str(), "rb");
	if (in == NULL) {
		err = strerror(errno);
		return false;
	}
	FILE* out = fopen(item.target.c_str(), "wb");
	if (out == NULL) {
		err = strerror(errno);
		fclose(in);
		return false;
	}
	char buf[1024 * 64];
	bool ok = true;
	while (true) {
		if (!storage_task_wait_if_paused_or_cancelled(task)) {
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
			task.moved_bytes += (long long) n;
			update_task(task);
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
		::unlink(item.target.c_str());
	}
	return ok;
}

static std::string storage_base_name(const std::string& path)
{
#ifdef _WIN32
	if (path.empty()) {
		return path;
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
	if (text.size() >= 2 && text.size() <= 3 && text[1] == ':') {
		return text;
	}
	std::string::size_type pos = text.rfind('/');
	return pos == std::string::npos ? text : text.substr(pos + 1);
#else
	if (path.empty() || path == "/") {
		return path;
	}
	std::string text = path;
	while (text.size() > 1 && text[text.size() - 1] == '/') {
		text.erase(text.size() - 1);
	}
	std::string::size_type pos = text.rfind('/');
	return pos == std::string::npos ? text : text.substr(pos + 1);
#endif
}

static std::string wait_storage_conflict_resolution(storage_migrate_task_t& task,
	const storage_move_item_t& item)
{
	if (!task.conflict_default.empty()) {
		return task.conflict_default;
	}
	storage_migrate_task_t initial_snapshot = current_storage_task_snapshot();
	if (initial_snapshot.id == task.id && !initial_snapshot.conflict_default.empty()) {
		task.conflict_default = initial_snapshot.conflict_default;
		return task.conflict_default;
	}
	task.state = "conflict";
	task.message = "发现同名文件";
	task.conflict_source = item.source;
	task.conflict_target = item.target;
	task.conflict_name = storage_base_name(item.source);
	task.conflict_resolution.clear();
	update_task(task);
	while (true) {
		//usleep(200 * 1000);
		acl_doze(200);
		storage_migrate_task_t snapshot = current_storage_task_snapshot();
		if (snapshot.id != task.id) {
			return "cancel";
		}
		if (snapshot.cancel_requested) {
			task.cancel_requested = true;
			return "cancel";
		}
		if (snapshot.pause_requested) {
			task.pause_requested = true;
			task.state = "paused";
			task.message = "迁移已暂停";
			update_task(task);
			continue;
		}
		if (task.state == "paused") {
			task.pause_requested = false;
			task.state = "conflict";
			task.message = "发现同名文件";
			update_task(task);
		}
		if (!snapshot.conflict_resolution.empty()) {
			const std::string choice = snapshot.conflict_resolution;
			if (choice == "remember-overwrite") {
				task.conflict_default = "overwrite";
			} else if (choice == "remember-skip") {
				task.conflict_default = "skip";
			}
			task.conflict_resolution.clear();
			task.conflict_source.clear();
			task.conflict_target.clear();
			task.conflict_name.clear();
			task.state = "running";
			task.message = "继续迁移";
			update_task(task);
			return task.conflict_default.empty() ? choice : task.conflict_default;
		}
	}
}

static bool delete_path_recursive_plain(const std::string& path, std::string& err)
{
	struct stat st;
	if (lstat(path.c_str(), &st) != 0) {
		if (errno == ENOENT) {
			return true;
		}
		err = strerror(errno);
		return false;
	}
	if (S_ISDIR(st.st_mode)) {
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
			if (!delete_path_recursive_plain(join_storage_path(path, entry->d_name), err)) {
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
	if (unlink(path.c_str()) != 0) {
		err = strerror(errno);
		return false;
	}
	return true;
}

static bool delete_storage_contents_except_backup(const std::string& path,
	std::string& err)
{
	DIR* dir = opendir(path.c_str());
	if (dir == NULL) {
		err = strerror(errno);
		return false;
	}
	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0
			|| strcmp(entry->d_name, ".backup") == 0)
		{
			continue;
		}
		if (!delete_path_recursive_plain(join_storage_path(path, entry->d_name), err)) {
			closedir(dir);
			return false;
		}
	}
	closedir(dir);
	return true;
}

static void run_storage_migration(storage_migrate_task_t task,
	std::vector<storage_move_item_t> items)
{
	task.state = "running";
	task.message = "正在准备迁移";
	update_task(task);

	std::string err;
	for (size_t i = 0; i < items.size(); ++i) {
		if (!storage_task_wait_if_paused_or_cancelled(task)) {
			return;
		}
		if (items[i].directory) {
			if (!make_dir_recursive(items[i].target.c_str())) {
				task.state = "failed";
				task.error = "cannot create target directory";
				update_task(task);
				return;
			}
			continue;
		}
		struct stat target_st;
		if (lstat(items[i].target.c_str(), &target_st) == 0) {
			const std::string choice = wait_storage_conflict_resolution(task, items[i]);
			if (choice == "cancel") {
				task.state = "cancelled";
				task.message = "迁移已取消";
				update_task(task);
				return;
			}
			if (choice == "skip") {
				task.moved_bytes += items[i].size;
				task.moved_files += 1;
				task.message = std::string("正在处理同名文件(跳过)：") + items[i].source;
				update_task(task);
				continue;
			}
			if (choice != "overwrite") {
				task.state = "failed";
				task.error = "invalid conflict resolution";
				update_task(task);
				return;
			}
			task.message = std::string("正在处理同名文件(覆盖)：") + items[i].source;
			update_task(task);
		} else if (errno != ENOENT) {
			task.state = "failed";
			task.error = strerror(errno);
			update_task(task);
			return;
		}
		if (task.message.compare(0, strlen("正在处理同名文件(覆盖)："), "正在处理同名文件(覆盖)：") != 0) {
			task.message = std::string("正在拷贝：") + items[i].source;
			update_task(task);
		}
		if (!copy_file_with_progress(items[i], task, err)) {
			task.state = "failed";
			task.error = err;
			update_task(task);
			return;
		}
		task.moved_files += 1;
		update_task(task);
	}

	std::string init_err;
	if (!init_storage_databases(task.target_dir, init_err)) {
		task.state = "failed";
		task.error = init_err;
		update_task(task);
		return;
	}
	runtime_upload_dir_set(task.target_dir);
	task.state = "done";
	task.message = "迁移完成";
	task.moved_bytes = task.total_bytes;
	update_task(task);
}

} // namespace

void runtime_upload_dir_init(const std::string& upload_dir)
{
	std::lock_guard<std::mutex> guard(g_runtime_upload_mutex);
	if (g_runtime_upload_dir.empty()) {
		g_runtime_upload_dir = upload_dir;
	}
}

std::string runtime_upload_dir_get()
{
	std::lock_guard<std::mutex> guard(g_runtime_upload_mutex);
	return g_runtime_upload_dir;
}

void runtime_upload_dir_set(const std::string& upload_dir)
{
	std::lock_guard<std::mutex> guard(g_runtime_upload_mutex);
	g_runtime_upload_dir = upload_dir;
}

bool local_disk_access_allowed(const std::string& upload_dir, bool admin,
	std::string& err)
{
	std::lock_guard<std::mutex> guard(g_settings_mutex);
	webcool_settings_t settings;
	if (!load_settings_unlocked(upload_dir, settings, err)) {
		return false;
	}
	return admin ? settings.local_disk_admin : settings.local_disk_user;
}

bool AdminStorageInfoAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", upload_dir.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AdminLocalDiskSettingsAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string err;
	webcool_settings_t settings;
	{
		std::lock_guard<std::mutex> guard(g_settings_mutex);
		if (!load_settings_unlocked(upload_dir, settings, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		if (req.getMethod() == acl::HTTP_METHOD_POST) {
			settings.local_disk_admin = request_bool_param(req,
				"local_disk_admin", settings.local_disk_admin);
			settings.local_disk_user = request_bool_param(req,
				"local_disk_user", settings.local_disk_user);
			if (!save_settings_unlocked(upload_dir, settings, err)) {
				json_error(res, 500, err.c_str(), req.isKeepAlive());
				return true;
			}
		}
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_bool("local_disk_admin", settings.local_disk_admin);
	root.add_bool("local_disk_user", settings.local_disk_user);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AdminStorageMigrateAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string source_dir, target_dir, err;
	const char* migrate_param = req.getParameter("migrate");
	const bool migrate_files = !(migrate_param != NULL
		&& (strcmp(migrate_param, "0") == 0
			|| strcasecmp(migrate_param, "false") == 0
			|| strcasecmp(migrate_param, "no") == 0));
	if (!ensure_storage_target_path(req.getParameter("path") ? req.getParameter("path") : "",
		target_dir, err))
	{
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!migrate_files) {
		if (!init_storage_databases(target_dir, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		runtime_upload_dir_set(target_dir);
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_bool("migrated", false);
		root.add_text("path", target_dir.c_str());
		return sendJson(res, 200, root, req.isKeepAlive());
	}
	if (!canonical_existing_path(upload_dir, source_dir, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (source_dir == target_dir) {
		json_error(res, 409, "target path is current storage path", req.isKeepAlive());
		return true;
	}
	if (is_same_or_child_path(source_dir, target_dir)) {
		json_error(res, 409, "target path cannot be inside current storage path", req.isKeepAlive());
		return true;
	}
	{
		std::lock_guard<std::mutex> guard(g_storage_migrate_mutex);
		if (g_storage_migrate_task.state == "queued"
			|| g_storage_migrate_task.state == "running")
		{
			json_error(res, 409, "storage migration is already running", req.isKeepAlive());
			return true;
		}
	}
	if (!backup_project_db_files(source_dir, false, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!backup_project_db_files(target_dir, true, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	std::vector<storage_move_item_t> items;
	if (!collect_move_items(source_dir, target_dir, items, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	storage_migrate_task_t task;
	task.id = make_task_id();
	task.state = "queued";
	task.message = "等待移动";
	task.source_dir = source_dir;
	task.target_dir = target_dir;
	task.cleanup_done = false;
	task.pause_requested = false;
	task.cancel_requested = false;
	task.total_bytes = 0;
	task.moved_bytes = 0;
	task.total_files = 0;
	task.moved_files = 0;
	for (size_t i = 0; i < items.size(); ++i) {
		if (!items[i].directory) {
			task.total_files += 1;
			task.total_bytes += items[i].size;
		}
	}
	update_task(task);
	std::thread(run_storage_migration, task, items).detach();

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", task.id.c_str());
	root.add_number("total_files", task.total_files);
	root.add_number("total_bytes", task.total_bytes);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AdminStorageMigrateProgressAction::run(request_t& req, response_t& res)
{
	storage_migrate_task_t task;
	{
		std::lock_guard<std::mutex> guard(g_storage_migrate_mutex);
		task = g_storage_migrate_task;
	}
	const char* task_id = req.getParameter("task_id");
	if (task_id != NULL && *task_id != '\0' && task.id != task_id) {
		json_error(res, 404, "task not found", req.isKeepAlive());
		return true;
	}
	const double progress = task.total_bytes > 0
		? ((double) task.moved_bytes * 100.0 / (double) task.total_bytes)
		: (task.state == "done" ? 100.0 : 0.0);
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", task.id.c_str());
	root.add_text("state", task.state.c_str());
	root.add_text("message", task.message.c_str());
	root.add_text("error", task.error.c_str());
	root.add_text("source_dir", task.source_dir.c_str());
	root.add_text("target_dir", task.target_dir.c_str());
	root.add_text("conflict_source", task.conflict_source.c_str());
	root.add_text("conflict_target", task.conflict_target.c_str());
	root.add_text("conflict_name", task.conflict_name.c_str());
	root.add_bool("cleanup_done", task.cleanup_done);
	root.add_bool("pause_requested", task.pause_requested);
	root.add_bool("cancel_requested", task.cancel_requested);
	root.add_number("progress", (long long) progress);
	root.add_number("total_bytes", task.total_bytes);
	root.add_number("moved_bytes", task.moved_bytes);
	root.add_number("total_files", task.total_files);
	root.add_number("moved_files", task.moved_files);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AdminStorageMigrateResolveAction::run(request_t& req, response_t& res)
{
	const char* task_id = req.getParameter("task_id");
	const char* choice_text = req.getParameter("choice");
	std::string choice = choice_text ? choice_text : "";
	if (choice != "overwrite" && choice != "skip" && choice != "cancel"
		&& choice != "remember-overwrite" && choice != "remember-skip")
	{
		json_error(res, 400, "invalid conflict choice", req.isKeepAlive());
		return true;
	}
	{
		std::lock_guard<std::mutex> guard(g_storage_migrate_mutex);
		if (task_id != NULL && *task_id != '\0' && g_storage_migrate_task.id != task_id) {
			json_error(res, 404, "task not found", req.isKeepAlive());
			return true;
		}
		if (g_storage_migrate_task.state != "conflict") {
			json_error(res, 409, "migration task is not waiting for conflict resolution", req.isKeepAlive());
			return true;
		}
		g_storage_migrate_task.conflict_resolution = choice;
		if (choice == "remember-overwrite") {
			g_storage_migrate_task.conflict_default = "overwrite";
		} else if (choice == "remember-skip") {
			g_storage_migrate_task.conflict_default = "skip";
		}
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("choice", choice.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AdminStorageMigrateControlAction::run(request_t& req, response_t& res)
{
	const char* task_id = req.getParameter("task_id");
	const char* action_text = req.getParameter("action");
	std::string action = action_text ? action_text : "";
	if (action != "pause" && action != "resume" && action != "cancel") {
		json_error(res, 400, "invalid migration control action", req.isKeepAlive());
		return true;
	}
	{
		std::lock_guard<std::mutex> guard(g_storage_migrate_mutex);
		if (task_id != NULL && *task_id != '\0' && g_storage_migrate_task.id != task_id) {
			json_error(res, 404, "task not found", req.isKeepAlive());
			return true;
		}
		if (g_storage_migrate_task.state != "queued"
			&& g_storage_migrate_task.state != "running"
			&& g_storage_migrate_task.state != "paused"
			&& g_storage_migrate_task.state != "conflict")
		{
			json_error(res, 409, "migration task cannot be controlled", req.isKeepAlive());
			return true;
		}
		if (action == "pause") {
			g_storage_migrate_task.pause_requested = true;
		} else if (action == "resume") {
			g_storage_migrate_task.pause_requested = false;
		} else if (action == "cancel") {
			g_storage_migrate_task.cancel_requested = true;
			g_storage_migrate_task.pause_requested = false;
			if (g_storage_migrate_task.state == "conflict" || g_storage_migrate_task.state == "paused") {
				g_storage_migrate_task.state = "cancelled";
				g_storage_migrate_task.message = "迁移已取消";
			}
		}
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("action", action.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AdminStorageMigrateCleanupAction::run(request_t& req, response_t& res)
{
	const char* task_id = req.getParameter("task_id");
	storage_migrate_task_t task;
	{
		std::lock_guard<std::mutex> guard(g_storage_migrate_mutex);
		if (task_id != NULL && *task_id != '\0' && g_storage_migrate_task.id != task_id) {
			json_error(res, 404, "task not found", req.isKeepAlive());
			return true;
		}
		task = g_storage_migrate_task;
	}
	if (task.state != "done") {
		json_error(res, 409, "migration task is not completed", req.isKeepAlive());
		return true;
	}
	std::string err;
	if (!delete_storage_contents_except_backup(task.source_dir, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	{
		std::lock_guard<std::mutex> guard(g_storage_migrate_mutex);
		if (g_storage_migrate_task.id == task.id) {
			g_storage_migrate_task.cleanup_done = true;
		}
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("source_dir", task.source_dir.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

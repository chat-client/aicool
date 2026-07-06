#include "stdafx.h"
#include "admin_internal.h"

#ifdef _WIN32
#include "../../platform_compat.h"
#else
#include <dirent.h>
#include <sys/stat.h>
#include <utime.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <thread>

namespace action {
namespace admin_internal {

webcool::mutex g_storage_backup_task_mutex;
storage_migrate_task_t g_storage_backup_task;
unsigned long long g_storage_backup_seq = 0;
webcool::mutex g_storage_backup_mutex;
webcool::mutex g_storage_backup_auto_mutex;
struct storage_backup_auto_event_t {
	std::string upload_dir;
	std::vector<std::string> sync_paths;
	std::vector<std::string> delete_paths;
	std::vector<std::string> move_from_paths;
	std::vector<std::string> move_to_paths;
	bool full_sync;
};
std::vector<storage_backup_auto_event_t> g_storage_backup_auto_events;
bool g_storage_backup_auto_running = false;

std::string make_backup_task_id()
{
	std::lock_guard<webcool::mutex> guard(g_storage_backup_task_mutex);
	++g_storage_backup_seq;
	return std::string("storage-backup-") + std::to_string((long long) time(NULL))
		+ "-" + std::to_string((long long) getpid())
		+ "-" + std::to_string((long long) g_storage_backup_seq);
}

void update_backup_task(const storage_migrate_task_t& task)
{
	std::lock_guard<webcool::mutex> guard(g_storage_backup_task_mutex);
	storage_migrate_task_t merged = task;
	if (g_storage_backup_task.id == task.id
		&& task.state != "done"
		&& task.state != "failed"
		&& task.state != "cancelled")
	{
		merged.pause_requested = task.pause_requested
			|| g_storage_backup_task.pause_requested;
		merged.cancel_requested = task.cancel_requested
			|| g_storage_backup_task.cancel_requested;
		if (merged.conflict_resolution.empty()) {
			merged.conflict_resolution = g_storage_backup_task.conflict_resolution;
		}
		if (merged.conflict_default.empty()) {
			merged.conflict_default = g_storage_backup_task.conflict_default;
		}
	}
	g_storage_backup_task = merged;
}

storage_migrate_task_t current_backup_task_snapshot()
{
	std::lock_guard<webcool::mutex> guard(g_storage_backup_task_mutex);
	return g_storage_backup_task;
}

bool backup_task_wait_if_paused_or_cancelled(storage_migrate_task_t& task)
{
	while (true) {
		storage_migrate_task_t snapshot = current_backup_task_snapshot();
		if (snapshot.id != task.id) {
			return false;
		}
		task.pause_requested = snapshot.pause_requested;
		task.cancel_requested = snapshot.cancel_requested;
		if (task.cancel_requested) {
			task.state = "cancelled";
			task.message = "备份已停止";
			update_backup_task(task);
			return false;
		}
		if (!task.pause_requested) {
			if (task.state == "paused") {
				task.state = "running";
				task.message = "继续备份";
				update_backup_task(task);
			}
			return true;
		}
		task.state = "paused";
		task.message = "备份已暂停";
		update_backup_task(task);
		acl_doze(200);
	}
}

bool files_have_same_data(const std::string& left,
	const std::string& right, bool& same, std::string& err)
{
	same = false;
	struct stat left_st, right_st;
	if (lstat(left.c_str(), &left_st) != 0
		|| lstat(right.c_str(), &right_st) != 0)
	{
		err = strerror(errno);
		return false;
	}
	if (!S_ISREG(left_st.st_mode) || !S_ISREG(right_st.st_mode)
		|| left_st.st_size != right_st.st_size)
	{
		return true;
	}
	FILE* left_fp = fopen(left.c_str(), "rb");
	if (left_fp == NULL) {
		err = strerror(errno);
		return false;
	}
	FILE* right_fp = fopen(right.c_str(), "rb");
	if (right_fp == NULL) {
		err = strerror(errno);
		fclose(left_fp);
		return false;
	}
	char left_buf[1024 * 64];
	char right_buf[1024 * 64];
	bool ok = true;
	same = true;
	while (true) {
		const size_t left_n = fread(left_buf, 1, sizeof(left_buf), left_fp);
		const size_t right_n = fread(right_buf, 1, sizeof(right_buf), right_fp);
		if (left_n != right_n || (left_n > 0 && memcmp(left_buf, right_buf, left_n) != 0)) {
			same = false;
			break;
		}
		if (left_n < sizeof(left_buf)) {
			if (ferror(left_fp) || ferror(right_fp)) {
				err = strerror(errno);
				ok = false;
			}
			break;
		}
	}
	fclose(left_fp);
	fclose(right_fp);
	return ok;
}

bool copy_backup_file_with_progress(const storage_move_item_t& item,
	storage_migrate_task_t& task, std::string& err)
{
	const std::string parent = item.target.substr(0, item.target.find_last_of("/\\"));
	if (!parent.empty() && !make_dir_recursive(parent.c_str())) {
		err = "cannot create target directory";
		return false;
	}
	struct stat source_st;
	if (lstat(item.source.c_str(), &source_st) != 0) {
		err = strerror(errno);
		return false;
	}
	const std::string tmp_target = item.target + ".webcool-sync-tmp-"
		+ std::to_string((long long) getpid());
	(void) ::unlink(tmp_target.c_str());
	FILE* in = fopen(item.source.c_str(), "rb");
	if (in == NULL) {
		err = strerror(errno);
		return false;
	}
	FILE* out = fopen(tmp_target.c_str(), "wb");
	if (out == NULL) {
		err = strerror(errno);
		fclose(in);
		return false;
	}
	char buf[1024 * 64];
	bool ok = true;
	while (true) {
		if (!backup_task_wait_if_paused_or_cancelled(task)) {
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
			update_backup_task(task);
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
		::unlink(tmp_target.c_str());
		return false;
	}
	(void) chmod(tmp_target.c_str(), source_st.st_mode & 0777);
	if (rename(tmp_target.c_str(), item.target.c_str()) != 0) {
		err = strerror(errno);
		::unlink(tmp_target.c_str());
		return false;
	}
	return true;
}

bool collect_backup_sync_items(const std::string& source,
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
			if ((S_ISDIR(st.st_mode) && !S_ISDIR(target_st.st_mode))
				|| (S_ISREG(st.st_mode) && !S_ISREG(target_st.st_mode)))
			{
				err = "target already contains incompatible path: ";
				err += entry->d_name;
				closedir(dir);
				return false;
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
			if (!collect_backup_sync_items(child_source, child_target, items, err)) {
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

bool delete_backup_target_extras(const std::string& source,
	const std::string& target, std::string& err)
{
	DIR* dir = opendir(target.c_str());
	if (dir == NULL) {
		err = strerror(errno);
		return false;
	}
	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		const std::string source_child = join_storage_path(source, entry->d_name);
		const std::string target_child = join_storage_path(target, entry->d_name);
		struct stat source_st;
		if (lstat(source_child.c_str(), &source_st) != 0) {
			if (errno == ENOENT) {
				if (!delete_path_recursive_plain(target_child, err)) {
					closedir(dir);
					return false;
				}
				continue;
			}
			err = strerror(errno);
			closedir(dir);
			return false;
		}
		struct stat target_st;
		if (lstat(target_child.c_str(), &target_st) != 0) {
			if (errno == ENOENT) {
				continue;
			}
			err = strerror(errno);
			closedir(dir);
			return false;
		}
		if (S_ISDIR(source_st.st_mode) && S_ISDIR(target_st.st_mode)) {
			if (!delete_backup_target_extras(source_child, target_child, err)) {
				closedir(dir);
				return false;
			}
		}
	}
	closedir(dir);
	return true;
}

bool backup_regular_file_is_current(const std::string& target,
	const struct stat& source_st)
{
	struct stat target_st;
	if (lstat(target.c_str(), &target_st) != 0) {
		return false;
	}
	if (!S_ISREG(target_st.st_mode)) {
		return false;
	}
	return target_st.st_size == source_st.st_size
		&& target_st.st_mtime == source_st.st_mtime
		&& ((target_st.st_mode & 0777) == (source_st.st_mode & 0777));
}

bool copy_backup_file_atomic_plain(const std::string& source,
	const std::string& target, const struct stat& source_st, std::string& err)
{
	const std::string parent = target.substr(0, target.find_last_of("/\\"));
	if (!parent.empty() && !make_dir_recursive(parent.c_str())) {
		err = "cannot create backup parent directory";
		return false;
	}
	const std::string tmp_target = target + ".webcool-auto-sync-tmp-"
		+ std::to_string((long long) getpid());
	(void) unlink(tmp_target.c_str());
	if (!copy_storage_file_plain(source, tmp_target, err)) {
		(void) unlink(tmp_target.c_str());
		return false;
	}
	(void) chmod(tmp_target.c_str(), source_st.st_mode & 0777);
#ifndef _WIN32
	struct utimbuf times;
	times.actime = source_st.st_atime;
	times.modtime = source_st.st_mtime;
	(void) utime(tmp_target.c_str(), &times);
#endif
	struct stat target_st;
	if (lstat(target.c_str(), &target_st) == 0 && !S_ISREG(target_st.st_mode)) {
		if (!delete_path_recursive_plain(target, err)) {
			(void) unlink(tmp_target.c_str());
			return false;
		}
	}
	if (rename(tmp_target.c_str(), target.c_str()) != 0) {
		err = strerror(errno);
		(void) unlink(tmp_target.c_str());
		return false;
	}
	return true;
}

bool sync_backup_target_extras_incremental(const std::string& source,
	const std::string& target, std::string& err)
{
	DIR* dir = opendir(target.c_str());
	if (dir == NULL) {
		if (errno == ENOENT) {
			return true;
		}
		err = strerror(errno);
		return false;
	}
	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		const std::string source_child = join_storage_path(source, entry->d_name);
		const std::string target_child = join_storage_path(target, entry->d_name);
		struct stat source_st;
		if (lstat(source_child.c_str(), &source_st) != 0) {
			if (errno == ENOENT) {
				if (!delete_path_recursive_plain(target_child, err)) {
					closedir(dir);
					return false;
				}
				continue;
			}
			err = strerror(errno);
			closedir(dir);
			return false;
		}
		if (!S_ISDIR(source_st.st_mode) && !S_ISREG(source_st.st_mode)) {
			if (!delete_path_recursive_plain(target_child, err)) {
				closedir(dir);
				return false;
			}
		}
	}
	closedir(dir);
	return true;
}

bool sync_backup_tree_incremental(const std::string& source,
	const std::string& target, std::string& err)
{
	struct stat source_st;
	if (lstat(source.c_str(), &source_st) != 0) {
		err = strerror(errno);
		return false;
	}
	if (!S_ISDIR(source_st.st_mode)) {
		err = "backup source is not a directory";
		return false;
	}
	struct stat target_st;
	if (lstat(target.c_str(), &target_st) == 0) {
		if (!S_ISDIR(target_st.st_mode)) {
			if (!delete_path_recursive_plain(target, err)) {
				return false;
			}
		}
	} else if (errno != ENOENT) {
		err = strerror(errno);
		return false;
	}
	if (!make_dir_recursive(target.c_str())) {
		err = "cannot create backup directory";
		return false;
	}
	(void) chmod(target.c_str(), source_st.st_mode & 0777);

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
		const std::string source_child = join_storage_path(source, entry->d_name);
		const std::string target_child = join_storage_path(target, entry->d_name);
		struct stat child_st;
		if (lstat(source_child.c_str(), &child_st) != 0) {
			err = strerror(errno);
			closedir(dir);
			return false;
		}
		if (S_ISDIR(child_st.st_mode)) {
			if (!sync_backup_tree_incremental(source_child, target_child, err)) {
				closedir(dir);
				return false;
			}
		} else if (S_ISREG(child_st.st_mode)) {
			if (!backup_regular_file_is_current(target_child, child_st)
				&& !copy_backup_file_atomic_plain(source_child, target_child,
					child_st, err))
			{
				closedir(dir);
				return false;
			}
		}
	}
	closedir(dir);
	return sync_backup_target_extras_incremental(source, target, err);
}

bool storage_backup_sync_incremental_blocking(const std::string& upload_dir,
	std::string& err)
{
	std::vector<storage_backup_path_t> backup_paths;
	{
		std::lock_guard<webcool::mutex> settings_guard(g_settings_mutex);
		webcool_settings_t settings;
		if (!load_settings_unlocked(upload_dir, settings, err)) {
			return false;
		}
		backup_paths = settings.backup_paths;
	}
	if (backup_paths.empty()) {
		return true;
	}

	std::lock_guard<webcool::mutex> backup_guard(g_storage_backup_mutex);
	std::string source_dir;
	if (!canonical_existing_path(upload_dir, source_dir, err)) {
		return false;
	}
	std::vector<storage_backup_path_t> enabled_paths;
	for (size_t i = 0; i < backup_paths.size(); ++i) {
		if (backup_paths[i].enabled) {
			enabled_paths.push_back(backup_paths[i]);
		}
	}
	for (size_t i = 0; i < backup_paths.size(); ++i) {
		if (!backup_paths[i].enabled) {
			continue;
		}
		std::string target_dir;
		if (!ensure_storage_target_path(backup_paths[i].path, target_dir, err)) {
			return false;
		}
		if (!validate_backup_path_for_settings(source_dir, target_dir,
			enabled_paths, backup_paths[i].path, err))
		{
			return false;
		}
		if (!sync_backup_tree_incremental(source_dir, target_dir, err)) {
			return false;
		}
	}
	return true;
}

bool storage_backup_sync_relative_path_blocking(const std::string& source_dir,
	const std::string& target_dir, const std::string& relative_path,
	std::string& err)
{
	if (relative_path.empty()) {
		return sync_backup_tree_incremental(source_dir, target_dir, err);
	}
	const std::string source_path = join_storage_path(source_dir, relative_path.c_str());
	const std::string target_path = join_storage_path(target_dir, relative_path.c_str());
	struct stat source_st;
	if (lstat(source_path.c_str(), &source_st) != 0) {
		if (errno == ENOENT) {
			return delete_path_recursive_plain(target_path, err);
		}
		err = strerror(errno);
		return false;
	}
	if (S_ISDIR(source_st.st_mode)) {
		return sync_backup_tree_incremental(source_path, target_path, err);
	}
	if (S_ISREG(source_st.st_mode)) {
		if (backup_regular_file_is_current(target_path, source_st)) {
			return true;
		}
		return copy_backup_file_atomic_plain(source_path, target_path, source_st, err);
	}
	return delete_path_recursive_plain(target_path, err);
}

bool storage_backup_delete_relative_path_blocking(const std::string& target_dir,
	const std::string& relative_path, std::string& err)
{
	if (relative_path.empty()) {
		return true;
	}
	const std::string target_path = join_storage_path(target_dir, relative_path.c_str());
	return delete_path_recursive_plain(target_path, err);
}

bool storage_backup_move_relative_path_blocking(const std::string& source_dir,
	const std::string& target_dir, const std::string& from_relative_path,
	const std::string& to_relative_path, std::string& err)
{
	if (from_relative_path.empty() || to_relative_path.empty()) {
		return true;
	}
	if (from_relative_path == to_relative_path) {
		return storage_backup_sync_relative_path_blocking(source_dir,
			target_dir, to_relative_path, err);
	}
	const std::string target_from = join_storage_path(target_dir,
		from_relative_path.c_str());
	const std::string target_to = join_storage_path(target_dir,
		to_relative_path.c_str());
	struct stat from_st;
	if (lstat(target_from.c_str(), &from_st) == 0) {
		const std::string parent = target_to.substr(0, target_to.find_last_of("/\\"));
		if (!parent.empty() && !make_dir_recursive(parent.c_str())) {
			err = "cannot create backup move target parent";
			return false;
		}
		struct stat to_st;
		if (lstat(target_to.c_str(), &to_st) == 0) {
			if (!delete_path_recursive_plain(target_to, err)) {
				return false;
			}
		} else if (errno != ENOENT) {
			err = strerror(errno);
			return false;
		}
		if (rename(target_from.c_str(), target_to.c_str()) == 0) {
			return true;
		}
		if (errno != ENOENT) {
			err = strerror(errno);
			return false;
		}
	} else if (errno != ENOENT) {
		err = strerror(errno);
		return false;
	}

	if (!storage_backup_sync_relative_path_blocking(source_dir, target_dir,
		to_relative_path, err))
	{
		return false;
	}
	return storage_backup_delete_relative_path_blocking(target_dir,
		from_relative_path, err);
}

std::string backup_join_relative_path(const std::string& prefix,
	const std::string& path);
bool backup_relative_scope_prefix(const std::string& storage_dir,
	const std::string& scope_dir, std::string& prefix, std::string& err);
void backup_auto_add_path(std::vector<std::string>& paths,
	const std::string& path);

bool storage_backup_sync_paths_blocking(const std::string& upload_dir,
	const std::vector<std::string>& sync_paths,
	const std::vector<std::string>& delete_paths,
	const std::vector<std::string>& move_from_paths,
	const std::vector<std::string>& move_to_paths, std::string& err)
{
	const std::string storage_dir = runtime_upload_dir_get().empty()
		? upload_dir : runtime_upload_dir_get();
	std::string scope_prefix;
	if (!backup_relative_scope_prefix(storage_dir, upload_dir, scope_prefix, err)) {
		return false;
	}
	std::vector<std::string> storage_sync_paths;
	std::vector<std::string> storage_delete_paths;
	std::vector<std::string> storage_move_from_paths;
	std::vector<std::string> storage_move_to_paths;
	for (size_t i = 0; i < sync_paths.size(); ++i) {
		backup_auto_add_path(storage_sync_paths,
			backup_join_relative_path(scope_prefix, sync_paths[i]));
	}
	for (size_t i = 0; i < delete_paths.size(); ++i) {
		backup_auto_add_path(storage_delete_paths,
			backup_join_relative_path(scope_prefix, delete_paths[i]));
	}
	const size_t move_count = move_from_paths.size() < move_to_paths.size()
		? move_from_paths.size() : move_to_paths.size();
	for (size_t i = 0; i < move_count; ++i) {
		storage_move_from_paths.push_back(
			backup_join_relative_path(scope_prefix, move_from_paths[i]));
		storage_move_to_paths.push_back(
			backup_join_relative_path(scope_prefix, move_to_paths[i]));
	}

	std::vector<storage_backup_path_t> backup_paths;
	{
		std::lock_guard<webcool::mutex> settings_guard(g_settings_mutex);
		webcool_settings_t settings;
		if (!load_settings_unlocked(storage_dir, settings, err)) {
			return false;
		}
		backup_paths = settings.backup_paths;
	}
	if (backup_paths.empty()) {
		return true;
	}

	std::lock_guard<webcool::mutex> backup_guard(g_storage_backup_mutex);
	std::string source_dir;
	if (!canonical_existing_path(storage_dir, source_dir, err)) {
		return false;
	}
	std::vector<storage_backup_path_t> enabled_paths;
	for (size_t i = 0; i < backup_paths.size(); ++i) {
		if (backup_paths[i].enabled) {
			enabled_paths.push_back(backup_paths[i]);
		}
	}
	for (size_t i = 0; i < backup_paths.size(); ++i) {
		if (!backup_paths[i].enabled) {
			continue;
		}
		std::string target_dir;
		if (!ensure_storage_target_path(backup_paths[i].path, target_dir, err)) {
			return false;
		}
		if (!validate_backup_path_for_settings(source_dir, target_dir,
			enabled_paths, backup_paths[i].path, err))
		{
			return false;
		}
		for (size_t j = 0; j < storage_move_from_paths.size(); ++j) {
			if (!storage_backup_move_relative_path_blocking(source_dir, target_dir,
				storage_move_from_paths[j], storage_move_to_paths[j], err))
			{
				return false;
			}
		}
		for (size_t j = 0; j < storage_delete_paths.size(); ++j) {
			if (!storage_backup_delete_relative_path_blocking(target_dir,
				storage_delete_paths[j], err))
			{
				return false;
			}
		}
		for (size_t j = 0; j < storage_sync_paths.size(); ++j) {
			if (!storage_backup_sync_relative_path_blocking(source_dir, target_dir,
				storage_sync_paths[j], err))
			{
				return false;
			}
		}
	}
	return true;
}

bool backup_auto_path_exists(const std::vector<std::string>& paths,
	const std::string& path)
{
	for (size_t i = 0; i < paths.size(); ++i) {
		if (paths[i] == path) {
			return true;
		}
	}
	return false;
}

void backup_auto_add_path(std::vector<std::string>& paths,
	const std::string& path)
{
	if (!backup_auto_path_exists(paths, path)) {
		paths.push_back(path);
	}
}

std::string backup_join_relative_path(const std::string& prefix,
	const std::string& path)
{
	if (prefix.empty()) {
		return path;
	}
	if (path.empty()) {
		return prefix;
	}
	return prefix + "/" + path;
}

bool backup_relative_scope_prefix(const std::string& storage_dir,
	const std::string& scope_dir, std::string& prefix, std::string& err)
{
	prefix.clear();
	std::string storage_abs;
	std::string scope_abs;
	if (!canonical_existing_path(storage_dir, storage_abs, err)
		|| !canonical_existing_path(scope_dir, scope_abs, err))
	{
		return false;
	}
	if (storage_abs == scope_abs) {
		return true;
	}
	if (!is_same_or_child_path(storage_abs, scope_abs)) {
		err = "backup sync scope is not inside current storage path";
		return false;
	}
	prefix = scope_abs.substr(storage_abs.size());
	while (!prefix.empty() && (prefix[0] == '/' || prefix[0] == '\\')) {
		prefix.erase(0, 1);
	}
	for (size_t i = 0; i < prefix.size(); ++i) {
		if (prefix[i] == '\\') {
			prefix[i] = '/';
		}
	}
	return true;
}

void backup_auto_merge_event(std::vector<storage_backup_auto_event_t>& events,
	const storage_backup_auto_event_t& event)
{
	for (size_t i = 0; i < events.size(); ++i) {
		if (events[i].upload_dir != event.upload_dir) {
			continue;
		}
		events[i].full_sync = events[i].full_sync || event.full_sync;
		for (size_t j = 0; j < event.sync_paths.size(); ++j) {
			backup_auto_add_path(events[i].sync_paths, event.sync_paths[j]);
		}
		for (size_t j = 0; j < event.delete_paths.size(); ++j) {
			backup_auto_add_path(events[i].delete_paths, event.delete_paths[j]);
		}
		for (size_t j = 0; j < event.move_from_paths.size()
			&& j < event.move_to_paths.size(); ++j)
		{
			events[i].move_from_paths.push_back(event.move_from_paths[j]);
			events[i].move_to_paths.push_back(event.move_to_paths[j]);
		}
		return;
	}
	events.push_back(event);
}

void run_storage_backup_auto_worker()
{
	while (true) {
		std::vector<storage_backup_auto_event_t> events;
		{
			std::lock_guard<webcool::mutex> guard(g_storage_backup_auto_mutex);
			if (g_storage_backup_auto_events.empty()) {
				g_storage_backup_auto_running = false;
				return;
			}
			events.swap(g_storage_backup_auto_events);
		}

		acl_doze(500);
		{
			std::lock_guard<webcool::mutex> guard(g_storage_backup_auto_mutex);
			for (size_t i = 0; i < g_storage_backup_auto_events.size(); ++i) {
				backup_auto_merge_event(events, g_storage_backup_auto_events[i]);
			}
			g_storage_backup_auto_events.clear();
		}

		for (size_t i = 0; i < events.size(); ++i) {
			std::string err;
			if (events[i].full_sync) {
				(void) storage_backup_sync_incremental_blocking(events[i].upload_dir, err);
			} else {
				(void) storage_backup_sync_paths_blocking(events[i].upload_dir,
					events[i].sync_paths, events[i].delete_paths,
					events[i].move_from_paths, events[i].move_to_paths, err);
			}
		}
	}
}

std::string wait_backup_conflict_resolution(storage_migrate_task_t& task,
	const storage_move_item_t& item)
{
	if (!task.conflict_default.empty()) {
		return task.conflict_default;
	}
	storage_migrate_task_t initial_snapshot = current_backup_task_snapshot();
	if (initial_snapshot.id == task.id && !initial_snapshot.conflict_default.empty()) {
		task.conflict_default = initial_snapshot.conflict_default;
		return task.conflict_default;
	}
	task.state = "conflict";
	task.message = "发现同名但内容不同的文件";
	task.conflict_source = item.source;
	task.conflict_target = item.target;
	task.conflict_name = storage_base_name(item.source);
	task.conflict_resolution.clear();
	update_backup_task(task);
	while (true) {
		acl_doze(200);
		storage_migrate_task_t snapshot = current_backup_task_snapshot();
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
			task.message = "备份已暂停";
			update_backup_task(task);
			continue;
		}
		if (task.state == "paused") {
			task.pause_requested = false;
			task.state = "conflict";
			task.message = "发现同名但内容不同的文件";
			update_backup_task(task);
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
			task.message = "继续备份";
			update_backup_task(task);
			return task.conflict_default.empty() ? choice : task.conflict_default;
		}
	}
}

void run_storage_backup_sync_task(storage_migrate_task_t task,
	std::vector<storage_move_item_t> items, std::vector<std::string> target_dirs)
{
	std::lock_guard<webcool::mutex> backup_guard(g_storage_backup_mutex);
	task.state = "running";
	task.message = "正在准备备份同步";
	update_backup_task(task);

	std::string err;
	for (size_t i = 0; i < items.size(); ++i) {
		if (!backup_task_wait_if_paused_or_cancelled(task)) {
			return;
		}
		if (items[i].directory) {
			if (!make_dir_recursive(items[i].target.c_str())) {
				task.state = "failed";
				task.error = "cannot create target directory";
				update_backup_task(task);
				return;
			}
			continue;
		}
		struct stat target_st;
		if (lstat(items[i].target.c_str(), &target_st) == 0) {
			if (!S_ISREG(target_st.st_mode)) {
				task.state = "failed";
				task.error = "target path already exists and is not a file";
				update_backup_task(task);
				return;
			}
			bool same = false;
			if (!files_have_same_data(items[i].source, items[i].target, same, err)) {
				task.state = "failed";
				task.error = err;
				update_backup_task(task);
				return;
			}
			if (same) {
				task.moved_bytes += items[i].size;
				task.moved_files += 1;
				task.message = std::string("正在处理同名文件(相同跳过)：") + items[i].source;
				update_backup_task(task);
				continue;
			}
			const std::string choice = wait_backup_conflict_resolution(task, items[i]);
			if (choice == "cancel") {
				task.state = "cancelled";
				task.message = "备份已停止";
				update_backup_task(task);
				return;
			}
			if (choice == "skip") {
				task.moved_bytes += items[i].size;
				task.moved_files += 1;
				task.message = std::string("正在处理同名文件(跳过)：") + items[i].source;
				update_backup_task(task);
				continue;
			}
			if (choice != "overwrite") {
				task.state = "failed";
				task.error = "invalid conflict resolution";
				update_backup_task(task);
				return;
			}
			task.message = std::string("正在处理同名文件(覆盖)：") + items[i].source;
			update_backup_task(task);
		} else if (errno != ENOENT) {
			task.state = "failed";
			task.error = strerror(errno);
			update_backup_task(task);
			return;
		}
		if (task.message.compare(0, strlen("正在处理同名文件(覆盖)："), "正在处理同名文件(覆盖)：") != 0) {
			task.message = std::string("正在备份：") + items[i].source;
			update_backup_task(task);
		}
		if (!copy_backup_file_with_progress(items[i], task, err)) {
			task.state = "failed";
			task.error = err;
			update_backup_task(task);
			return;
		}
		task.moved_files += 1;
		update_backup_task(task);
	}

	task.message = "正在清理备份目录";
	update_backup_task(task);
	for (size_t i = 0; i < target_dirs.size(); ++i) {
		if (!backup_task_wait_if_paused_or_cancelled(task)) {
			return;
		}
		if (!delete_backup_target_extras(task.source_dir, target_dirs[i], err)) {
			task.state = "failed";
			task.error = err;
			update_backup_task(task);
			return;
		}
	}

	task.state = "done";
	task.message = "备份同步完成";
	task.moved_bytes = task.total_bytes;
	update_backup_task(task);
}

} // namespace admin_internal

using namespace admin_internal;

bool storage_backup_sync_now(const std::string& upload_dir, std::string& err)
{
	if (upload_dir.empty()) {
		return true;
	}
	err.clear();
	bool start_worker = false;
	{
		std::lock_guard<webcool::mutex> guard(g_storage_backup_auto_mutex);
		storage_backup_auto_event_t event;
		event.upload_dir = upload_dir;
		event.full_sync = true;
		backup_auto_merge_event(g_storage_backup_auto_events, event);
		if (!g_storage_backup_auto_running) {
			g_storage_backup_auto_running = true;
			start_worker = true;
		}
	}
	if (start_worker) {
		std::thread(run_storage_backup_auto_worker).detach();
	}
	return true;
}

bool storage_backup_sync_paths(const std::string& upload_dir,
	const std::vector<std::string>& sync_paths,
	const std::vector<std::string>& delete_paths, std::string& err)
{
	std::vector<std::string> move_from_paths;
	std::vector<std::string> move_to_paths;
	return storage_backup_sync_path_moves(upload_dir, sync_paths, delete_paths,
		move_from_paths, move_to_paths, err);
}

bool storage_backup_sync_path_moves(const std::string& upload_dir,
	const std::vector<std::string>& sync_paths,
	const std::vector<std::string>& delete_paths,
	const std::vector<std::string>& move_from_paths,
	const std::vector<std::string>& move_to_paths, std::string& err)
{
	if (upload_dir.empty()) {
		return true;
	}
	err.clear();
	storage_backup_auto_event_t event;
	event.upload_dir = upload_dir;
	event.full_sync = false;
	for (size_t i = 0; i < sync_paths.size(); ++i) {
		backup_auto_add_path(event.sync_paths, sync_paths[i]);
	}
	for (size_t i = 0; i < delete_paths.size(); ++i) {
		backup_auto_add_path(event.delete_paths, delete_paths[i]);
	}
	for (size_t i = 0; i < move_from_paths.size()
		&& i < move_to_paths.size(); ++i)
	{
		event.move_from_paths.push_back(move_from_paths[i]);
		event.move_to_paths.push_back(move_to_paths[i]);
	}
	if (event.sync_paths.empty() && event.delete_paths.empty()
		&& event.move_from_paths.empty())
	{
		return true;
	}
	bool start_worker = false;
	{
		std::lock_guard<webcool::mutex> guard(g_storage_backup_auto_mutex);
		backup_auto_merge_event(g_storage_backup_auto_events, event);
		if (!g_storage_backup_auto_running) {
			g_storage_backup_auto_running = true;
			start_worker = true;
		}
	}
	if (start_worker) {
		std::thread(run_storage_backup_auto_worker).detach();
	}
	return true;
}

bool storage_backup_upload_auto_sync_enabled(const std::string& upload_dir,
	std::string& err)
{
	const std::string storage_dir = runtime_upload_dir_get().empty()
		? upload_dir : runtime_upload_dir_get();
	std::lock_guard<webcool::mutex> settings_guard(g_settings_mutex);
	webcool_settings_t settings;
	if (!load_settings_unlocked(storage_dir, settings, err)) {
		return true;
	}
	return settings.backup_upload_auto_sync;
}

bool AdminStorageBackupAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string err;
	webcool_settings_t settings;
	const char* raw_path = req.getParameter("path");
	const char* raw_remove = req.getParameter("remove");
	const char* raw_toggle = req.getParameter("toggle");
	const char* raw_upload_auto_sync = req.getParameter("upload_auto_sync");
	std::string backup_path = raw_path ? raw_path : "";
	std::string remove_path = raw_remove ? raw_remove : "";
	std::string toggle_path = raw_toggle ? raw_toggle : "";
	int status = 200;
	bool ok = true;
	acl::gofiber_wait_thread([&] {
		std::lock_guard<webcool::mutex> guard(g_settings_mutex);
		if (!load_settings_unlocked(upload_dir, settings, err)) {
			status = 500;
			ok = false;
			return;
		}
		if (raw_upload_auto_sync != NULL) {
			settings.backup_upload_auto_sync =
				parse_bool_text(raw_upload_auto_sync, settings.backup_upload_auto_sync);
		} else if (!remove_path.empty()) {
			std::string remove_dir;
			if (!ensure_storage_target_path(remove_path, remove_dir, err)) {
				status = 400;
				ok = false;
				return;
			}
			std::vector<storage_backup_path_t> next;
			for (size_t i = 0; i < settings.backup_paths.size(); ++i) {
				std::string existing_dir;
				if (ensure_storage_target_path(settings.backup_paths[i].path, existing_dir, err)
					&& existing_dir == remove_dir)
				{
					continue;
				}
				next.push_back(settings.backup_paths[i]);
			}
			settings.backup_paths = next;
			backup_path.clear();
		} else if (!toggle_path.empty()) {
			std::string toggle_dir;
			if (!ensure_storage_target_path(toggle_path, toggle_dir, err)) {
				status = 400;
				ok = false;
				return;
			}
			bool found = false;
			for (size_t i = 0; i < settings.backup_paths.size(); ++i) {
				std::string existing_dir;
				if (ensure_storage_target_path(settings.backup_paths[i].path, existing_dir, err)
					&& existing_dir == toggle_dir)
				{
					settings.backup_paths[i].enabled = !settings.backup_paths[i].enabled;
					found = true;
					break;
				}
			}
			if (!found) {
				err = "backup path not found";
				status = 404;
				ok = false;
				return;
			}
		} else if (!backup_path.empty()) {
			std::string target_dir;
			if (!ensure_storage_target_path(backup_path, target_dir, err)) {
				status = 400;
				ok = false;
				return;
			}
			if (!validate_backup_path_for_settings(upload_dir, target_dir,
				settings.backup_paths, "", err))
			{
				status = 409;
				ok = false;
				return;
			}
			settings.backup_paths.push_back(make_backup_path(target_dir, true));
			backup_path = target_dir;
		} else {
			err = "backup path parameter is required";
			status = 400;
			ok = false;
			return;
		}
		if (!save_settings_unlocked(upload_dir, settings, err)) {
			status = 500;
			ok = false;
			return;
		}
	});
	if (!ok) {
		json_error(res, status, err.c_str(), req.isKeepAlive());
		return true;
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("backup_path", backup_path.c_str());
	root.add_bool("backup_upload_auto_sync", settings.backup_upload_auto_sync);
	acl::json_node& backups = json.create_array();
	root.add_child("backup_paths", backups);
	for (size_t i = 0; i < settings.backup_paths.size(); ++i) {
		acl::json_node& item = backups.add_child(false, true);
		item.add_text("path", settings.backup_paths[i].path.c_str());
		item.add_bool("enabled", settings.backup_paths[i].enabled);
	}
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AdminStorageSwapBackupAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string err;
	std::string source_dir;
	webcool_settings_t settings;
	const char* raw_path = req.getParameter("path");
	std::string selected_path = raw_path ? raw_path : "";
	std::string backup_dir;
	int status = 200;
	bool ok = true;
	acl::gofiber_wait_thread([&] {
		if (!canonical_existing_path(upload_dir, source_dir, err)) {
			status = 500;
			ok = false;
			return;
		}
		{
			std::lock_guard<webcool::mutex> guard(g_settings_mutex);
			if (!load_settings_unlocked(upload_dir, settings, err)) {
				status = 500;
				ok = false;
				return;
			}
		}
		if (settings.backup_paths.empty()) {
			err = "backup path is not set";
			status = 409;
			ok = false;
			return;
		}
		if (selected_path.empty()) {
			selected_path = settings.backup_paths[0].path;
		}
		if (!ensure_storage_target_path(selected_path, backup_dir, err)) {
			status = 400;
			ok = false;
			return;
		}
		bool found_backup = false;
		std::vector<storage_backup_path_t> next_backups;
		for (size_t i = 0; i < settings.backup_paths.size(); ++i) {
			std::string existing_dir;
			if (!ensure_storage_target_path(settings.backup_paths[i].path, existing_dir, err)) {
				status = 400;
				ok = false;
				return;
			}
			if (existing_dir == backup_dir) {
				found_backup = true;
				continue;
			}
			next_backups.push_back(make_backup_path(existing_dir, settings.backup_paths[i].enabled));
		}
		if (!found_backup) {
			err = "backup path not found";
			status = 404;
			ok = false;
			return;
		}
		if (source_dir == backup_dir) {
			err = "backup path cannot be current storage path";
			status = 409;
			ok = false;
			return;
		}
		runtime_upload_dir_set(backup_dir);
		next_backups.push_back(make_backup_path(source_dir, true));
		settings.backup_paths = next_backups;
		{
			std::lock_guard<webcool::mutex> guard(g_settings_mutex);
			if (!save_settings_unlocked(backup_dir, settings, err)) {
				status = 500;
				ok = false;
				return;
			}
		}
	});
	if (!ok) {
		json_error(res, status, err.c_str(), req.isKeepAlive());
		return true;
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", backup_dir.c_str());
	root.add_text("backup_path", settings.backup_paths.empty()
		? "" : settings.backup_paths[0].path.c_str());
	root.add_bool("backup_upload_auto_sync", settings.backup_upload_auto_sync);
	acl::json_node& backups = json.create_array();
	root.add_child("backup_paths", backups);
	for (size_t i = 0; i < settings.backup_paths.size(); ++i) {
		acl::json_node& item = backups.add_child(false, true);
		item.add_text("path", settings.backup_paths[i].path.c_str());
		item.add_bool("enabled", settings.backup_paths[i].enabled);
	}
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AdminStorageBackupSyncAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string source_dir, err;
	std::vector<storage_backup_path_t> backup_paths;
	std::vector<std::string> enabled_backups;
	int status = 200;
	bool ok = true;
	{
		std::lock_guard<webcool::mutex> guard(g_storage_backup_task_mutex);
		if (g_storage_backup_task.state == "queued"
			|| g_storage_backup_task.state == "running"
			|| g_storage_backup_task.state == "paused"
			|| g_storage_backup_task.state == "conflict")
		{
			json_error(res, 409, "storage backup sync is already running", req.isKeepAlive());
			return true;
		}
	}

	storage_migrate_task_t task;
	task.id = make_backup_task_id();
	task.state = "queued";
	task.message = "正在准备备份同步";
	task.source_dir.clear();
	task.target_dir.clear();
	task.cleanup_done = false;
	task.pause_requested = false;
	task.cancel_requested = false;
	task.total_bytes = 0;
	task.moved_bytes = 0;
	task.total_files = 0;
	task.moved_files = 0;
	update_backup_task(task);

	std::vector<storage_move_item_t> items;
	acl::gofiber_wait_thread([&] {
		if (!canonical_existing_path(upload_dir, source_dir, err)) {
			status = 500;
			ok = false;
			return;
		}
		{
			std::lock_guard<webcool::mutex> settings_guard(g_settings_mutex);
			webcool_settings_t settings;
			if (!load_settings_unlocked(upload_dir, settings, err)) {
				status = 500;
				ok = false;
				return;
			}
			backup_paths = settings.backup_paths;
		}
		for (size_t i = 0; i < backup_paths.size(); ++i) {
			if (!backup_paths[i].enabled) {
				continue;
			}
			std::string backup_dir;
			if (!ensure_storage_target_path(backup_paths[i].path, backup_dir, err)) {
				status = 400;
				ok = false;
				return;
			}
			if (!validate_backup_path_for_settings(source_dir, backup_dir,
				backup_paths, backup_paths[i].path, err))
			{
				status = 409;
				ok = false;
				return;
			}
			enabled_backups.push_back(backup_dir);
		}
		if (enabled_backups.empty()) {
			err = "enabled backup path is not set";
			status = 409;
			ok = false;
			return;
		}
		for (size_t i = 0; i < enabled_backups.size(); ++i) {
			if (!collect_backup_sync_items(source_dir, enabled_backups[i], items, err)) {
				status = 500;
				ok = false;
				return;
			}
		}
	});
	if (!ok) {
		task.state = "failed";
		task.error = err;
		update_backup_task(task);
		json_error(res, status, err.c_str(), req.isKeepAlive());
		return true;
	}

	task.state = "queued";
	task.message = "等待备份同步";
	task.source_dir = source_dir;
	task.target_dir = enabled_backups.size() == 1 ? enabled_backups[0] : "multiple backups";
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
	update_backup_task(task);
	go[task, items, enabled_backups] {
		acl::gofiber_wait_thread([task, items, enabled_backups] {
			run_storage_backup_sync_task(task, items, enabled_backups);
		});
	};

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", task.id.c_str());
	root.add_number("total_files", task.total_files);
	root.add_number("total_bytes", task.total_bytes);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AdminStorageBackupSyncProgressAction::run(request_t& req, response_t& res)
{
	storage_migrate_task_t task;
	{
		std::lock_guard<webcool::mutex> guard(g_storage_backup_task_mutex);
		task = g_storage_backup_task;
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
	root.add_bool("pause_requested", task.pause_requested);
	root.add_bool("cancel_requested", task.cancel_requested);
	root.add_number("progress", (long long) progress);
	root.add_number("total_bytes", task.total_bytes);
	root.add_number("moved_bytes", task.moved_bytes);
	root.add_number("total_files", task.total_files);
	root.add_number("moved_files", task.moved_files);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AdminStorageBackupSyncResolveAction::run(request_t& req, response_t& res)
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
		std::lock_guard<webcool::mutex> guard(g_storage_backup_task_mutex);
		if (task_id != NULL && *task_id != '\0' && g_storage_backup_task.id != task_id) {
			json_error(res, 404, "task not found", req.isKeepAlive());
			return true;
		}
		if (g_storage_backup_task.state != "conflict") {
			json_error(res, 409, "backup sync task is not waiting for conflict resolution", req.isKeepAlive());
			return true;
		}
		g_storage_backup_task.conflict_resolution = choice;
		if (choice == "remember-overwrite") {
			g_storage_backup_task.conflict_default = "overwrite";
		} else if (choice == "remember-skip") {
			g_storage_backup_task.conflict_default = "skip";
		} else if (choice == "cancel") {
			g_storage_backup_task.cancel_requested = true;
		}
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("choice", choice.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AdminStorageBackupSyncControlAction::run(request_t& req, response_t& res)
{
	const char* task_id = req.getParameter("task_id");
	const char* action_text = req.getParameter("action");
	std::string action = action_text ? action_text : "";
	if (action != "pause" && action != "resume" && action != "cancel") {
		json_error(res, 400, "invalid backup sync control action", req.isKeepAlive());
		return true;
	}
	{
		std::lock_guard<webcool::mutex> guard(g_storage_backup_task_mutex);
		if (task_id != NULL && *task_id != '\0' && g_storage_backup_task.id != task_id) {
			json_error(res, 404, "task not found", req.isKeepAlive());
			return true;
		}
		if (g_storage_backup_task.state != "queued"
			&& g_storage_backup_task.state != "running"
			&& g_storage_backup_task.state != "paused"
			&& g_storage_backup_task.state != "conflict")
		{
			json_error(res, 409, "backup sync task cannot be controlled", req.isKeepAlive());
			return true;
		}
		if (action == "pause") {
			g_storage_backup_task.pause_requested = true;
		} else if (action == "resume") {
			g_storage_backup_task.pause_requested = false;
		} else if (action == "cancel") {
			g_storage_backup_task.cancel_requested = true;
			g_storage_backup_task.pause_requested = false;
			if (g_storage_backup_task.state == "conflict"
				|| g_storage_backup_task.state == "paused")
			{
				g_storage_backup_task.state = "cancelled";
				g_storage_backup_task.message = "备份已停止";
			}
		}
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("action", action.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

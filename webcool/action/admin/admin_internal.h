#pragma once

#include "../actions.h"
#include "../action_util.h"

#include <mutex>
#include <string>
#include <vector>

namespace action {
namespace admin_internal {

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

struct storage_backup_path_t {
	std::string path;
	bool enabled;
};

struct webcool_settings_t {
	bool local_disk_admin;
	bool local_disk_user;
	bool backup_upload_auto_sync;
	std::vector<storage_backup_path_t> backup_paths;
};

extern std::mutex g_runtime_upload_mutex;
extern std::string g_runtime_upload_dir;
extern std::mutex g_settings_mutex;

extern std::mutex g_storage_migrate_mutex;
extern storage_migrate_task_t g_storage_migrate_task;
extern unsigned long long g_storage_migrate_seq;

extern std::mutex g_storage_backup_task_mutex;
extern storage_migrate_task_t g_storage_backup_task;
extern unsigned long long g_storage_backup_seq;
extern std::mutex g_storage_backup_mutex;

webcool_settings_t default_settings();
//void json_error(response_t& res, int status, const char* msg, bool keep_alive);

std::string canonical_or_empty_path(const std::string& input, std::string& err);
bool canonical_existing_path(const std::string& input, std::string& out, std::string& err);
bool is_absolute_storage_path(const std::string& path);
bool ensure_storage_target_path(const std::string& input, std::string& out, std::string& err);
bool is_same_or_child_path(const std::string& base, const std::string& candidate);
bool init_storage_databases(const std::string& path, std::string& err);

std::string settings_dir(const std::string& upload_dir);
std::string settings_file(const std::string& upload_dir);
bool parse_bool_text(const std::string& text, bool fallback);
bool request_bool_param(request_t& req, const char* name, bool fallback);
bool backup_vector_contains(const std::vector<storage_backup_path_t>& items,
	const std::string& value);
storage_backup_path_t make_backup_path(const std::string& path, bool enabled);
bool load_settings_unlocked(const std::string& upload_dir,
	webcool_settings_t& settings, std::string& err);
bool save_settings_unlocked(const std::string& upload_dir,
	const webcool_settings_t& settings, std::string& err);
bool backup_vector_contains_dir(const std::vector<storage_backup_path_t>& items,
	const std::string& dir);
bool validate_backup_path_for_settings(const std::string& upload_dir,
	const std::string& target_dir, const std::vector<storage_backup_path_t>& existing,
	const std::string& skip_path, std::string& err);

std::string join_storage_path(const std::string& parent, const char* name);
bool record_primary_storage_path(const std::string& upload_dir, std::string& err);
void ensure_shared_folder_for_storage(const std::string& upload_dir);

std::string storage_backup_date_suffix();
bool copy_storage_file_plain(const std::string& source,
	const std::string& dest, std::string& err);
bool copy_storage_path_plain(const std::string& source,
	const std::string& dest, std::string& err);
bool collect_move_items(const std::string& source,
	const std::string& target, std::vector<storage_move_item_t>& items,
	std::string& err);
bool delete_path_recursive_plain(const std::string& path, std::string& err);
bool delete_storage_contents_except_backup(const std::string& path, std::string& err);
bool delete_storage_contents_all(const std::string& path, std::string& err);
std::string storage_base_name(const std::string& path);

std::string make_task_id();
void update_task(const storage_migrate_task_t& task);
storage_migrate_task_t current_storage_task_snapshot();
bool storage_task_wait_if_paused_or_cancelled(storage_migrate_task_t& task);
bool copy_file_with_progress(const storage_move_item_t& item,
	storage_migrate_task_t& task, std::string& err);
bool backup_project_db_files(const std::string& storage_dir,
	bool move_files, std::string& err);
std::string wait_storage_conflict_resolution(storage_migrate_task_t& task,
	const storage_move_item_t& item);
std::string make_backup_task_id();
void update_backup_task(const storage_migrate_task_t& task);
storage_migrate_task_t current_backup_task_snapshot();
bool backup_task_wait_if_paused_or_cancelled(storage_migrate_task_t& task);
bool files_have_same_data(const std::string& left,
	const std::string& right, bool& same, std::string& err);
bool copy_backup_file_with_progress(const storage_move_item_t& item,
	storage_migrate_task_t& task, std::string& err);
std::string wait_backup_conflict_resolution(storage_migrate_task_t& task,
	const storage_move_item_t& item);
void run_storage_backup_sync_task(storage_migrate_task_t task,
	std::vector<storage_move_item_t> items,
	std::vector<std::string> target_dirs);

} // namespace admin_internal
} // namespace action

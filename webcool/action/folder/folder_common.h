#pragma once

#include "../file/file_common.h"

#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace action {

struct folder_node_t {
	std::string name;
	std::string path;
	long long direct_file_count;
	long long direct_folder_count;
	std::vector<folder_node_t> children;
};

extern std::mutex g_folder_lock_mutex;

bool should_skip_self_parent_entry(const char* name);
bool validate_folder_segment(const std::string& name, std::string& err);
bool remove_path_recursive(const std::string& path, std::string& err);
bool copy_path_recursive(const std::string& source, const std::string& dest,
	std::string& err);
bool is_same_or_child_path(const std::string& base_path,
	const std::string& test_path);

bool list_folder_tree(const std::string& upload_dir,
	const std::string& relative_path, folder_node_t& node, std::string& err,
	long long& folder_count, bool show_hidden);
bool list_folder_children(const std::string& upload_dir,
	const std::string& relative_path, folder_node_t& node, std::string& err,
	bool show_hidden);
bool folder_lock_unlocked(const std::string& path,
	const std::map<std::string, std::string>& locks,
	const std::map<std::string, std::string>& unlocked_locks);
void append_folder_json(acl::json& json, acl::json_node& arr,
	const folder_node_t& node, const std::map<std::string, std::string>& locks,
	const std::map<std::string, std::string>& unlocked_locks);

bool alloc_recycle_folder_target(const std::string& upload_dir,
	const std::string& original_name, std::string& recycle_rel,
	std::string& err);

bool validate_lock_password(const std::string& password, std::string& err);
bool load_folder_locks_locked(const std::string& upload_dir,
	std::map<std::string, std::string>& locks, std::string& err);
bool save_folder_locks_locked(const std::string& upload_dir,
	const std::map<std::string, std::string>& locks, std::string& err);
bool find_locked_ancestor_locked(
	const std::map<std::string, std::string>& locks,
	const std::string& relative_path, std::string& locked_path);
bool rename_folder_locks_prefix(const std::string& upload_dir,
	const std::string& old_prefix, const std::string& new_prefix,
	std::string& err);

} // namespace action

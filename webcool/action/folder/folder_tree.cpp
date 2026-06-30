#include "stdafx.h"
#include "folder_common.h"

#include <algorithm>

namespace action {

bool list_folder_tree(const std::string& upload_dir,
	const std::string& relative_path, folder_node_t& node, std::string& err,
	long long& folder_count, bool show_hidden)
{
	err.clear();
	node.direct_file_count = 0;
	node.children.clear();

	std::string full = join_upload_path(upload_dir, relative_path);
	DIR* dir = opendir(full.c_str());
	if (dir == NULL) {
		err = strerror(errno);
		return false;
	}

	std::vector<folder_node_t> children;
	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (should_skip_entry(entry->d_name, show_hidden)) {
			continue;
		}

		std::string child_name(entry->d_name);
		std::string child_rel = relative_path.empty()
			? child_name
			: (relative_path + "/" + child_name);
		std::string child_full = join_upload_path(upload_dir, child_rel);

		struct stat st;
		if (stat(child_full.c_str(), &st) != 0) {
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			folder_node_t child;
			child.name = child_name;
			child.path = child_rel;
			folder_count++;
			if (!list_folder_tree(upload_dir, child_rel, child, err, folder_count, show_hidden)) {
				closedir(dir);
				return false;
			}
			children.push_back(child);
		} else if (S_ISREG(st.st_mode)) {
			if (!is_protected_virtual_path(child_rel)) {
				node.direct_file_count++;
			}
		}
	}

	closedir(dir);
	std::sort(children.begin(), children.end(),
		[](const folder_node_t& a, const folder_node_t& b) {
			return a.name < b.name;
		});
	node.children.swap(children);
	return true;
}

bool folder_lock_unlocked(
	const std::string& path,
	const std::map<std::string, std::string>& locks,
	const std::map<std::string, std::string>& unlocked_locks)
{
	std::map<std::string, std::string>::const_iterator lock_it = locks.find(path);
	if (lock_it == locks.end()) {
		return true;
	}
	std::map<std::string, std::string>::const_iterator unlocked_it = unlocked_locks.find(path);
	return unlocked_it != unlocked_locks.end() && unlocked_it->second == lock_it->second;
}

void append_folder_json(acl::json& json, acl::json_node& arr,
	const folder_node_t& node, const std::map<std::string, std::string>& locks,
	const std::map<std::string, std::string>& unlocked_locks)
{
	const bool locked = locks.find(node.path) != locks.end();
	const bool unlocked = folder_lock_unlocked(node.path, locks, unlocked_locks);
	acl::json_node& item = arr.add_child(false, true);
	item.add_text("name", node.name.c_str());
	item.add_text("path", node.path.c_str());
	item.add_text("parent_path", parent_relative_path(node.path).c_str());
	item.add_bool("locked", locked);
	item.add_number("file_count", locked && !unlocked ? 0 : node.direct_file_count);
	acl::json_node& children = json.create_array();
	item.add_child("children", children);
	if (!locked || unlocked) {
		for (size_t i = 0; i < node.children.size(); ++i) {
			append_folder_json(json, children, node.children[i], locks, unlocked_locks);
		}
	}
	item.add_number("folder_count", locked && !unlocked ? 0 : (long long) node.children.size());
}

} // namespace action

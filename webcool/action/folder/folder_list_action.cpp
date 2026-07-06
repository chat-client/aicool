#include "stdafx.h"
#include "folder_common.h"

#include <algorithm>
#include "common/webcool_mutex.h"
#include <sstream>

namespace action {

static std::string folder_display_name(const std::string& folder)
{
	if (folder.empty()) {
		return "根目录";
	}
	size_t pos = folder.find_last_of('/');
	return pos == std::string::npos ? folder : folder.substr(pos + 1);
}

static bool folder_access_allowed(const std::string& folder,
	const std::map<std::string, std::string>& locks,
	const std::map<std::string, std::string>& unlocked_locks)
{
	if (folder.empty()) {
		return true;
	}
	std::string locked_path;
	if (!find_locked_ancestor_locked(locks, folder, locked_path)) {
		return true;
	}
	return folder_lock_unlocked(locked_path, locks, unlocked_locks);
}

static bool root_has_shared_folder(const folder_node_t& root_node)
{
	for (size_t i = 0; i < root_node.children.size(); ++i) {
		if (root_node.children[i].path == shared_folder_name()) {
			return true;
		}
	}
	return false;
}

bool FolderListAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	if (!make_dir_recursive(upload_dir.c_str())) {
		json_error(res, 500, "cannot access upload dir", req.isKeepAlive());
		return true;
	}

	std::string err;
	if (!ensure_fixed_upload_folders(upload_dir, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	const bool show_hidden = request_bool_param(req, "show_hidden");
	std::string folder;
	const char* folder_text = req.getParameter("folder");
	if (folder_text != NULL && *folder_text != '\0') {
		if (!normalize_relative_path(folder_text, folder, err, false)) {
			json_error(res, 400, err.c_str(), req.isKeepAlive());
			return true;
		}
	}

	std::map<std::string, std::string> locks;
	{
		std::lock_guard<webcool::mutex> guard(g_folder_lock_mutex);
		if (!load_folder_locks_locked(upload_dir, locks, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	}
	std::map<std::string, std::string> unlocked_locks;
	long long unlock_count = 0;
	const char* unlock_count_text = req.getParameter("unlock_count");
	if (unlock_count_text != NULL && *unlock_count_text != '\0') {
		unlock_count = atoll(unlock_count_text);
		if (unlock_count < 0 || unlock_count > 100) {
			json_error(res, 400, "invalid unlock count", req.isKeepAlive());
			return true;
		}
	}
	for (long long i = 0; i < unlock_count; ++i) {
		std::ostringstream path_key;
		path_key << "unlock_path_" << i;
		std::ostringstream password_key;
		password_key << "unlock_password_" << i;
		const char* path_text = req.getParameter(path_key.str().c_str());
		const char* password_text = req.getParameter(password_key.str().c_str());
		if (path_text == NULL || password_text == NULL) {
			continue;
		}
		std::string unlock_path;
		if (!normalize_relative_path(path_text, unlock_path, err, false)) {
			json_error(res, 400, err.c_str(), req.isKeepAlive());
			return true;
		}
		unlocked_locks[unlock_path] = password_text;
	}

	folder_node_t root_node;
	root_node.name = folder_display_name(folder);
	root_node.path = folder;
	root_node.direct_file_count = 0;
	root_node.direct_folder_count = 0;
	if (folder_access_allowed(folder, locks, unlocked_locks)) {
		if (!list_folder_children(upload_dir, folder, root_node, err, show_hidden)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	}
	if (folder.empty() && !root_has_shared_folder(root_node)) {
		folder_node_t shared_node;
		shared_node.name = shared_folder_name();
		shared_node.path = shared_folder_name();
		shared_node.direct_file_count = 0;
		shared_node.direct_folder_count = 0;
		if (!list_folder_children(upload_dir, shared_folder_name(), shared_node, err, show_hidden)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		root_node.children.push_back(shared_node);
		std::sort(root_node.children.begin(), root_node.children.end(),
			[](const folder_node_t& a, const folder_node_t& b) {
				return a.name < b.name;
			});
		root_node.direct_folder_count = (long long) root_node.children.size();
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("root_name", root_node.name.c_str());
	root.add_text("root_path", root_node.path.c_str());
	root.add_number("file_count", root_node.direct_file_count);
	root.add_number("count", root_node.direct_folder_count);
	acl::json_node& folders = json.create_array();
	root.add_child("folders", folders);
	for (size_t i = 0; i < root_node.children.size(); ++i) {
		append_folder_json(json, folders, root_node.children[i], locks, unlocked_locks);
	}
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

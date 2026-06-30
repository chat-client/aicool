#include "stdafx.h"
#include "folder_common.h"

#include <algorithm>
#include <mutex>
#include <sstream>

namespace action {

bool FolderListAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	if (!make_dir_recursive(upload_dir.c_str())) {
		json_error(res, 500, "cannot access upload dir", req.isKeepAlive());
		return true;
	}

	std::string err;
	if (!ensure_shared_upload_dir(err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	const bool show_hidden = request_bool_param(req, "show_hidden");
	folder_node_t root_node;
	root_node.name = "根目录";
	root_node.path.clear();
	long long folder_count = 0;
	if (!list_folder_tree(upload_dir, std::string(), root_node, err, folder_count, show_hidden)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	bool has_shared_folder = false;
	for (size_t i = 0; i < root_node.children.size(); ++i) {
		if (root_node.children[i].path == shared_folder_name()) {
			has_shared_folder = true;
			break;
		}
	}
	if (!has_shared_folder) {
		folder_node_t shared_node;
		shared_node.name = shared_folder_name();
		shared_node.path = shared_folder_name();
		folder_count++;
		if (!list_folder_tree(upload_dir, shared_folder_name(), shared_node,
			err, folder_count, show_hidden))
		{
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		root_node.children.push_back(shared_node);
		std::sort(root_node.children.begin(), root_node.children.end(),
			[](const folder_node_t& a, const folder_node_t& b) {
				return a.name < b.name;
			});
	}

	std::map<std::string, std::string> locks;
	{
		std::lock_guard<std::mutex> guard(g_folder_lock_mutex);
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

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("root_name", root_node.name.c_str());
	root.add_text("root_path", "");
	root.add_number("file_count", root_node.direct_file_count);
	root.add_number("count", folder_count);
	acl::json_node& folders = json.create_array();
	root.add_child("folders", folders);
	for (size_t i = 0; i < root_node.children.size(); ++i) {
		append_folder_json(json, folders, root_node.children[i], locks, unlocked_locks);
	}
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

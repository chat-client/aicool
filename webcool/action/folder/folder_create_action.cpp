#include "stdafx.h"
#include "folder_common.h"

namespace action {

bool FolderCreateAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string parent_path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("parent"), parent_path, err, true)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(parent_path)) {
		json_error(res, 409, "cannot create folder inside recycle folder", req.isKeepAlive());
		return true;
	}
	bool lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, parent_path,
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!lock_allowed) {
		json_error(res, 403, "folder is locked", req.isKeepAlive());
		return true;
	}

	std::string name = req.getParameter("name") ? req.getParameter("name") : "";
	if (!validate_folder_segment(name, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	if (!make_dir_recursive(upload_dir.c_str())) {
		json_error(res, 500, "cannot access upload dir", req.isKeepAlive());
		return true;
	}
	if (!ensure_fixed_upload_folders(upload_dir, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!parent_path.empty() && !upload_directory_exists(upload_dir, parent_path)) {
		json_error(res, 404, "parent folder not found", req.isKeepAlive());
		return true;
	}

	const std::string new_path = parent_path.empty() ? name : (parent_path + "/" + name);
	const std::string full = join_upload_path(upload_dir, new_path);
	struct stat st;
	if (stat(full.c_str(), &st) == 0) {
		if (!S_ISDIR(st.st_mode)) {
			json_error(res, 409, "target path already exists", req.isKeepAlive());
			return true;
		}

		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("name", name.c_str());
		root.add_text("path", new_path.c_str());
		root.add_text("parent_path", parent_path.c_str());
		root.add_bool("existed", true);
		root.add_text("message", "folder already exists");
		return sendJson(res, 200, root, req.isKeepAlive());
	}
	if (!make_dir(full.c_str())) {
		json_error(res, 500, "create folder failed", req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("name", name.c_str());
	root.add_text("path", new_path.c_str());
	root.add_text("parent_path", parent_path.c_str());
	root.add_text("message", "folder created");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

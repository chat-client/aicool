#include "stdafx.h"
#include "folder_common.h"

namespace action {

bool FolderRenameAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string old_path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("path"), old_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(old_path)) {
		json_error(res, 409, "recycle folder is protected", req.isKeepAlive());
		return true;
	}
	if (is_shared_root_path(old_path)) {
		json_error(res, 409, "shared folder is protected", req.isKeepAlive());
		return true;
	}
	if (is_shared_fixed_subfolder_path(old_path)) {
		json_error(res, 409, "shared fixed folder is protected", req.isKeepAlive());
		return true;
	}
	if (is_root_fixed_folder_path(old_path)) {
		json_error(res, 409, "root fixed folder is protected", req.isKeepAlive());
		return true;
	}
	bool lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, old_path,
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

	std::string new_name = req.getParameter("name") ? req.getParameter("name") : "";
	if (!validate_folder_segment(new_name, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!upload_directory_exists(upload_dir, old_path)) {
		json_error(res, 404, "folder not found", req.isKeepAlive());
		return true;
	}

	const std::string parent_path = parent_relative_path(old_path);
	const std::string new_path = parent_path.empty() ? new_name : (parent_path + "/" + new_name);
	if (is_recycle_root_path(new_path)) {
		json_error(res, 409, "recycle folder name is protected", req.isKeepAlive());
		return true;
	}
	if (is_shared_fixed_subfolder_path(new_path)) {
		json_error(res, 409, "shared fixed folder name is protected", req.isKeepAlive());
		return true;
	}
	if (is_root_fixed_folder_path(new_path)) {
		json_error(res, 409, "root fixed folder name is protected", req.isKeepAlive());
		return true;
	}
	if (new_path == old_path) {
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("path", old_path.c_str());
		root.add_text("message", "folder unchanged");
		return sendJson(res, 200, root, req.isKeepAlive());
	}
	if (upload_directory_exists(upload_dir, new_path)) {
		json_error(res, 409, "folder already exists", req.isKeepAlive());
		return true;
	}

	std::string old_full = join_upload_path(upload_dir, old_path);
	std::string new_full = join_upload_path(upload_dir, new_path);
	if (::rename(old_full.c_str(), new_full.c_str()) != 0) {
		json_error(res, 500, "rename folder failed", req.isKeepAlive());
		return true;
	}

	if (!rename_folder_locks_prefix(upload_dir, old_path, new_path, err)) {
		::rename(new_full.c_str(), old_full.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	bool tag_updated = false;
	std::string rename_err;
	if (!tag_rename_folder_prefix(upload_dir, old_path, new_path, rename_err)) {
		std::string rollback_err;
		rename_folder_locks_prefix(upload_dir, new_path, old_path, rollback_err);
		::rename(new_full.c_str(), old_full.c_str());
		json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
		return true;
	}
	tag_updated = true;
	if (!video_resume_rename_folder_prefix(upload_dir, old_path, new_path, rename_err)) {
		if (tag_updated) {
			std::string rollback_err;
			tag_rename_folder_prefix(upload_dir, new_path, old_path, rollback_err);
		}
		std::string lock_rollback_err;
		rename_folder_locks_prefix(upload_dir, new_path, old_path, lock_rollback_err);
		::rename(new_full.c_str(), old_full.c_str());
		json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", new_path.c_str());
	root.add_text("old_path", old_path.c_str());
	root.add_text("name", new_name.c_str());
	root.add_text("message", "folder renamed");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

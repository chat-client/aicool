#include "stdafx.h"
#include "folder_common.h"

namespace action {

bool FolderMoveAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string source_path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("path"), source_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(source_path)) {
		json_error(res, 409, "recycle folder is protected", req.isKeepAlive());
		return true;
	}
	if (is_shared_root_path(source_path)) {
		json_error(res, 409, "shared folder is protected", req.isKeepAlive());
		return true;
	}
	if (is_shared_fixed_subfolder_path(source_path)) {
		json_error(res, 409, "shared fixed folder is protected", req.isKeepAlive());
		return true;
	}
	bool source_lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, source_path,
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		source_lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!source_lock_allowed) {
		json_error(res, 403, "source folder is locked", req.isKeepAlive());
		return true;
	}

	std::string target_parent;
	if (!normalize_relative_path(req.getParameter("folder"), target_parent, err, true)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(target_parent)) {
		json_error(res, 409, "cannot move folder into recycle folder", req.isKeepAlive());
		return true;
	}
	bool target_lock_allowed = false;
	if (!folder_lock_path_allows(upload_dir, target_parent,
		req.getParameter("target_folder_password") ? req.getParameter("target_folder_password") : "",
		target_lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!target_lock_allowed) {
		json_error(res, 403, "target folder is locked", req.isKeepAlive());
		return true;
	}

	if (!upload_directory_exists(upload_dir, source_path)) {
		json_error(res, 404, "folder not found", req.isKeepAlive());
		return true;
	}
	if (!target_parent.empty() && !upload_directory_exists(upload_dir, target_parent)) {
		json_error(res, 404, "target folder not found", req.isKeepAlive());
		return true;
	}
	if (is_same_or_child_path(source_path, target_parent)) {
		json_error(res, 409, "cannot move folder into itself or its child", req.isKeepAlive());
		return true;
	}

	const std::string folder_name = base_name_from_relative_path(source_path);
	const std::string target_path = target_parent.empty()
		? folder_name
		: (target_parent + "/" + folder_name);
	if (target_path == source_path) {
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("path", source_path.c_str());
		root.add_text("message", "folder unchanged");
		return sendJson(res, 200, root, req.isKeepAlive());
	}
	if (upload_directory_exists(upload_dir, target_path)) {
		json_error(res, 409, "target folder already exists", req.isKeepAlive());
		return true;
	}

	const std::string source_full = join_upload_path(upload_dir, source_path);
	const std::string target_full = join_upload_path(upload_dir, target_path);
	if (::rename(source_full.c_str(), target_full.c_str()) != 0) {
		json_error(res, 500, "move folder failed", req.isKeepAlive());
		return true;
	}

	if (!rename_folder_locks_prefix(upload_dir, source_path, target_path, err)) {
		::rename(target_full.c_str(), source_full.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	bool tag_updated = false;
	std::string rename_err;
	if (!tag_rename_folder_prefix(upload_dir, source_path, target_path, rename_err)) {
		std::string lock_rollback_err;
		rename_folder_locks_prefix(upload_dir, target_path, source_path, lock_rollback_err);
		::rename(target_full.c_str(), source_full.c_str());
		json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
		return true;
	}
	tag_updated = true;
	if (!video_resume_rename_folder_prefix(upload_dir, source_path, target_path, rename_err)) {
		if (tag_updated) {
			std::string rollback_err;
			tag_rename_folder_prefix(upload_dir, target_path, source_path, rollback_err);
		}
		std::string lock_rollback_err;
		rename_folder_locks_prefix(upload_dir, target_path, source_path, lock_rollback_err);
		::rename(target_full.c_str(), source_full.c_str());
		json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", target_path.c_str());
	root.add_text("old_path", source_path.c_str());
	root.add_text("parent_path", target_parent.c_str());
	root.add_text("name", folder_name.c_str());
	root.add_text("message", "folder moved");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

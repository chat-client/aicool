#include "stdafx.h"
#include "folder_common.h"

#include <cstring>

namespace action {

bool FolderCopyAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string source_path;
	std::string err;
	const bool overwrite = req.getParameter("overwrite") != NULL
		&& strcmp(req.getParameter("overwrite"), "1") == 0;
	if (!normalize_relative_path(req.getParameter("path"), source_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (source_path.empty()) {
		json_error(res, 409, "root folder cannot be copied", req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(source_path)) {
		json_error(res, 409, "recycle folder is protected", req.isKeepAlive());
		return true;
	}
	if (is_root_fixed_folder_path(source_path)) {
		json_error(res, 409, "root fixed folder is protected", req.isKeepAlive());
		return true;
	}
	if (!upload_directory_exists(upload_dir, source_path)) {
		json_error(res, 404, "folder not found", req.isKeepAlive());
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
		json_error(res, 409, "cannot copy folder into recycle folder", req.isKeepAlive());
		return true;
	}
	if (!target_parent.empty() && !upload_directory_exists(upload_dir, target_parent)) {
		json_error(res, 404, "target folder not found", req.isKeepAlive());
		return true;
	}
	if (is_same_or_child_path(source_path, target_parent)) {
		json_error(res, 409, "cannot copy folder into itself or its child",
			req.isKeepAlive());
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

	const std::string folder_name = base_name_from_relative_path(source_path);
	const std::string target_path = target_parent.empty()
		? folder_name
		: (target_parent + "/" + folder_name);
	if (target_path == source_path) {
		json_error(res, 409, "source and destination are the same", req.isKeepAlive());
		return true;
	}
	if (is_shared_fixed_subfolder_path(target_path)) {
		json_error(res, 409, "shared fixed folder is protected", req.isKeepAlive());
		return true;
	}
	if (is_root_fixed_folder_path(target_path)) {
		json_error(res, 409, "root fixed folder is protected", req.isKeepAlive());
		return true;
	}

	const std::string source_full = join_upload_path(upload_dir, source_path);
	const std::string target_full = join_upload_path(upload_dir, target_path);
	struct stat target_st;
	if (lstat(target_full.c_str(), &target_st) == 0) {
		if (!overwrite) {
			json_error(res, 409, "target already contains a path with same name",
				req.isKeepAlive());
			return true;
		}
		if (S_ISDIR(target_st.st_mode)) {
			bool dest_lock_allowed = false;
			if (!folder_lock_path_allows(upload_dir, target_path,
				req.getParameter("target_folder_password") ? req.getParameter("target_folder_password") : "",
				dest_lock_allowed, locked_path, err))
			{
				json_error(res, 500, err.c_str(), req.isKeepAlive());
				return true;
			}
			if (!dest_lock_allowed) {
				json_error(res, 403, "destination folder is locked", req.isKeepAlive());
				return true;
			}
		} else {
			bool dest_file_allowed = false;
			if (!file_lock_path_allows(upload_dir, remote_file_lock_key(target_path),
				req.getParameter("file_password") ? req.getParameter("file_password") : "",
				dest_file_allowed, err))
			{
				json_error(res, 500, err.c_str(), req.isKeepAlive());
				return true;
			}
			if (!dest_file_allowed) {
				json_error(res, 403, "destination file is locked", req.isKeepAlive());
				return true;
			}
		}
		if (!remove_path_recursive(target_full, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	} else if (errno != ENOENT) {
		json_error(res, 500, strerror(errno), req.isKeepAlive());
		return true;
	}

	if (req.getParameter("async") != NULL && strcmp(req.getParameter("async"), "1") == 0) {
		const std::string task_id = start_remote_copy_task(source_full, target_full,
			target_path, true, upload_dir);
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("task_id", task_id.c_str());
		root.add_text("path", target_path.c_str());
		root.add_text("folder_path", target_parent.c_str());
		root.add_bool("directory", true);
		root.add_text("message", "copy task started");
		return sendJson(res, 200, root, req.isKeepAlive());
	}
	if (!copy_path_recursive(source_full, target_full, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", target_path.c_str());
	root.add_text("source", source_path.c_str());
	root.add_text("folder_path", target_parent.c_str());
	root.add_bool("overwritten", overwrite);
	root.add_text("message", "folder copied");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

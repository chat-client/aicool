#include "stdafx.h"
#include "file_common.h"

namespace action {

bool CopyFileAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string file_path;
	std::string err;
	const bool overwrite = req.getParameter("overwrite") != NULL
		&& strcmp(req.getParameter("overwrite"), "1") == 0;
	if (!normalize_relative_path(req.getParameter("file"), file_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_protected_virtual_path(file_path)) {
		json_error(res, 403, "system file is protected", req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(file_path)) {
		json_error(res, 409, "recycle file is protected", req.isKeepAlive());
		return true;
	}
	if (!upload_regular_file_exists(upload_dir, file_path)) {
		json_error(res, 404, "source file not found", req.isKeepAlive());
		return true;
	}

	std::string target_folder;
	if (!normalize_relative_path(req.getParameter("folder"), target_folder, err, true)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(target_folder)) {
		json_error(res, 409, "cannot copy file into recycle folder", req.isKeepAlive());
		return true;
	}
	if (!target_folder.empty() && !upload_directory_exists(upload_dir, target_folder)) {
		json_error(res, 404, "target folder not found", req.isKeepAlive());
		return true;
	}

	bool source_lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, parent_relative_path(file_path),
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

	bool file_lock_allowed = false;
	if (!file_lock_path_allows(upload_dir, remote_file_lock_key(file_path),
		req.getParameter("file_password") ? req.getParameter("file_password") : "",
		file_lock_allowed, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!file_lock_allowed) {
		json_error(res, 403, "file is locked", req.isKeepAlive());
		return true;
	}

	bool target_lock_allowed = false;
	if (!folder_lock_path_allows(upload_dir, target_folder,
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

	const std::string target_path = target_folder.empty()
		? base_name_from_relative_path(file_path)
		: (target_folder + "/" + base_name_from_relative_path(file_path));
	if (is_protected_virtual_path(target_path)) {
		json_error(res, 403, "system file is protected", req.isKeepAlive());
		return true;
	}
	if (target_path == file_path) {
		json_error(res, 409, "source and destination are the same", req.isKeepAlive());
		return true;
	}

	const std::string from_full = join_upload_path(upload_dir, file_path);
	const std::string to_full = join_upload_path(upload_dir, target_path);
	struct stat target_st;
	if (lstat(to_full.c_str(), &target_st) == 0) {
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
		if (!remove_upload_path_recursive(to_full, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	} else if (errno != ENOENT) {
		json_error(res, 500, strerror(errno), req.isKeepAlive());
		return true;
	}

	struct stat source_st;
	if (stat(from_full.c_str(), &source_st) != 0 || !S_ISREG(source_st.st_mode)) {
		json_error(res, 404, "source file not found", req.isKeepAlive());
		return true;
	}
	if (req.getParameter("async") != NULL && strcmp(req.getParameter("async"), "1") == 0) {
		const std::string task_id = start_remote_copy_task(from_full, to_full,
			target_path, false, upload_dir);
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("task_id", task_id.c_str());
		root.add_text("path", target_path.c_str());
		root.add_text("folder_path", target_folder.c_str());
		root.add_bool("directory", false);
		root.add_text("message", "copy task started");
		return sendJson(res, 200, root, req.isKeepAlive());
	}
	if (!copy_regular_file_plain(from_full, to_full, source_st.st_mode, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("file", file_path.c_str());
	root.add_text("path", target_path.c_str());
	root.add_text("folder_path", target_folder.c_str());
	root.add_bool("overwritten", overwrite);
	root.add_text("message", "file copied");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool send_remote_copy_task_snapshot(response_t& res,
	const remote_copy_task_snapshot_t& task, bool keep_alive)
{
	const double progress = task.total_bytes > 0
		? (double) task.copied_bytes * 100.0 / (double) task.total_bytes
		: (task.state == "done" ? 100.0 : 0.0);
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", task.id.c_str());
	root.add_text("state", task.state.c_str());
	root.add_text("message", task.message.c_str());
	root.add_text("error", task.error.c_str());
	root.add_text("path", task.path.c_str());
	root.add_number("total_bytes", task.total_bytes);
	root.add_number("copied_bytes", task.copied_bytes);
	root.add_number("progress", (long long) progress);
	root.add_bool("directory", task.directory);
	root.add_bool("cancel_requested", task.cancel_requested);
	return sendJson(res, 200, root, keep_alive);
}

bool RemoteCopyProgressAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	(void) upload_dir;
	const char* task_id = req.getParameter("task_id");
	if (task_id == NULL || *task_id == '\0') {
		json_error(res, 400, "task_id is required", req.isKeepAlive());
		return true;
	}
	remote_copy_task_snapshot_t task;
	if (!remote_copy_task_snapshot(task_id, task)) {
		json_error(res, 404, "copy task not found", req.isKeepAlive());
		return true;
	}
	return send_remote_copy_task_snapshot(res, task, req.isKeepAlive());
}

bool RemoteCopyCancelAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	(void) upload_dir;
	const char* task_id = req.getParameter("task_id");
	if (task_id == NULL || *task_id == '\0') {
		json_error(res, 400, "task_id is required", req.isKeepAlive());
		return true;
	}
	remote_copy_task_snapshot_t task;
	if (!remote_copy_task_cancel(task_id, task)) {
		json_error(res, 404, "copy task not found", req.isKeepAlive());
		return true;
	}
	return send_remote_copy_task_snapshot(res, task, req.isKeepAlive());
}

} // namespace action

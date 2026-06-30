#include "stdafx.h"
#include "file_common.h"

namespace action {

bool RenameFileAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string file_path;
	std::string err;
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

	std::string new_name = req.getParameter("name") ? req.getParameter("name") : "";
	std::string normalized_name;
	if (!normalize_relative_path(new_name.c_str(), normalized_name, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (normalized_name != base_name_from_relative_path(normalized_name)) {
		json_error(res, 400, "file name must not contain path separator", req.isKeepAlive());
		return true;
	}
	if (is_recycle_root_path(normalized_name)) {
		json_error(res, 409, "recycle folder name is protected", req.isKeepAlive());
		return true;
	}

	bool source_lock_allowed = false;
	std::string locked_path;
	const std::string parent_path = parent_relative_path(file_path);
	if (!folder_lock_path_allows(upload_dir, parent_path,
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

	const std::string target_path = parent_path.empty()
		? normalized_name
		: (parent_path + "/" + normalized_name);
	if (is_protected_virtual_path(target_path)) {
		json_error(res, 403, "system file is protected", req.isKeepAlive());
		return true;
	}
	if (target_path == file_path) {
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("file", file_path.c_str());
		root.add_text("path", target_path.c_str());
		root.add_text("message", "file unchanged");
		return sendJson(res, 200, root, req.isKeepAlive());
	}
	if (upload_regular_file_exists(upload_dir, target_path)
		|| upload_directory_exists(upload_dir, target_path))
	{
		json_error(res, 409, "target path already exists", req.isKeepAlive());
		return true;
	}

	const std::string from_full = join_upload_path(upload_dir, file_path);
	const std::string to_full = join_upload_path(upload_dir, target_path);
	if (::rename(from_full.c_str(), to_full.c_str()) != 0) {
		json_error(res, 500, "rename file failed", req.isKeepAlive());
		return true;
	}

	std::string rename_err;
	if (!tag_rename_file(upload_dir, file_path, target_path, rename_err)
		|| !video_resume_rename_file(upload_dir, file_path, target_path, rename_err)
		|| !file_lock_rename_key(upload_dir, remote_file_lock_key(file_path),
			remote_file_lock_key(target_path), rename_err))
	{
		(void) ::rename(to_full.c_str(), from_full.c_str());
		json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("file", file_path.c_str());
	root.add_text("path", target_path.c_str());
	root.add_text("folder_path", parent_path.c_str());
	root.add_text("name", normalized_name.c_str());
	root.add_text("message", "file renamed");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

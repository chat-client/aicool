#include "stdafx.h"
#include "file_common.h"

namespace action {

bool RestoreAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string file_path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("file"), file_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!is_recycle_file_path(file_path)) {
		json_error(res, 400, "restore only supports recycle files", req.isKeepAlive());
		return true;
	}
	bool restore_is_dir = upload_directory_exists(upload_dir, file_path);
	if (!restore_is_dir && !upload_regular_file_exists(upload_dir, file_path)) {
		std::string resolved_path;
		if (!resolve_recycle_item_path(upload_dir, file_path, resolved_path,
			restore_is_dir))
		{
			json_error(res, 404, "recycle item not found", req.isKeepAlive());
			return true;
		}
		file_path = resolved_path;
	}

	recycle_record_t rec;
	bool found = false;
	if (!get_recycle_record(upload_dir, file_path, rec, found, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!found) {
		json_error(res, 404, "recycle record not found", req.isKeepAlive());
		return true;
	}

	std::string target_path;
	if (!resolve_restore_target_path(upload_dir, rec, target_path, err)) {
		json_error(res, 409, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_protected_virtual_path(target_path)) {
		json_error(res, 403, "system file is protected", req.isKeepAlive());
		return true;
	}

	const std::string target_parent = parent_relative_path(target_path);
	bool restore_lock_allowed = false;
	std::string restore_locked_path;
	if (!folder_lock_path_allows(upload_dir, target_parent,
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		restore_lock_allowed, restore_locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!restore_lock_allowed) {
		json_error(res, 403, "target folder is locked", req.isKeepAlive());
		return true;
	}
	if (!target_parent.empty()) {
		const std::string parent_full = join_upload_path(upload_dir, target_parent);
		if (!make_dir_recursive(parent_full.c_str())) {
			json_error(res, 500, "cannot restore target folder", req.isKeepAlive());
			return true;
		}
	}

	const std::string from_full = join_upload_path(upload_dir, file_path);
	const std::string to_full = join_upload_path(upload_dir, target_path);
	if (::rename(from_full.c_str(), to_full.c_str()) != 0) {
		json_error(res, 500, restore_is_dir ? "restore folder failed" : "restore file failed", req.isKeepAlive());
		return true;
	}

	bool tag_renamed = false;
	if (!(restore_is_dir
		? tag_rename_folder_prefix(upload_dir, file_path, target_path, err)
		: tag_rename_file(upload_dir, file_path, target_path, err)))
	{
		(void) ::rename(to_full.c_str(), from_full.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	tag_renamed = true;

	const bool rename_ok = restore_is_dir
		? (video_resume_rename_folder_prefix(upload_dir, file_path, target_path, err)
			&& folder_lock_rename_prefix(upload_dir, file_path, target_path, err))
		: (video_resume_rename_file(upload_dir, file_path, target_path, err)
			&& file_lock_rename_key(upload_dir, remote_file_lock_key(file_path),
				remote_file_lock_key(target_path), err));
	if (!rename_ok)
	{
		if (tag_renamed) {
			std::string rollback_err;
			if (restore_is_dir) {
				tag_rename_folder_prefix(upload_dir, target_path, file_path, rollback_err);
			} else {
				tag_rename_file(upload_dir, target_path, file_path, rollback_err);
			}
		}
		(void) ::rename(to_full.c_str(), from_full.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	if (!delete_recycle_record(upload_dir, file_path, err)) {
		std::string rollback_err;
		if (restore_is_dir) {
			folder_lock_rename_prefix(upload_dir, target_path, file_path, rollback_err);
			video_resume_rename_folder_prefix(upload_dir, target_path, file_path, rollback_err);
			tag_rename_folder_prefix(upload_dir, target_path, file_path, rollback_err);
		} else {
			file_lock_rename_key(upload_dir, remote_file_lock_key(target_path),
				remote_file_lock_key(file_path), rollback_err);
			video_resume_rename_file(upload_dir, target_path, file_path, rollback_err);
			tag_rename_file(upload_dir, target_path, file_path, rollback_err);
		}
		(void) ::rename(to_full.c_str(), from_full.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	{
		std::string sync_err;
		std::vector<std::string> sync_paths;
		std::vector<std::string> delete_paths;
		std::vector<std::string> move_from_paths;
		std::vector<std::string> move_to_paths;
		move_from_paths.push_back(file_path);
		move_to_paths.push_back(target_path);
		sync_paths.push_back(".recycle_bin.db");
		sync_paths.push_back(".recycle_bin.db-wal");
		sync_paths.push_back(".recycle_bin.db-shm");
		sync_paths.push_back(".recycle_bin.db-journal");
		sync_paths.push_back(".tag_catalog.db");
		sync_paths.push_back(".tag_catalog.db-wal");
		sync_paths.push_back(".tag_catalog.db-shm");
		sync_paths.push_back(".tag_catalog.db-journal");
		sync_paths.push_back(".video_resume.db");
		sync_paths.push_back(".video_resume.db-wal");
		sync_paths.push_back(".video_resume.db-shm");
		sync_paths.push_back(".video_resume.db-journal");
		sync_paths.push_back(".file_locks.txt");
		sync_paths.push_back(".folder_locks.txt");
		(void) storage_backup_sync_path_moves(upload_dir, sync_paths, delete_paths,
			move_from_paths, move_to_paths, sync_err);
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("file", file_path.c_str());
	root.add_text("path", target_path.c_str());
	root.add_bool("directory", restore_is_dir);
	root.add_text("message", "restored");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

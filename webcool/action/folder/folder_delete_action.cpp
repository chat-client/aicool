#include "stdafx.h"
#include "folder_common.h"

namespace action {

bool FolderDeleteAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("path"), path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(path)) {
		json_error(res, 409, "recycle folder is protected", req.isKeepAlive());
		return true;
	}
	if (is_shared_root_path(path)) {
		json_error(res, 409, "shared folder is protected", req.isKeepAlive());
		return true;
	}
	bool lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, path,
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
	if (!upload_directory_exists(upload_dir, path)) {
		json_error(res, 404, "folder not found", req.isKeepAlive());
		return true;
	}

	const std::string folder_name = base_name_from_relative_path(path);
	std::string recycle_path;
	if (!alloc_recycle_folder_target(upload_dir, folder_name, recycle_path, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	const std::string from_full = join_upload_path(upload_dir, path);
	const std::string to_full = join_upload_path(upload_dir, recycle_path);
	if (::rename(from_full.c_str(), to_full.c_str()) != 0) {
		json_error(res, 500, "move folder to recycle bin failed", req.isKeepAlive());
		return true;
	}

	if (!rename_folder_locks_prefix(upload_dir, path, recycle_path, err)) {
		(void) ::rename(to_full.c_str(), from_full.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	bool tag_updated = false;
	std::string rename_err;
	if (!tag_rename_folder_prefix(upload_dir, path, recycle_path, rename_err)) {
		std::string rollback_err;
		rename_folder_locks_prefix(upload_dir, recycle_path, path, rollback_err);
		(void) ::rename(to_full.c_str(), from_full.c_str());
		json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
		return true;
	}
	tag_updated = true;
	if (!video_resume_rename_folder_prefix(upload_dir, path, recycle_path, rename_err)) {
		if (tag_updated) {
			std::string rollback_err;
			tag_rename_folder_prefix(upload_dir, recycle_path, path, rollback_err);
		}
		std::string lock_rollback_err;
		rename_folder_locks_prefix(upload_dir, recycle_path, path, lock_rollback_err);
		(void) ::rename(to_full.c_str(), from_full.c_str());
		json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!recycle_bin_insert_record(upload_dir, recycle_path, path, err)) {
		std::string rollback_err;
		video_resume_rename_folder_prefix(upload_dir, recycle_path, path, rollback_err);
		tag_rename_folder_prefix(upload_dir, recycle_path, path, rollback_err);
		rename_folder_locks_prefix(upload_dir, recycle_path, path, rollback_err);
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
		move_from_paths.push_back(path);
		move_to_paths.push_back(recycle_path);
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
		sync_paths.push_back(".folder_locks.txt");
		(void) storage_backup_sync_path_moves(upload_dir, sync_paths, delete_paths,
			move_from_paths, move_to_paths, sync_err);
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", path.c_str());
	root.add_text("recycle_path", recycle_path.c_str());
	root.add_text("message", "folder moved to recycle bin");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

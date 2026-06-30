#include "stdafx.h"
#include "file_common.h"

namespace action {

bool DeleteAction::run(request_t& req, response_t& res,
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

	const bool hard_delete = is_recycle_file_path(file_path);
	bool target_is_dir = upload_directory_exists(upload_dir, file_path);
	if (!target_is_dir && !upload_regular_file_exists(upload_dir, file_path)) {
		if (hard_delete) {
			std::string resolved_path;
			if (resolve_recycle_item_path(upload_dir, file_path, resolved_path,
				target_is_dir))
			{
				file_path = resolved_path;
			} else {
				json_error(res, 404, "recycle item not found", req.isKeepAlive());
				return true;
			}
		} else {
			json_error(res, 404, "file not found", req.isKeepAlive());
			return true;
		}
	}
	if (target_is_dir && !hard_delete) {
		json_error(res, 400, "directory delete should use folder delete API", req.isKeepAlive());
		return true;
	}
	bool lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, parent_relative_path(file_path),
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

	std::string recycle_path;
	if (hard_delete) {
		const std::string fullpath = join_upload_path(upload_dir, file_path);
		if (target_is_dir) {
			if (!delete_directory_recursive(fullpath, err)) {
				json_error(res, 500, err.c_str(), req.isKeepAlive());
				return true;
			}
		} else {
			if (::unlink(fullpath.c_str()) != 0) {
				json_error(res, 404, "file not found or delete failed", req.isKeepAlive());
				return true;
			}
		}

		err.clear();
		if (!delete_recycle_record(upload_dir, file_path, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!target_is_dir
			&& (!folder_unbind_file(upload_dir, file_path, err)
				|| !tag_unbind_file(upload_dir, file_path, err)))
		{
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	} else {
		if (!soft_delete_to_recycle(upload_dir, file_path, recycle_path, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	}

	{
		std::string sync_err;
		std::vector<std::string> sync_paths;
		std::vector<std::string> delete_paths;
		std::vector<std::string> move_from_paths;
		std::vector<std::string> move_to_paths;
		if (!recycle_path.empty()) {
			move_from_paths.push_back(file_path);
			move_to_paths.push_back(recycle_path);
		} else {
			delete_paths.push_back(file_path);
		}
		sync_paths.push_back(".recycle_bin.db");
		sync_paths.push_back(".recycle_bin.db-wal");
		sync_paths.push_back(".recycle_bin.db-shm");
		sync_paths.push_back(".recycle_bin.db-journal");
		sync_paths.push_back(".tag_catalog.db");
		sync_paths.push_back(".tag_catalog.db-wal");
		sync_paths.push_back(".tag_catalog.db-shm");
		sync_paths.push_back(".tag_catalog.db-journal");
		(void) storage_backup_sync_path_moves(upload_dir, sync_paths, delete_paths,
			move_from_paths, move_to_paths, sync_err);
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("file", file_path.c_str());
	root.add_text("message", hard_delete ? "deleted permanently" : "moved to recycle bin");
	root.add_bool("permanent", hard_delete);
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

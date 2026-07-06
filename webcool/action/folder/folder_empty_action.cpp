#include "stdafx.h"
#include "folder_common.h"

namespace action {

namespace {

struct folder_child_t {
	std::string name;
	std::string path;
	bool directory;
};

static void add_folder_empty_sync_paths(std::vector<std::string>& sync_paths)
{
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
	sync_paths.push_back(".file_locks.txt");
}

static bool collect_folder_children_for_empty(const std::string& upload_dir,
	const std::string& folder_path, std::vector<folder_child_t>& out,
	std::string& err)
{
	out.clear();
	err.clear();

	const std::string full_path = join_upload_path(upload_dir, folder_path);
	DIR* dir = opendir(full_path.c_str());
	if (dir == NULL) {
		err = strerror(errno);
		return false;
	}

	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (should_skip_self_parent_entry(entry->d_name)) {
			continue;
		}

		const std::string child_name(entry->d_name);
		const std::string child_path = folder_path.empty()
			? child_name
			: (folder_path + "/" + child_name);
		if (is_protected_virtual_path(child_path)) {
			continue;
		}

		const std::string child_full = join_upload_path(upload_dir, child_path);
		struct stat st;
		if (lstat(child_full.c_str(), &st) != 0) {
			continue;
		}

		folder_child_t child;
		child.name = child_name;
		child.path = child_path;
		child.directory = S_ISDIR(st.st_mode);
		out.push_back(child);
	}

	closedir(dir);
	return true;
}

static bool soft_delete_folder_child_to_recycle(const std::string& upload_dir,
	const std::string& path, std::string& recycle_path, std::string& err)
{
	err.clear();
	recycle_path.clear();

	if (is_recycle_file_path(path)) {
		err = "recycle folder is protected";
		return false;
	}
	if (is_shared_root_path(path)) {
		err = "shared folder is protected";
		return false;
	}
	if (is_shared_fixed_subfolder_path(path)) {
		err = "shared fixed folder is protected";
		return false;
	}
	if (is_root_fixed_folder_path(path)) {
		err = "root fixed folder is protected";
		return false;
	}

	const std::string folder_name = base_name_from_relative_path(path);
	if (!alloc_recycle_folder_target(upload_dir, folder_name, recycle_path, err)) {
		return false;
	}

	const std::string from_full = join_upload_path(upload_dir, path);
	const std::string to_full = join_upload_path(upload_dir, recycle_path);
	if (::rename(from_full.c_str(), to_full.c_str()) != 0) {
		err = "move folder to recycle bin failed";
		return false;
	}

	if (!rename_folder_locks_prefix(upload_dir, path, recycle_path, err)) {
		(void) ::rename(to_full.c_str(), from_full.c_str());
		return false;
	}

	bool tag_updated = false;
	std::string rename_err;
	if (!tag_rename_folder_prefix(upload_dir, path, recycle_path, rename_err)) {
		std::string rollback_err;
		rename_folder_locks_prefix(upload_dir, recycle_path, path, rollback_err);
		(void) ::rename(to_full.c_str(), from_full.c_str());
		err = rename_err;
		return false;
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
		err = rename_err;
		return false;
	}
	if (!recycle_bin_insert_record(upload_dir, recycle_path, path, err)) {
		std::string rollback_err;
		video_resume_rename_folder_prefix(upload_dir, recycle_path, path, rollback_err);
		tag_rename_folder_prefix(upload_dir, recycle_path, path, rollback_err);
		rename_folder_locks_prefix(upload_dir, recycle_path, path, rollback_err);
		(void) ::rename(to_full.c_str(), from_full.c_str());
		return false;
	}

	return true;
}

} // namespace

bool FolderEmptyAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string folder_path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("path"), folder_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_protected_virtual_path(folder_path)) {
		json_error(res, 403, "system folder is protected", req.isKeepAlive());
		return true;
	}
	if (!upload_directory_exists(upload_dir, folder_path)) {
		json_error(res, 404, "folder not found", req.isKeepAlive());
		return true;
	}

	bool lock_allowed = false;
	std::string locked_path;
	if (!is_recycle_file_path(folder_path)
		&& !folder_lock_path_allows(upload_dir, folder_path,
			req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
			lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!is_recycle_file_path(folder_path) && !lock_allowed) {
		json_error(res, 403, "folder is locked", req.isKeepAlive());
		return true;
	}

	std::vector<folder_child_t> children;
	if (!collect_folder_children_for_empty(upload_dir, folder_path, children, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	const bool permanent = is_recycle_file_path(folder_path);
	const bool recycle_root = is_recycle_root_path(folder_path);
	long long deleted_count = 0;
	std::vector<std::string> sync_paths;
	std::vector<std::string> delete_paths;
	std::vector<std::string> move_from_paths;
	std::vector<std::string> move_to_paths;
	add_folder_empty_sync_paths(sync_paths);

	for (size_t i = 0; i < children.size(); ++i) {
		const folder_child_t& child = children[i];
		if (permanent) {
			const std::string child_full = join_upload_path(upload_dir, child.path);
			if (!remove_upload_path_recursive(child_full, err)) {
				json_error(res, 500, err.c_str(), req.isKeepAlive());
				return true;
			}
			if (recycle_root && !delete_recycle_record(upload_dir, child.path, err)) {
				json_error(res, 500, err.c_str(), req.isKeepAlive());
				return true;
			}
			delete_paths.push_back(child.path);
			deleted_count++;
			continue;
		}

		bool child_lock_allowed = false;
		std::string child_locked_path;
		if (!folder_lock_path_allows(upload_dir,
			child.directory ? child.path : parent_relative_path(child.path),
			req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
			child_lock_allowed, child_locked_path, err))
		{
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!child_lock_allowed) {
			json_error(res, 403, "child item is locked", req.isKeepAlive());
			return true;
		}
		if (!child.directory) {
			bool file_lock_allowed = false;
			if (!file_lock_path_allows(upload_dir, remote_file_lock_key(child.path),
				req.getParameter("file_password") ? req.getParameter("file_password") : "",
				file_lock_allowed, err))
			{
				json_error(res, 500, err.c_str(), req.isKeepAlive());
				return true;
			}
			if (!file_lock_allowed) {
				json_error(res, 403, "child file is locked", req.isKeepAlive());
				return true;
			}
		}

		std::string recycle_path;
		if (child.directory) {
			if (!soft_delete_folder_child_to_recycle(upload_dir, child.path,
				recycle_path, err))
			{
				json_error(res, 500, err.c_str(), req.isKeepAlive());
				return true;
			}
		} else if (!soft_delete_to_recycle(upload_dir, child.path,
			recycle_path, err))
		{
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		move_from_paths.push_back(child.path);
		move_to_paths.push_back(recycle_path);
		deleted_count++;
	}

	{
		std::string sync_err;
		(void) storage_backup_sync_path_moves(upload_dir, sync_paths, delete_paths,
			move_from_paths, move_to_paths, sync_err);
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", folder_path.c_str());
	root.add_number("count", deleted_count);
	root.add_bool("permanent", permanent);
	root.add_text("message", permanent ? "folder emptied permanently" : "folder emptied to recycle bin");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

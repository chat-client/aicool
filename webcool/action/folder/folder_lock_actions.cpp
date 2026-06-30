#include "stdafx.h"
#include "folder_common.h"

#include <mutex>

namespace action {

bool FolderLockAction::run(request_t& req, response_t& res,
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
	if (!upload_directory_exists(upload_dir, path)) {
		json_error(res, 404, "folder not found", req.isKeepAlive());
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

	const std::string password = req.getParameter("password") ? req.getParameter("password") : "";
	if (!validate_lock_password(password, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	{
		std::lock_guard<std::mutex> guard(g_folder_lock_mutex);
		std::map<std::string, std::string> locks;
		if (!load_folder_locks_locked(upload_dir, locks, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		locks[path] = password;
		if (!save_folder_locks_locked(upload_dir, locks, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", path.c_str());
	root.add_text("message", "folder locked");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool FolderUnlockAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("path"), path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	const std::string password = req.getParameter("password") ? req.getParameter("password") : "";

	{
		std::lock_guard<std::mutex> guard(g_folder_lock_mutex);
		std::map<std::string, std::string> locks;
		if (!load_folder_locks_locked(upload_dir, locks, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		std::map<std::string, std::string>::iterator it = locks.find(path);
		if (it == locks.end()) {
			json_error(res, 404, "folder lock not found", req.isKeepAlive());
			return true;
		}
		if (it->second != password) {
			json_error(res, 403, "password is incorrect", req.isKeepAlive());
			return true;
		}
		locks.erase(it);
		if (!save_folder_locks_locked(upload_dir, locks, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", path.c_str());
	root.add_text("message", "folder unlocked");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool FolderLockVerifyAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("path"), path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	const std::string password = req.getParameter("password") ? req.getParameter("password") : "";
	bool allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, path, password, allowed, locked_path, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!allowed) {
		json_error(res, 403, "password is incorrect", req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", path.c_str());
	root.add_text("locked_path", locked_path.c_str());
	root.add_text("message", "folder lock verified");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

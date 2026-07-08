#include "stdafx.h"
#include "local_disk_common.h"

namespace action {

std::string remote_file_lock_key(const std::string& path);

namespace {

static std::string copy_from_remote_json_text(acl::json_node* node)
{
	if (node == NULL) {
		return "";
	}
	const char* value = node->get_string();
	if (value == NULL) {
		value = node->get_text();
	}
	return value ? value : "";
}

static bool copy_from_remote_is_json_request(request_t& req)
{
	if (req.getRequestType() == acl::HTTP_REQUEST_TEXT_JSON) {
		return true;
	}
	const char* content_type = req.getHeader("Content-Type");
	if (content_type == NULL) {
		return false;
	}
	std::string value = content_type;
	for (size_t i = 0; i < value.size(); ++i) {
		value[i] = (char) ::tolower((unsigned char) value[i]);
	}
	return value.find("application/json") != std::string::npos
		|| value.find("text/json") != std::string::npos
		|| value.find("+json") != std::string::npos;
}

static bool copy_from_remote_request_data(request_t& req,
	std::string& source_path, bool& source_is_dir_param,
	std::string& target, std::string& folder_password,
	std::string& file_password, std::string& target_local_dir_password,
	std::string& err)
{
	source_is_dir_param = req.getParameter("path") != NULL;
	const char* source_param = source_is_dir_param
		? req.getParameter("path")
		: req.getParameter("file");
	source_path = source_param ? source_param : "";
	target = req.getParameter("target") ? req.getParameter("target") : "";
	folder_password = req.getParameter("folder_password")
		? req.getParameter("folder_password") : "";
	file_password = req.getParameter("file_password")
		? req.getParameter("file_password") : "";
	target_local_dir_password = req.getParameter("target_local_dir_password")
		? req.getParameter("target_local_dir_password") : "";

	if (req.getContentLength() <= 0) {
		return true;
	}
	if (!copy_from_remote_is_json_request(req)) {
		return true;
	}
	acl::json* body = req.getJson(256 * 1024);
	if (body == NULL) {
		err = "invalid json body";
		return false;
	}

	const std::string body_path = copy_from_remote_json_text((*body)["path"]);
	const std::string body_file = copy_from_remote_json_text((*body)["file"]);
	if (!body_path.empty()) {
		source_path = body_path;
		source_is_dir_param = true;
	} else if (!body_file.empty()) {
		source_path = body_file;
		source_is_dir_param = false;
	}

	acl::json_node* target_node = (*body)["target"];
	if (target_node != NULL) {
		target = copy_from_remote_json_text(target_node);
	}
	acl::json_node* folder_password_node = (*body)["folder_password"];
	if (folder_password_node != NULL) {
		folder_password = copy_from_remote_json_text(folder_password_node);
	}
	acl::json_node* file_password_node = (*body)["file_password"];
	if (file_password_node != NULL) {
		file_password = copy_from_remote_json_text(file_password_node);
	}
	acl::json_node* target_password_node = (*body)["target_local_dir_password"];
	if (target_password_node != NULL) {
		target_local_dir_password =
			copy_from_remote_json_text(target_password_node);
	}
	return true;
}

} // namespace

bool LocalDiskDeleteAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string path;
	std::string err;
	if (!normalize_local_path(req.getParameter("path"), path, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	struct stat st;
	if (stat(path.c_str(), &st) != 0) {
		json_error(res, 404, "path not found", req.isKeepAlive());
		return true;
	}
	const bool is_dir = S_ISDIR(st.st_mode);
	const bool is_file = S_ISREG(st.st_mode);
	if (!is_dir && !is_file) {
		json_error(res, 400, "only files and directories can be deleted", req.isKeepAlive());
		return true;
	}
	if (!ensure_local_dir_unlocked_for_request(upload_dir, req, res,
		is_dir ? path : parent_path(path),
		is_dir ? "directory is locked" : "parent directory is locked"))
	{
		return true;
	}
	if (is_file) {
		bool file_lock_allowed = false;
		std::string lock_err;
		if (!file_lock_path_allows(upload_dir, local_file_lock_key(path),
			req.getParameter("file_password") ? req.getParameter("file_password") : "",
			file_lock_allowed, lock_err))
		{
			json_error(res, 500, lock_err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!file_lock_allowed) {
			json_error(res, 403, "file is locked", req.isKeepAlive());
			return true;
		}
	}
	if (path == "/") {
		json_error(res, 409, "root directory cannot be deleted", req.isKeepAlive());
		return true;
	}

	if (is_dir && is_system_level_directory_path(path)) {
		json_error(res, 409, "system directory cannot be deleted",
			req.isKeepAlive());
		return true;
	}

	std::string trash_path;
	if (!move_file_to_trash(path, trash_path, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	std::string rename_err;
	const std::string old_local_key = std::string("local:") + path;
	const std::string new_local_key = std::string("local:") + trash_path;
	if (is_dir) {
		if (!tag_rename_folder_prefix(upload_dir, old_local_key, new_local_key,
			rename_err)
			|| !video_resume_rename_folder_prefix(upload_dir, old_local_key,
				new_local_key, rename_err)
			|| !file_lock_rename_prefix(upload_dir, local_dir_lock_key(path),
				local_dir_lock_key(trash_path), rename_err)
			|| !file_lock_rename_prefix(upload_dir, local_file_lock_key(path),
				local_file_lock_key(trash_path), rename_err))
		{
			(void) ::rename(trash_path.c_str(), path.c_str());
			json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
			return true;
		}
	} else {
		if (!tag_rename_file(upload_dir, old_local_key, new_local_key, rename_err)
			|| !video_resume_rename_file(upload_dir, old_local_key,
				new_local_key, rename_err)
			|| !file_lock_rename_key(upload_dir, local_file_lock_key(path),
				local_file_lock_key(trash_path), rename_err))
		{
			(void) ::rename(trash_path.c_str(), path.c_str());
			json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
			return true;
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", path.c_str());
	root.add_text("trash_path", trash_path.c_str());
	root.add_text("message", is_dir ? "directory moved to trash" : "file moved to trash");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskCreateDirAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string parent;
	std::string err;
	if (!normalize_local_path(req.getParameter("path"), parent, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	struct stat st;
	if (stat(parent.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
		json_error(res, 404, "parent directory not found", req.isKeepAlive());
		return true;
	}
	if (!ensure_local_dir_unlocked_for_request(upload_dir, req, res, parent,
		"parent directory is locked"))
	{
		return true;
	}

	const char* name_param = req.getParameter("name");
	std::string name = name_param ? name_param : "";
	while (!name.empty() && (name[0] == ' ' || name[0] == '\t')) {
		name.erase(0, 1);
	}
	while (!name.empty() && (name[name.size() - 1] == ' ' || name[name.size() - 1] == '\t')) {
		name.erase(name.size() - 1);
	}
	if (!validate_local_name(name, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	const std::string new_path = join_local_path(parent, name.c_str());
	if (::mkdir(new_path.c_str(), 0755) != 0) {
		json_error(res, errno == EEXIST ? 409 : 500,
			errno == EEXIST ? "directory already exists" : strerror(errno),
			req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", new_path.c_str());
	root.add_text("name", name.c_str());
	root.add_text("message", "directory created");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskMoveAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string source;
	std::string target;
	std::string err;
	const char* source_param = req.getParameter("path");
	const char* target_param = req.getParameter("target");
	if (source_param == NULL || *source_param == '\0'
		|| target_param == NULL || *target_param == '\0')
	{
		json_error(res, 400, "source path and target directory are required",
			req.isKeepAlive());
		return true;
	}
	if (!normalize_local_path(source_param, source, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!normalize_local_path(target_param, target, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	struct stat source_st;
	if (stat(source.c_str(), &source_st) != 0) {
		json_error(res, 404, "source path not found", req.isKeepAlive());
		return true;
	}
	const bool source_is_dir = S_ISDIR(source_st.st_mode);
	if (!source_is_dir && !S_ISREG(source_st.st_mode)) {
		json_error(res, 400, "only files and directories can be moved",
			req.isKeepAlive());
		return true;
	}
	if (!source_is_dir) {
		bool file_lock_allowed = false;
		std::string lock_err;
		if (!file_lock_path_allows(upload_dir, local_file_lock_key(source),
			req.getParameter("file_password") ? req.getParameter("file_password") : "",
			file_lock_allowed, lock_err))
		{
			json_error(res, 500, lock_err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!file_lock_allowed) {
			json_error(res, 403, "file is locked", req.isKeepAlive());
			return true;
		}
	}
	if (source == "/") {
		json_error(res, 409, "root directory cannot be moved", req.isKeepAlive());
		return true;
	}
	if (source_is_dir && is_system_level_directory_path(source)) {
		json_error(res, 409, "system directory cannot be moved",
			req.isKeepAlive());
		return true;
	}
	if (!ensure_local_dir_unlocked_for_request(upload_dir, req, res,
		source_is_dir ? source : parent_path(source),
		source_is_dir ? "source directory is locked" : "source parent directory is locked"))
	{
		return true;
	}

	struct stat target_st;
	if (stat(target.c_str(), &target_st) != 0 || !S_ISDIR(target_st.st_mode)) {
		json_error(res, 404, "target directory not found", req.isKeepAlive());
		return true;
	}
	if (!ensure_local_dir_unlocked_for_request(upload_dir, req, res, target,
		"target directory is locked", "target_local_dir_password"))
	{
		return true;
	}
	if (source_is_dir && is_same_or_child_path(source, target)) {
		json_error(res, 409, "directory cannot be moved into itself",
			req.isKeepAlive());
		return true;
	}
	if (parent_path(source) == target) {
		json_error(res, 409, "source is already in target directory",
			req.isKeepAlive());
		return true;
	}

	const std::string name = local_base_name(source);
	if (name.empty()) {
		json_error(res, 400, "invalid source path", req.isKeepAlive());
		return true;
	}
	const std::string dest = join_local_path(target, name.c_str());
	struct stat dest_st;
	if (stat(dest.c_str(), &dest_st) == 0) {
		json_error(res, 409, "target already contains a path with same name",
			req.isKeepAlive());
		return true;
	}
	if (errno != ENOENT) {
		json_error(res, 500, strerror(errno), req.isKeepAlive());
		return true;
	}

	if (::rename(source.c_str(), dest.c_str()) != 0) {
		json_error(res, errno == EXDEV ? 409 : 500,
			errno == EXDEV ? "cannot move across different file systems" : strerror(errno),
			req.isKeepAlive());
		return true;
	}
	if (!source_is_dir && !file_lock_rename_key(upload_dir,
		local_file_lock_key(source), local_file_lock_key(dest), err))
	{
		(void) ::rename(dest.c_str(), source.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (source_is_dir && !file_lock_rename_key(upload_dir,
		local_dir_lock_key(source), local_dir_lock_key(dest), err))
	{
		(void) ::rename(dest.c_str(), source.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", dest.c_str());
	root.add_text("old_path", source.c_str());
	root.add_text("target", target.c_str());
	root.add_text("message", source_is_dir ? "directory moved" : "file moved");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskCopyAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string source;
	std::string target;
	std::string err;
	const char* source_param = req.getParameter("path");
	const char* target_param = req.getParameter("target");
	const bool async = req.getParameter("async") != NULL
		&& strcmp(req.getParameter("async"), "1") == 0;
	const bool overwrite = req.getParameter("overwrite") != NULL
		&& strcmp(req.getParameter("overwrite"), "1") == 0;
	if (source_param == NULL || *source_param == '\0'
		|| target_param == NULL || *target_param == '\0')
	{
		json_error(res, 400, "source path and target directory are required",
			req.isKeepAlive());
		return true;
	}
	if (!normalize_local_path(source_param, source, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!normalize_local_path(target_param, target, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	struct stat source_st;
	if (stat(source.c_str(), &source_st) != 0) {
		json_error(res, 404, "source path not found", req.isKeepAlive());
		return true;
	}
	const bool source_is_dir = S_ISDIR(source_st.st_mode);
	const bool source_is_file = S_ISREG(source_st.st_mode);
	if (!source_is_dir && !source_is_file) {
		json_error(res, 400, "only files and directories can be copied",
			req.isKeepAlive());
		return true;
	}
	if (source == "/") {
		json_error(res, 409, "root directory cannot be copied", req.isKeepAlive());
		return true;
	}
	if (source_is_dir && is_system_level_directory_path(source)) {
		json_error(res, 409, "system directory cannot be copied",
			req.isKeepAlive());
		return true;
	}
	if (!source_is_dir) {
		bool file_lock_allowed = false;
		std::string lock_err;
		if (!file_lock_path_allows(upload_dir, local_file_lock_key(source),
			req.getParameter("file_password") ? req.getParameter("file_password") : "",
			file_lock_allowed, lock_err))
		{
			json_error(res, 500, lock_err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!file_lock_allowed) {
			json_error(res, 403, "file is locked", req.isKeepAlive());
			return true;
		}
	}
	if (!ensure_local_dir_unlocked_for_request(upload_dir, req, res,
		source_is_dir ? source : parent_path(source),
		source_is_dir ? "source directory is locked" : "source parent directory is locked"))
	{
		return true;
	}

	struct stat target_st;
	if (stat(target.c_str(), &target_st) != 0 || !S_ISDIR(target_st.st_mode)) {
		json_error(res, 404, "target directory not found", req.isKeepAlive());
		return true;
	}
	if (!ensure_local_dir_unlocked_for_request(upload_dir, req, res, target,
		"target directory is locked", "target_local_dir_password"))
	{
		return true;
	}
	if (source_is_dir && is_same_or_child_path(source, target)) {
		json_error(res, 409, "directory cannot be copied into itself",
			req.isKeepAlive());
		return true;
	}

	const std::string name = local_base_name(source);
	if (name.empty()) {
		json_error(res, 400, "invalid source path", req.isKeepAlive());
		return true;
	}
	const std::string dest = join_local_path(target, name.c_str());
	struct stat dest_st;
	if (stat(dest.c_str(), &dest_st) == 0) {
		if (!overwrite) {
			json_error(res, 409, "target already contains a path with same name",
				req.isKeepAlive());
			return true;
		}
		if (dest == source) {
			json_error(res, 409, "source and destination are the same",
				req.isKeepAlive());
			return true;
		}
		if (S_ISDIR(dest_st.st_mode)
			&& !ensure_local_dir_unlocked_for_request(upload_dir, req, res, dest,
				"destination directory is locked", "target_local_dir_password"))
		{
			return true;
		}
		if (S_ISREG(dest_st.st_mode)) {
			bool dest_file_allowed = false;
			std::string lock_err;
			if (!file_lock_path_allows(upload_dir, local_file_lock_key(dest),
				req.getParameter("file_password") ? req.getParameter("file_password") : "",
				dest_file_allowed, lock_err))
			{
				json_error(res, 500, lock_err.c_str(), req.isKeepAlive());
				return true;
			}
			if (!dest_file_allowed) {
				json_error(res, 403, "destination file is locked", req.isKeepAlive());
				return true;
			}
		}
		if (!remove_local_path_recursive(dest, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	}
	else if (errno != ENOENT) {
		json_error(res, 500, strerror(errno), req.isKeepAlive());
		return true;
	}

	if (async) {
		const std::string task_id = start_remote_copy_task(source, dest, dest,
			source_is_dir, upload_dir);
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("task_id", task_id.c_str());
		root.add_text("path", dest.c_str());
		root.add_text("source", source.c_str());
		root.add_text("target", target.c_str());
		root.add_bool("overwritten", overwrite);
		root.add_bool("directory", source_is_dir);
		root.add_text("message", "copy task started");
		return sendJson(res, 200, root, req.isKeepAlive());
	}

	if (!copy_local_path_recursive(source, dest, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", dest.c_str());
	root.add_text("source", source.c_str());
	root.add_text("target", target.c_str());
	root.add_bool("overwritten", overwrite);
	root.add_text("message", source_is_dir ? "directory copied" : "file copied");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskCopyFromRemoteAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string raw_source_path;
	std::string target;
	std::string folder_password;
	std::string file_password;
	std::string target_local_dir_password;
	std::string err;
	bool source_is_dir_param = false;
	if (!copy_from_remote_request_data(req, raw_source_path,
		source_is_dir_param, target, folder_password, file_password,
		target_local_dir_password, err))
	{
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (raw_source_path.empty()) {
		json_error(res, 400, "source path is required", req.isKeepAlive());
		return true;
	}
	std::string source_path;
	if (!normalize_relative_path(raw_source_path.c_str(), source_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(source_path) || is_protected_virtual_path(source_path)) {
		json_error(res, 403, "source path is protected", req.isKeepAlive());
		return true;
	}
	if (source_is_dir_param && is_root_fixed_folder_path(source_path)) {
		json_error(res, 409, "root fixed folder is protected", req.isKeepAlive());
		return true;
	}
	if (!normalize_local_path(target.c_str(), target, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	const std::string source_full = join_upload_path(upload_dir, source_path);
	struct stat source_st;
	if (stat(source_full.c_str(), &source_st) != 0) {
		json_error(res, 404, "source path not found", req.isKeepAlive());
		return true;
	}
	const bool source_is_dir = S_ISDIR(source_st.st_mode);
	const bool source_is_file = S_ISREG(source_st.st_mode);
	if (!source_is_dir && !source_is_file) {
		json_error(res, 400, "only files and directories can be copied", req.isKeepAlive());
		return true;
	}
	if (source_is_dir_param != source_is_dir) {
		json_error(res, 400, "source type mismatch", req.isKeepAlive());
		return true;
	}
	if (source_is_dir && !upload_directory_exists(upload_dir, source_path)) {
		json_error(res, 404, "source folder not found", req.isKeepAlive());
		return true;
	}
	if (source_is_file && !upload_regular_file_exists(upload_dir, source_path)) {
		json_error(res, 404, "source file not found", req.isKeepAlive());
		return true;
	}

	bool source_lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir,
		source_is_dir ? source_path : parent_relative_path(source_path),
		folder_password,
		source_lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!source_lock_allowed) {
		json_error(res, 403, "source folder is locked", req.isKeepAlive());
		return true;
	}
	if (source_is_file) {
		bool file_lock_allowed = false;
		if (!file_lock_path_allows(upload_dir, remote_file_lock_key(source_path),
			file_password, file_lock_allowed, err))
		{
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!file_lock_allowed) {
			json_error(res, 403, "file is locked", req.isKeepAlive());
			return true;
		}
	}

	struct stat target_st;
	if (stat(target.c_str(), &target_st) != 0 || !S_ISDIR(target_st.st_mode)) {
		json_error(res, 404, "target directory not found", req.isKeepAlive());
		return true;
	}
	const std::string local_lock_upload_dir = runtime_upload_dir_get();
	bool target_lock_allowed = false;
	std::string target_locked_path;
	if (!local_dir_lock_path_allows(local_lock_upload_dir, target,
		target_local_dir_password, target_lock_allowed, target_locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!target_lock_allowed) {
		json_error(res, 403, "target directory is locked", req.isKeepAlive());
		return true;
	}

	const std::string name = base_name_from_relative_path(source_path);
	if (name.empty()) {
		json_error(res, 400, "invalid source path", req.isKeepAlive());
		return true;
	}
	const std::string dest = join_local_path(target, name.c_str());
	struct stat dest_st;
	if (stat(dest.c_str(), &dest_st) == 0) {
		json_error(res, 409, "target already contains a path with same name",
			req.isKeepAlive());
		return true;
	}
	if (errno != ENOENT) {
		json_error(res, 500, strerror(errno), req.isKeepAlive());
		return true;
	}

	const std::string task_id = start_remote_copy_task(source_full, dest, dest,
		source_is_dir, upload_dir);
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", task_id.c_str());
	root.add_text("path", dest.c_str());
	root.add_text("source", source_path.c_str());
	root.add_text("target", target.c_str());
	root.add_bool("directory", source_is_dir);
	root.add_text("message", "copy task started");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskRenameAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string source;
	std::string err;
	const char* source_param = req.getParameter("path");
	if (source_param == NULL || *source_param == '\0') {
		json_error(res, 400, "source path is required", req.isKeepAlive());
		return true;
	}
	if (!normalize_local_path(source_param, source, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	struct stat source_st;
	if (stat(source.c_str(), &source_st) != 0) {
		json_error(res, 404, "source path not found", req.isKeepAlive());
		return true;
	}
	const bool source_is_dir = S_ISDIR(source_st.st_mode);
	const bool source_is_file = S_ISREG(source_st.st_mode);
	if (!source_is_dir && !source_is_file) {
		json_error(res, 400, "only files and directories can be renamed",
			req.isKeepAlive());
		return true;
	}
	if (source == "/") {
		json_error(res, 409, "root directory cannot be renamed", req.isKeepAlive());
		return true;
	}
	if (source_is_dir && is_system_level_directory_path(source)) {
		json_error(res, 409, "system directory cannot be renamed",
			req.isKeepAlive());
		return true;
	}

	std::string new_name = req.getParameter("name") ? req.getParameter("name") : "";
	while (!new_name.empty() && new_name[0] == ' ') {
		new_name.erase(0, 1);
	}
	while (!new_name.empty() && new_name[new_name.size() - 1] == ' ') {
		new_name.erase(new_name.size() - 1);
	}
	if (!validate_local_name_segment(new_name, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	if (source_is_file) {
		bool file_lock_allowed = false;
		std::string lock_err;
		if (!file_lock_path_allows(upload_dir, local_file_lock_key(source),
			req.getParameter("file_password") ? req.getParameter("file_password") : "",
			file_lock_allowed, lock_err))
		{
			json_error(res, 500, lock_err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!file_lock_allowed) {
			json_error(res, 403, "file is locked", req.isKeepAlive());
			return true;
		}
	}
	if (!ensure_local_dir_unlocked_for_request(upload_dir, req, res,
		source_is_dir ? source : parent_path(source),
		source_is_dir ? "directory is locked" : "source parent directory is locked"))
	{
		return true;
	}

	const std::string dest = join_local_path(parent_path(source), new_name.c_str());
	if (dest == source) {
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("path", source.c_str());
		root.add_text("old_path", source.c_str());
		root.add_text("name", new_name.c_str());
		root.add_text("message", "file unchanged");
		return sendJson(res, 200, root, req.isKeepAlive());
	}

	struct stat dest_st;
	if (stat(dest.c_str(), &dest_st) == 0) {
		json_error(res, 409, "target path already exists", req.isKeepAlive());
		return true;
	}
	if (errno != ENOENT) {
		json_error(res, 500, strerror(errno), req.isKeepAlive());
		return true;
	}

	if (::rename(source.c_str(), dest.c_str()) != 0) {
		json_error(res, errno == EXDEV ? 409 : 500,
			errno == EXDEV ? "cannot rename across different file systems" : strerror(errno),
			req.isKeepAlive());
		return true;
	}

	std::string rename_err;
	const std::string old_local_key = std::string("local:") + source;
	const std::string new_local_key = std::string("local:") + dest;
	if (source_is_dir) {
		if (!tag_rename_folder_prefix(upload_dir, old_local_key, new_local_key,
			rename_err)
			|| !video_resume_rename_folder_prefix(upload_dir, old_local_key,
				new_local_key, rename_err)
			|| !file_lock_rename_prefix(upload_dir, local_dir_lock_key(source),
				local_dir_lock_key(dest), rename_err)
			|| !file_lock_rename_prefix(upload_dir, local_file_lock_key(source),
				local_file_lock_key(dest), rename_err))
		{
			(void) ::rename(dest.c_str(), source.c_str());
			json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
			return true;
		}
	} else {
		if (!tag_rename_file(upload_dir, old_local_key, new_local_key, rename_err)
			|| !video_resume_rename_file(upload_dir, old_local_key, new_local_key,
				rename_err)
			|| !file_lock_rename_key(upload_dir,
				local_file_lock_key(source), local_file_lock_key(dest), rename_err))
		{
			(void) ::rename(dest.c_str(), source.c_str());
			json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
			return true;
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", dest.c_str());
	root.add_text("old_path", source.c_str());
	root.add_text("name", new_name.c_str());
	root.add_bool("directory", source_is_dir);
	root.add_text("message", source_is_dir ? "directory renamed" : "file renamed");
	return sendJson(res, 200, root, req.isKeepAlive());
}


} // namespace action

#include "stdafx.h"
#include "tag_common.h"

namespace action {

static bool fetch_tag_for_lock_request(const std::string& upload_dir,
	request_t& req, response_t& res, TagRow& row, std::string& tag_id,
	std::string& err)
{
	if (!ensure_tag_db_for_request(upload_dir, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return false;
	}
	tag_id = trim_copy(req.getParameter("id"));
	if (!validate_tag_id(tag_id, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return false;
	}
	std::lock_guard<webcool::mutex> guard(g_tag_mutex);
	acl::db_sqlite db(tag_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
	if (!open_tag_db_locked(db, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return false;
	}
	if (!fetch_tag_locked(db, tag_id, &row, err)) {
		json_error(res, 404, err.empty() ? "tag not found" : err.c_str(),
			req.isKeepAlive());
		return false;
	}
	return true;
}

bool TagLockAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string tag_id, err;
	TagRow row;
	if (!fetch_tag_for_lock_request(upload_dir, req, res, row, tag_id, err)) {
		return true;
	}
	if (is_protected_root_tag(row)) {
		json_error(res, 400, "restricted root tags cannot be locked", req.isKeepAlive());
		return true;
	}
	const std::string password = req.getParameter("password")
		? req.getParameter("password")
		: "";
	if (!named_lock_set(upload_dir, tag_lock_key(tag_id), password, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("id", tag_id.c_str());
	root.add_text("message", "tag locked");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool TagUnlockAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string tag_id, err;
	TagRow row;
	if (!fetch_tag_for_lock_request(upload_dir, req, res, row, tag_id, err)) {
		return true;
	}
	const std::string password = req.getParameter("password")
		? req.getParameter("password")
		: "";
	if (!named_lock_remove(upload_dir, tag_lock_key(tag_id), password, err)) {
		json_error(res, err == "password is incorrect" ? 403 : 404,
			err.c_str(), req.isKeepAlive());
		return true;
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("id", tag_id.c_str());
	root.add_text("message", "tag lock removed");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool TagLockVerifyAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string tag_id, err;
	TagRow row;
	if (!fetch_tag_for_lock_request(upload_dir, req, res, row, tag_id, err)) {
		return true;
	}
	bool allowed = false;
	if (!named_lock_verify(upload_dir, tag_lock_key(tag_id),
		req.getParameter("password") ? req.getParameter("password") : "",
		allowed, err))
	{
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
	root.add_text("id", tag_id.c_str());
	root.add_text("message", "tag lock verified");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

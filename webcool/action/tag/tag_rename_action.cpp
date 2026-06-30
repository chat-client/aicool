#include "stdafx.h"
#include "tag_common.h"

namespace action {

bool TagRenameAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string db_err;
	if (!ensure_tag_db_for_request(upload_dir, db_err)) {
		json_error(res, 500, db_err.c_str(), req.isKeepAlive());
		return true;
	}

	std::string tag_id = trim_copy(req.getParameter("id"));
	if (!validate_tag_id(tag_id, db_err)) {
		json_error(res, 400, db_err.c_str(), req.isKeepAlive());
		return true;
	}

	std::string name = trim_copy(req.getParameter("name"));
	if (!validate_tag_name(name, db_err)) {
		json_error(res, 400, db_err.c_str(), req.isKeepAlive());
		return true;
	}

	TagRow row;
	{
		std::lock_guard<std::mutex> guard(g_tag_mutex);
		acl::db_sqlite db(tag_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
		if (!open_tag_db_locked(db, db_err)) {
			json_error(res, 500, db_err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!ensure_default_root_tags_locked(db, db_err)) {
			json_error(res, 500, db_err.c_str(), req.isKeepAlive());
			return true;
		}

		if (!fetch_tag_locked(db, tag_id, &row, db_err)) {
			json_error(res, 404,
				db_err.empty() ? "tag not found" : db_err.c_str(),
				req.isKeepAlive());
			return true;
		}
		if (is_protected_root_tag(row)) {
			json_error(res, 400,
				"restricted root tags cannot be renamed",
				req.isKeepAlive());
			return true;
		}

		if (row.name != name) {
			acl::query query;
			query.create("UPDATE tag_catalog"
				" SET tag_name=:tag_name, updated_at=strftime('%s','now')"
				" WHERE id=:id")
				.set_parameter("tag_name", name.c_str())
				.set_parameter("id", tag_id.c_str());
			if (!db.exec_update(query)) {
				json_error(res, 500, db.get_error(), req.isKeepAlive());
				return true;
			}
			row.name = name;
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("id", tag_id.c_str());
	root.add_text("name", row.name.c_str());
	root.add_text("parent_id", row.parent_id.c_str());
	root.add_text("message", "tag renamed");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

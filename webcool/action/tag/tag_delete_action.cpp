#include "stdafx.h"
#include "tag_common.h"

namespace action {

bool TagDeleteAction::run(request_t& req, response_t& res,
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

	std::vector<std::string> ids;
	{
		std::lock_guard<webcool::mutex> guard(g_tag_mutex);
		acl::db_sqlite db(tag_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
		if (!open_tag_db_locked(db, db_err)) {
			json_error(res, 500, db_err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!ensure_default_root_tags_locked(db, db_err)) {
			json_error(res, 500, db_err.c_str(), req.isKeepAlive());
			return true;
		}

		TagRow row;
		if (!fetch_tag_locked(db, tag_id, &row, db_err)) {
			json_error(res, 404,
				db_err.empty() ? "tag not found" : db_err.c_str(),
				req.isKeepAlive());
			return true;
		}
		if (is_protected_root_tag(row)) {
			json_error(res, 400,
				"restricted root tags cannot be deleted",
				req.isKeepAlive());
			return true;
		}

		if (!collect_subtree_ids_locked(db, tag_id, ids, db_err)) {
			json_error(res, 500, db_err.c_str(), req.isKeepAlive());
			return true;
		}

		for (size_t i = ids.size(); i > 0; --i) {
			acl::query query;
			query.create("DELETE FROM tag_catalog WHERE id=:id")
				.set_parameter("id", ids[i - 1].c_str());
			if (!db.exec_update(query)) {
				json_error(res, 500, db.get_error(), req.isKeepAlive());
				return true;
			}
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("id", tag_id.c_str());
	root.add_number("removed", (long long) ids.size());
	root.add_text("message", "tag deleted");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

#include "stdafx.h"
#include "tag_common.h"

namespace action {

bool TagCreateAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string db_err;
	if (!ensure_tag_db_for_request(upload_dir, db_err)) {
		json_error(res, 500, db_err.c_str(), req.isKeepAlive());
		return true;
	}

	std::string name = trim_copy(req.getParameter("name"));
	if (!validate_tag_name(name, db_err)) {
		json_error(res, 400, db_err.c_str(), req.isKeepAlive());
		return true;
	}

	std::string parent_id = trim_copy(req.getParameter("parent_id"));
	if (!parent_id.empty() && !validate_tag_id(parent_id, db_err)) {
		json_error(res, 400, db_err.c_str(), req.isKeepAlive());
		return true;
	}

	std::string new_id;
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

		if (!parent_id.empty()) {
			TagRow parent_row;
			if (!fetch_tag_locked(db, parent_id, &parent_row, db_err)) {
				json_error(res, 404,
					db_err.empty() ? "parent tag not found" : db_err.c_str(),
					req.isKeepAlive());
				return true;
			}

			int parent_level = get_tag_level_locked(db, parent_id, db_err);
			if (parent_level < 0) {
				json_error(res, 500, db_err.c_str(), req.isKeepAlive());
				return true;
			}
			if (parent_level >= 3) {
				json_error(res, 400, "tag level exceeds max depth", req.isKeepAlive());
				return true;
			}
		}

		long long sort_order = next_sort_order_locked(db, parent_id, db_err);
		if (sort_order <= 0) {
			json_error(res, 500, db_err.c_str(), req.isKeepAlive());
			return true;
		}

		new_id = make_tag_id_locked();
		acl::query query;
		query.create("INSERT INTO tag_catalog(id, parent_id, tag_name, sort_order, updated_at)"
			" VALUES(:id, :parent_id, :tag_name, :sort_order, strftime('%s','now'))")
			.set_parameter("id", new_id.c_str())
			.set_parameter("parent_id", parent_id.c_str())
			.set_parameter("tag_name", name.c_str())
			.set_parameter("sort_order", sort_order);
		if (!db.exec_update(query)) {
			json_error(res, 500, db.get_error(), req.isKeepAlive());
			return true;
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("id", new_id.c_str());
	root.add_text("name", name.c_str());
	root.add_text("parent_id", parent_id.c_str());
	root.add_text("message", "tag created");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

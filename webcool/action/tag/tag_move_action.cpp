#include "stdafx.h"
#include "tag_common.h"

namespace action {

bool TagMoveAction::run(request_t& req, response_t& res,
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

	std::string parent_id = trim_copy(req.getParameter("parent_id"));
	if (!parent_id.empty() && !validate_tag_id(parent_id, db_err)) {
		json_error(res, 400, db_err.c_str(), req.isKeepAlive());
		return true;
	}

	TagRow row;
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

		if (!fetch_tag_locked(db, tag_id, &row, db_err)) {
			json_error(res, 404,
				db_err.empty() ? "tag not found" : db_err.c_str(),
				req.isKeepAlive());
			return true;
		}
		if (is_protected_root_tag(row)) {
			json_error(res, 400,
				"restricted root tags cannot be moved",
				req.isKeepAlive());
			return true;
		}
		if (parent_id == tag_id) {
			json_error(res, 400, "cannot move tag into itself", req.isKeepAlive());
			return true;
		}

		std::vector<std::string> subtree_ids;
		if (!collect_subtree_ids_locked(db, tag_id, subtree_ids, db_err)) {
			json_error(res, 500, db_err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!parent_id.empty()
			&& std::find(subtree_ids.begin(), subtree_ids.end(), parent_id)
				!= subtree_ids.end())
		{
			json_error(res, 400, "cannot move tag into its child", req.isKeepAlive());
			return true;
		}

		int target_level = 0;
		if (!parent_id.empty()) {
			TagRow parent_row;
			if (!fetch_tag_locked(db, parent_id, &parent_row, db_err)) {
				json_error(res, 404,
					db_err.empty() ? "parent tag not found" : db_err.c_str(),
					req.isKeepAlive());
				return true;
			}
			target_level = get_tag_level_locked(db, parent_id, db_err);
			if (target_level < 0) {
				json_error(res, 500, db_err.c_str(), req.isKeepAlive());
				return true;
			}
			if (target_level >= 3) {
				json_error(res, 400, "tag level exceeds max depth", req.isKeepAlive());
				return true;
			}
		}

		std::map<std::string, TagRow> subtree_rows;
		int max_relative_level = 1;
		for (size_t i = 0; i < subtree_ids.size(); ++i) {
			TagRow item;
			if (!fetch_tag_locked(db, subtree_ids[i], &item, db_err)) {
				json_error(res, 500,
					db_err.empty() ? "tag not found" : db_err.c_str(),
					req.isKeepAlive());
				return true;
			}
			subtree_rows[item.id] = item;
		}
		for (size_t i = 0; i < subtree_ids.size(); ++i) {
			int relative_level = 1;
			std::string current = subtree_ids[i];
			while (current != tag_id) {
				std::map<std::string, TagRow>::const_iterator it =
					subtree_rows.find(current);
				if (it == subtree_rows.end() || it->second.parent_id.empty()) {
					break;
				}
				current = it->second.parent_id;
				relative_level++;
			}
			if (relative_level > max_relative_level) {
				max_relative_level = relative_level;
			}
		}
		if (target_level + max_relative_level > 3) {
			json_error(res, 400, "tag level exceeds max depth", req.isKeepAlive());
			return true;
		}

		long long sort_order = next_sort_order_locked(db, parent_id, db_err);
		if (sort_order <= 0) {
			json_error(res, 500, db_err.c_str(), req.isKeepAlive());
			return true;
		}

		acl::query query;
		query.create("UPDATE tag_catalog"
			" SET parent_id=:parent_id, sort_order=:sort_order,"
			" updated_at=strftime('%s','now')"
			" WHERE id=:id")
			.set_parameter("parent_id", parent_id.c_str())
			.set_parameter("sort_order", sort_order)
			.set_parameter("id", tag_id.c_str());
		if (!db.exec_update(query)) {
			json_error(res, 500, db.get_error(), req.isKeepAlive());
			return true;
		}
		row.parent_id = parent_id;
		row.sort_order = sort_order;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("id", tag_id.c_str());
	root.add_text("parent_id", row.parent_id.c_str());
	root.add_text("message", "tag moved");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

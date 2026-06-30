#include "stdafx.h"
#include "tag_common.h"

namespace action {

bool TagListAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string db_err;
	if (!ensure_tag_db_for_request(upload_dir, db_err)) {
		json_error(res, 500, db_err.c_str(), req.isKeepAlive());
		return true;
	}

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

	acl::query query;
	query.create("SELECT id, parent_id, tag_name, sort_order"
		" FROM tag_catalog ORDER BY parent_id ASC, sort_order ASC, created_at ASC");
	if (!db.exec_select(query)) {
		json_error(res, 500, db.get_error(), req.isKeepAlive());
		return true;
	}

	std::map<std::string, TagRow> rows_by_id;
	std::map<std::string, std::vector<std::string> > children_by_parent;
	std::map<std::string, long long> direct_file_counts;
	std::vector<std::string> roots;
	for (size_t i = 0; i < db.length(); ++i) {
		const acl::db_row* row = db[i];
		if (row == NULL) {
			continue;
		}
		TagRow item;
		const char* id = (*row)["id"];
		const char* parent_id = (*row)["parent_id"];
		const char* tag_name = (*row)["tag_name"];
		const char* sort_order = (*row)["sort_order"];
		item.id = id ? id : "";
		item.parent_id = parent_id ? parent_id : "";
		item.name = tag_name ? tag_name : "";
		item.sort_order = sort_order ? atoll(sort_order) : 0;
		if (item.id.empty()) {
			continue;
		}
		rows_by_id[item.id] = item;
		if (item.parent_id.empty()) {
			roots.push_back(item.id);
		} else {
			children_by_parent[item.parent_id].push_back(item.id);
		}
	}
	db.free_result();

	acl::query count_query;
	count_query.create("SELECT tag_id, COUNT(*) AS file_count"
		" FROM file_tag_rel GROUP BY tag_id");
	if (!db.exec_select(count_query)) {
		json_error(res, 500, db.get_error(), req.isKeepAlive());
		return true;
	}
	for (size_t i = 0; i < db.length(); ++i) {
		const acl::db_row* count_row = db[i];
		if (count_row == NULL) {
			continue;
		}
		const char* tag_id = (*count_row)["tag_id"];
		const char* file_count = (*count_row)["file_count"];
		if (tag_id != NULL && *tag_id != '\0') {
			direct_file_counts[tag_id] = file_count ? atoll(file_count) : 0;
		}
	}
	db.free_result();

	std::sort(roots.begin(), roots.end(),
		[&rows_by_id](const std::string& left, const std::string& right) {
			std::map<std::string, TagRow>::const_iterator lit = rows_by_id.find(left);
			std::map<std::string, TagRow>::const_iterator rit = rows_by_id.find(right);
			if (lit == rows_by_id.end() || rit == rows_by_id.end()) {
				return left < right;
			}
			const TagRow& lrow = lit->second;
			const TagRow& rrow = rit->second;
			const int lprio = root_tag_priority(lrow);
			const int rprio = root_tag_priority(rrow);
			if (lprio != rprio) {
				return lprio < rprio;
			}
			if (lrow.sort_order != rrow.sort_order) {
				return lrow.sort_order < rrow.sort_order;
			}
			return lrow.id < rrow.id;
		});

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	acl::json_node& arr = json.create_array();
	root.add_child("tags", arr);
	for (size_t i = 0; i < roots.size(); ++i) {
		std::map<std::string, TagRow>::const_iterator it = rows_by_id.find(roots[i]);
		if (it == rows_by_id.end()) {
			continue;
		}
			append_tag_json(upload_dir, json, arr, it->second,
				children_by_parent, rows_by_id, direct_file_counts);
	}
	root.add_number("count", (long long) rows_by_id.size());
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

#include "stdafx.h"
#include "tag_common.h"

namespace action {

bool TagFilesAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string db_err;
	if (!ensure_tag_db_for_request(upload_dir, db_err)) {
		json_error(res, 500, db_err.c_str(), req.isKeepAlive());
		return true;
	}

	std::string tag_id = trim_copy(req.getParameter("tag_id"));
	if (!validate_tag_id(tag_id, db_err)) {
		json_error(res, 400, db_err.c_str(), req.isKeepAlive());
		return true;
	}

	std::vector<std::string> file_names;
	std::string tag_name;
	{
		std::lock_guard<std::mutex> guard(g_tag_mutex);
		acl::db_sqlite db(tag_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
		if (!open_tag_db_locked(db, db_err)) {
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
		tag_name = row.name;
		bool tag_allowed = false;
		std::string tag_lock_err;
		if (!named_lock_verify(upload_dir, tag_lock_key(tag_id),
			req.getParameter("tag_password") ? req.getParameter("tag_password") : "",
			tag_allowed, tag_lock_err))
		{
			json_error(res, 500, tag_lock_err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!tag_allowed) {
			json_error(res, 403, "tag is locked", req.isKeepAlive());
			return true;
		}

		acl::query query;
		query.create("SELECT file_name FROM file_tag_rel WHERE tag_id=:tag_id"
			" ORDER BY updated_at DESC, file_name ASC")
			.set_parameter("tag_id", tag_id.c_str());
		if (!db.exec_select(query)) {
			json_error(res, 500, db.get_error(), req.isKeepAlive());
			return true;
		}
		for (size_t i = 0; i < db.length(); ++i) {
			const acl::db_row* file_row = db[i];
			if (file_row == NULL) {
				continue;
			}
			const char* file_name = (*file_row)["file_name"];
			if (file_name != NULL && *file_name != '\0') {
				file_names.push_back(file_name);
			}
		}
		db.free_result();
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("tag_id", tag_id.c_str());
	root.add_text("tag_name", tag_name.c_str());
	acl::json_node& files = json.create_array();
	root.add_child("files", files);
	long long count = 0;

	for (size_t i = 0; i < file_names.size(); ++i) {
		std::string file_path;
		const bool is_local_file = is_local_tag_file_name(file_names[i]);
		if (is_local_file) {
			file_path = local_tag_path_from_storage_name(file_names[i]);
		} else if (!normalize_relative_path(file_names[i].c_str(), file_path, db_err, false)) {
			continue;
		}

		const std::string full = is_local_file ? file_path : join_upload_path(upload_dir, file_path);
		struct stat st;
		if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
			continue;
		}

		long long fsize = regular_file_size(full);

		char uploaded_time[32];
		uploaded_time[0] = '\0';
		format_upload_time(st.st_mtime, uploaded_time, sizeof(uploaded_time));

		acl::json_node& item = files.add_child(false, true);
		item.add_text("name", base_name_from_relative_path(file_path).c_str());
		item.add_text("path", file_path.c_str());
		item.add_text("folder_path", (is_local_file ? tag_local_parent_path(file_path) : parent_relative_path(file_path)).c_str());
		item.add_bool("local", is_local_file);
		bool file_locked = false;
		std::string lock_key = is_local_file
			? (std::string("local:") + file_path)
			: (std::string("remote:") + file_path);
		std::string lock_err;
		if (file_lock_path_has_lock(upload_dir, lock_key, file_locked, lock_err)) {
			item.add_bool("locked", file_locked);
		}
		item.add_number("size", fsize);
		item.add_number("uploaded_at", (long long) st.st_mtime);
		item.add_text("uploaded_time", uploaded_time);
		count++;
	}

	root.add_number("count", count);
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

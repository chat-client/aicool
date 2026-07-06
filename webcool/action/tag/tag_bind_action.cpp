#include "stdafx.h"
#include "tag_common.h"

namespace action {

bool TagBindAction::run(request_t& req, response_t& res,
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

	const char* file = req.getParameter("file");
	if (file == NULL || *file == '\0') {
		json_error(res, 400, "missing query parameter: file", req.isKeepAlive());
		return true;
	}

	const bool is_local_file = req.getParameter("local") != NULL
		&& strcmp(req.getParameter("local"), "1") == 0;
	std::string file_path;
	std::string stored_file_name;
	if (is_local_file) {
		if (!normalize_existing_local_file_path(file, file_path, db_err)) {
			json_error(res, 400, db_err.c_str(), req.isKeepAlive());
			return true;
		}
		stored_file_name = local_tag_storage_name(file_path);
	} else {
		if (!normalize_relative_path(file, file_path, db_err, false)) {
			json_error(res, 400, db_err.c_str(), req.isKeepAlive());
			return true;
		}

		if (!file_exists_in_upload_dir(upload_dir, file_path.c_str())) {
			json_error(res, 404, "file not found", req.isKeepAlive());
			return true;
		}
		stored_file_name = file_path;
	}

	{
		std::lock_guard<webcool::mutex> guard(g_tag_mutex);
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

		TagRow root_row;
		if (!get_root_tag_locked(db, tag_id, &root_row, db_err)) {
			json_error(res, 500, db_err.c_str(), req.isKeepAlive());
			return true;
		}
		if (root_row.name == g_default_video_tag_name
			&& !is_video_file_name(file_path))
		{
			json_error(res, 400,
				"video tag can only bind video files",
				req.isKeepAlive());
			return true;
		}
		if (root_row.name == g_default_audio_tag_name
			&& !is_audio_file_name(file_path))
		{
			json_error(res, 400,
				"audio tag can only bind audio files",
				req.isKeepAlive());
			return true;
		}
		if (root_row.name == g_default_image_tag_name
			&& !is_image_file_name(file_path))
		{
			json_error(res, 400,
				"image tag can only bind image files",
				req.isKeepAlive());
			return true;
		}
		if (root_row.name == g_default_document_tag_name
			&& !is_document_file_name(file_path))
		{
			json_error(res, 400,
				"document tag can only bind document files",
				req.isKeepAlive());
			return true;
		}

		acl::query query;
		query.create("INSERT OR REPLACE INTO file_tag_rel(tag_id, file_name, updated_at)"
			" VALUES(:tag_id, :file_name, strftime('%s','now'))")
			.set_parameter("tag_id", tag_id.c_str())
			.set_parameter("file_name", stored_file_name.c_str());
		if (!db.exec_update(query)) {
			json_error(res, 500, db.get_error(), req.isKeepAlive());
			return true;
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("tag_id", tag_id.c_str());
	root.add_text("file", file_path.c_str());
	root.add_bool("local", is_local_file);
	root.add_text("message", "file bound to tag");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool TagUnbindAction::run(request_t& req, response_t& res,
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

	const char* file = req.getParameter("file");
	if (file == NULL || *file == '\0') {
		json_error(res, 400, "missing query parameter: file", req.isKeepAlive());
		return true;
	}

	const bool is_local_file = req.getParameter("local") != NULL
		&& strcmp(req.getParameter("local"), "1") == 0;
	std::string file_path;
	std::string stored_file_name;
	if (is_local_file) {
		if (!normalize_existing_local_file_path(file, file_path, db_err)) {
			json_error(res, 400, db_err.c_str(), req.isKeepAlive());
			return true;
		}
		stored_file_name = local_tag_storage_name(file_path);
	} else {
		if (!normalize_relative_path(file, file_path, db_err, false)) {
			json_error(res, 400, db_err.c_str(), req.isKeepAlive());
			return true;
		}
		stored_file_name = file_path;
	}

	{
		std::lock_guard<webcool::mutex> guard(g_tag_mutex);
		acl::db_sqlite db(tag_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
		if (!open_tag_db_locked(db, db_err)) {
			json_error(res, 500, db_err.c_str(), req.isKeepAlive());
			return true;
		}

		acl::query query;
		query.create("DELETE FROM file_tag_rel WHERE tag_id=:tag_id AND file_name=:file_name")
			.set_parameter("tag_id", tag_id.c_str())
			.set_parameter("file_name", stored_file_name.c_str());
		if (!db.exec_update(query)) {
			json_error(res, 500, db.get_error(), req.isKeepAlive());
			return true;
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("tag_id", tag_id.c_str());
	root.add_text("file", file_path.c_str());
	root.add_bool("local", is_local_file);
	root.add_text("message", "file unbound from tag");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

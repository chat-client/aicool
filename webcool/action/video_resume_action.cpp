#include "actions.h"
#include "action_util.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include "../platform_compat.h"
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <mutex>
#include <string>
#include <vector>

namespace action {

static std::mutex g_resume_mutex;
static std::string g_resume_db_file;
static bool g_resume_db_ready = false;
static const char* g_video_resume_create_table_sql =
	"CREATE TABLE IF NOT EXISTS video_resume ("
	"file_name TEXT PRIMARY KEY NOT NULL,"
	"position_ms INTEGER NOT NULL DEFAULT 0,"
	"updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
	")";
static const char* g_local_resume_prefix = "local:";

static std::string resume_db_file_for_upload_dir(const std::string& upload_dir)
{
	acl::string path;
	path.format("%s/.video_resume.db", upload_dir.c_str());
	return std::string(path.c_str());
}

static bool is_absolute_local_resume_path(const char* path)
{
	if (path == NULL || *path == '\0') {
		return false;
	}
#ifdef _WIN32
	return (strlen(path) >= 3
			&& ((path[0] >= 'A' && path[0] <= 'Z')
				|| (path[0] >= 'a' && path[0] <= 'z'))
			&& path[1] == ':'
			&& (path[2] == '/' || path[2] == '\\'))
		|| (strlen(path) >= 2
			&& (path[0] == '/' || path[0] == '\\')
			&& (path[1] == '/' || path[1] == '\\'));
#else
	return path[0] == '/';
#endif
}

static bool normalize_video_resume_file_key(const char* file,
	std::string& key, std::string& err)
{
	err.clear();
	key.clear();
	if (file == NULL || *file == '\0') {
		err = "missing query parameter: file";
		return false;
	}
	const size_t prefix_len = strlen(g_local_resume_prefix);
	if (strncmp(file, g_local_resume_prefix, prefix_len) == 0) {
		const char* local_path = file + prefix_len;
		if (!is_absolute_local_resume_path(local_path)) {
			err = "absolute local path is required";
			return false;
		}
		struct stat st;
		if (stat(local_path, &st) != 0 || !S_ISREG(st.st_mode)) {
			err = "file not found";
			return false;
		}
		key = std::string(g_local_resume_prefix) + local_path;
		return true;
	}
	return normalize_relative_path(file, key, err, false);
}

static bool parse_non_negative_i64(const char* text, long long& out) {
	if (text == NULL || *text == '\0') {
		return false;
	}

	errno = 0;
	char* end = NULL;
	long long v = strtoll(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || v < 0) {
		return false;
	}
	out = v;
	return true;
}

static void json_error(response_t& res, int status, const char* msg,
	bool keep_alive)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", msg ? msg : "unknown error");
	sendJson(res, status, root, keep_alive);
}

static bool ensure_resume_table_exists_locked(std::string& err) {
	err.clear();
	if (g_resume_db_file.empty()) {
		err = "resume database file is empty";
		return false;
	}

	acl::db_sqlite db(g_resume_db_file.c_str(), "utf-8");
	if (!db.open()) {
		err = db.get_error();
		return false;
	}

	db.set_busy_timeout(3000);
	acl::query query;
	query.create(g_video_resume_create_table_sql);
	if (!db.exec_update(query)) {
		err = db.get_error();
		return false;
	}

	return true;
}

static bool ensure_video_resume_db_for_request(const std::string& upload_dir,
	std::string& err)
{
	err.clear();

	acl::string expected_db_file;
	expected_db_file.format("%s/.video_resume.db", upload_dir.c_str());
	if (!g_resume_db_ready || g_resume_db_file != expected_db_file.c_str()) {
		if (!init_video_resume_db(upload_dir, err)) {
			return false;
		}
	}

	std::lock_guard<std::mutex> guard(g_resume_mutex);
	if (!ensure_resume_table_exists_locked(err)) {
		g_resume_db_ready = false;
		return false;
	}

	g_resume_db_ready = true;
	return true;
}

bool init_video_resume_db(const std::string& upload_dir, std::string& err) {
	err.clear();

	std::lock_guard<std::mutex> guard(g_resume_mutex);
	acl::string next_db_file;
	next_db_file.format("%s/.video_resume.db", upload_dir.c_str());
	if (g_resume_db_ready && g_resume_db_file == next_db_file.c_str()) {
		return true;
	}

	std::string sqlite_lib_path = choose_sqlite_lib_path();
	if (sqlite_lib_path.empty()) {
		err = "sqlite dynamic library not found";
		return false;
	}

	acl::db_handle::set_loadpath(sqlite_lib_path.c_str());

	g_resume_db_file = next_db_file.c_str();

	if (!ensure_resume_table_exists_locked(err)) {
		return false;
	}

	g_resume_db_ready = true;
	return true;
}

bool video_resume_rename_file(const std::string& upload_dir,
	const std::string& old_file_name, const std::string& new_file_name,
	std::string& err)
{
	err.clear();
	if (old_file_name.empty() || new_file_name.empty() || old_file_name == new_file_name) {
		return true;
	}
	if (!ensure_video_resume_db_for_request(upload_dir, err)) {
		return false;
	}

	std::lock_guard<std::mutex> guard(g_resume_mutex);
	acl::db_sqlite db(resume_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
	if (!db.open()) {
		err = db.get_error();
		return false;
	}
	db.set_busy_timeout(3000);

	acl::query delete_query;
	delete_query.create("DELETE FROM video_resume WHERE file_name=:new_file")
		.set_parameter("new_file", new_file_name.c_str());
	if (!db.exec_update(delete_query)) {
		err = db.get_error();
		return false;
	}

	acl::query update_query;
	update_query.create("UPDATE video_resume SET file_name=:new_file, updated_at=strftime('%s','now')"
		" WHERE file_name=:old_file")
		.set_parameter("new_file", new_file_name.c_str())
		.set_parameter("old_file", old_file_name.c_str());
	if (!db.exec_update(update_query)) {
		err = db.get_error();
		return false;
	}
	return true;
}

bool video_resume_rename_folder_prefix(const std::string& upload_dir,
	const std::string& old_prefix, const std::string& new_prefix,
	std::string& err)
{
	err.clear();
	if (old_prefix.empty() || new_prefix.empty() || old_prefix == new_prefix) {
		return true;
	}
	if (!ensure_video_resume_db_for_request(upload_dir, err)) {
		return false;
	}

	std::lock_guard<std::mutex> guard(g_resume_mutex);
	acl::db_sqlite db(resume_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
	if (!db.open()) {
		err = db.get_error();
		return false;
	}
	db.set_busy_timeout(3000);

	const std::string old_like = old_prefix + "/%";
	acl::query delete_query;
	delete_query.create("DELETE FROM video_resume "
		"WHERE file_name IN ("
			"SELECT :new_prefix || substr(file_name, length(:old_prefix) + 1) "
			"FROM video_resume WHERE file_name LIKE :old_like"
		")")
		.set_parameter("new_prefix", new_prefix.c_str())
		.set_parameter("old_prefix", old_prefix.c_str())
		.set_parameter("old_like", old_like.c_str());
	if (!db.exec_update(delete_query)) {
		err = db.get_error();
		return false;
	}

	acl::query update_query;
	update_query.create("UPDATE video_resume "
		"SET file_name=:new_prefix || substr(file_name, length(:old_prefix) + 1), "
		"updated_at=strftime('%s','now') "
		"WHERE file_name LIKE :old_like")
		.set_parameter("new_prefix", new_prefix.c_str())
		.set_parameter("old_prefix", old_prefix.c_str())
		.set_parameter("old_like", old_like.c_str());
	if (!db.exec_update(update_query)) {
		err = db.get_error();
		return false;
	}
	return true;
}

bool VideoResumeGetAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string db_err;
	if (!ensure_video_resume_db_for_request(upload_dir, db_err)) {
		json_error(res, 500, db_err.c_str(), req.isKeepAlive());
		return true;
	}

	const char* file = req.getParameter("file");
	if (file == NULL || *file == '\0') {
		json_error(res, 400, "missing query parameter: file", req.isKeepAlive());
		return true;
	}

	std::string file_path;
	if (!normalize_video_resume_file_key(file, file_path, db_err)) {
		json_error(res, 400, db_err.c_str(), req.isKeepAlive());
		return true;
	}

	{
		std::lock_guard<std::mutex> guard(g_resume_mutex);
		if (!g_resume_db_ready || g_resume_db_file.empty()) {
			json_error(res, 500, "resume database not initialized", req.isKeepAlive());
			return true;
		}

		acl::db_sqlite db(resume_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
		if (!db.open()) {
			json_error(res, 500, db.get_error(), req.isKeepAlive());
			return true;
		}
		db.set_busy_timeout(3000);

		acl::query query;
		query.create("SELECT position_ms FROM video_resume WHERE file_name=:file")
			.set_parameter("file", file_path.c_str());

		if (!db.exec_select(query)) {
			json_error(res, 500, db.get_error(), req.isKeepAlive());
			return true;
		}

		long long position_ms = 0;
		bool found = false;
		if (!db.empty()) {
			const acl::db_row* row = db.get_first_row();
			if (row != NULL) {
				const char* v = (*row)["position_ms"];
				if (v != NULL) {
					position_ms = atoll(v);
					if (position_ms < 0) {
						position_ms = 0;
					}
				}
				found = true;
			}
		}
		db.free_result();

		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("file", file_path.c_str());
		root.add_bool("found", found);
		root.add_number("position_ms", position_ms);
		return sendJson(res, 200, root, req.isKeepAlive());
	}
}

bool VideoResumeSetAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string db_err;
	if (!ensure_video_resume_db_for_request(upload_dir, db_err)) {
		json_error(res, 500, db_err.c_str(), req.isKeepAlive());
		return true;
	}

	const char* file = req.getParameter("file");
	if (file == NULL || *file == '\0') {
		json_error(res, 400, "missing query parameter: file", req.isKeepAlive());
		return true;
	}

	std::string file_path;
	if (!normalize_video_resume_file_key(file, file_path, db_err)) {
		json_error(res, 400, db_err.c_str(), req.isKeepAlive());
		return true;
	}

	long long position_ms = 0;
	const char* position = req.getParameter("position_ms");
	if (!parse_non_negative_i64(position, position_ms)) {
		json_error(res, 400, "invalid query parameter: position_ms", req.isKeepAlive());
		return true;
	}

	{
		std::lock_guard<std::mutex> guard(g_resume_mutex);
		if (!g_resume_db_ready || g_resume_db_file.empty()) {
			json_error(res, 500, "resume database not initialized", req.isKeepAlive());
			return true;
		}

		acl::db_sqlite db(resume_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
		if (!db.open()) {
			json_error(res, 500, db.get_error(), req.isKeepAlive());
			return true;
		}
		db.set_busy_timeout(3000);

		acl::query query;
		query.create(
			"INSERT OR REPLACE INTO video_resume(file_name, position_ms, updated_at) "
			"VALUES(:file, :position_ms, strftime('%s','now')) "
			)
			.set_parameter("file", file_path.c_str())
			.set_parameter("position_ms", position_ms);

		if (!db.exec_update(query)) {
			json_error(res, 500, db.get_error(), req.isKeepAlive());
			return true;
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("file", file_path.c_str());
	root.add_number("position_ms", position_ms);
	root.add_text("message", "resume position saved");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

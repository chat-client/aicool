#include "actions.h"
#include "action_util.h"

#ifdef _WIN32
#include "../platform_compat.h"
#else
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <time.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <stdlib.h>
#include <string>
#include <vector>

namespace action {

namespace {

static std::mutex g_recycle_mutex;
static std::string g_recycle_db_file;
static bool g_recycle_db_ready = false;
static unsigned long g_recycle_seq = 0;

static std::string recycle_db_file_for_upload_dir(const std::string& upload_dir)
{
	acl::string path;
	path.format("%s/.recycle_bin.db", upload_dir.c_str());
	return std::string(path.c_str());
}

static const char* g_recycle_table_create_sql =
	"CREATE TABLE IF NOT EXISTS recycle_bin ("
	"id INTEGER PRIMARY KEY AUTOINCREMENT,"
	"recycle_name TEXT NOT NULL UNIQUE,"
	"original_path TEXT NOT NULL,"
	"original_name TEXT NOT NULL,"
	"deleted_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
	")";

static const char* g_recycle_table_index_sql =
	"CREATE INDEX IF NOT EXISTS idx_recycle_bin_deleted_at"
	" ON recycle_bin(deleted_at DESC)";

struct file_entry_t {
	std::string name;
	std::string path;
	std::string folder_path;
	std::string recycle_original_name;
	std::string recycle_original_path;
	long long size;
	long long uploaded_at;
	std::string uploaded_time;
	bool directory;
	bool locked;
};

struct recycle_record_t {
	std::string original_path;
	std::string original_name;
};

static void format_upload_time(time_t ts, char* buf, size_t size);
static bool should_skip_entry(const char* name, bool show_hidden);

static bool seek_file64(FILE* fp, long long offset)
{
#ifdef _WIN32
	return _fseeki64(fp, offset, SEEK_SET) == 0;
#else
	return fseeko(fp, (off_t) offset, SEEK_SET) == 0;
#endif
}

static std::string remote_file_lock_key(const std::string& path) {
	return std::string("remote:") + path;
}

static bool ensure_recycle_tables_locked(std::string& err) {
	err.clear();
	if (g_recycle_db_file.empty()) {
		err = "recycle database file is empty";
		return false;
	}

	acl::db_sqlite db(g_recycle_db_file.c_str(), "utf-8");
	if (!db.open()) {
		err = db.get_error();
		return false;
	}
	db.set_busy_timeout(3000);

	acl::query q1;
	q1.create(g_recycle_table_create_sql);
	if (!db.exec_update(q1)) {
		err = db.get_error();
		return false;
	}

	acl::query q2;
	q2.create(g_recycle_table_index_sql);
	if (!db.exec_update(q2)) {
		err = db.get_error();
		return false;
	}

	return true;
}

static bool ensure_recycle_db_for_request(const std::string& upload_dir,
	std::string& err)
{
	err.clear();
	acl::string expected_db_file;
	expected_db_file.format("%s/.recycle_bin.db", upload_dir.c_str());
	if (!g_recycle_db_ready || g_recycle_db_file != expected_db_file.c_str()) {
		if (!init_recycle_bin_db(upload_dir, err)) {
			return false;
		}
	}

	std::lock_guard<std::mutex> guard(g_recycle_mutex);
	if (!ensure_recycle_tables_locked(err)) {
		g_recycle_db_ready = false;
		return false;
	}
	g_recycle_db_ready = true;
	return true;
}

static std::string make_recycle_unique_name(const std::string& original_name) {
	const time_t now = time(NULL);
	char buf[128];
	++g_recycle_seq;
	snprintf(buf, sizeof(buf), "%lld_%d_%lu_%s",
		(long long) now, (int) getpid(), g_recycle_seq, original_name.c_str());
	return std::string(buf);
}

static std::string build_restore_candidate_path(const std::string& original_path,
	int attempt)
{
	if (attempt <= 0) {
		return original_path;
	}

	const std::string parent = parent_relative_path(original_path);
	const std::string base = base_name_from_relative_path(original_path);
	std::string stem = base;
	std::string ext;
	const std::string::size_type dot = base.rfind('.');
	if (dot != std::string::npos && dot > 0) {
		stem = base.substr(0, dot);
		ext = base.substr(dot);
	}

	char suffix[48];
	snprintf(suffix, sizeof(suffix), " (restore %d)", attempt);
	const std::string renamed = stem + suffix + ext;
	return parent.empty() ? renamed : (parent + "/" + renamed);
}

static bool alloc_recycle_target_path(const std::string& upload_dir,
	const std::string& original_name, std::string& recycle_rel, std::string& err)
{
	err.clear();
	recycle_rel.clear();
	std::string path = join_upload_path(upload_dir, recycle_folder_name());
	if (!make_dir_recursive(path.c_str())) {
		err = "cannot access recycle folder: ";
		err += path.c_str();
		return false;
	}

	for (int i = 0; i < 1024; ++i) {
		std::string unique_name = make_recycle_unique_name(original_name);
		std::string candidate = std::string(recycle_folder_name()) + "/" + unique_name;
		if (!upload_regular_file_exists(upload_dir, candidate)
			&& !upload_directory_exists(upload_dir, candidate))
		{
			recycle_rel = candidate;
			return true;
		}
	}

	err = "cannot allocate recycle file name";
	return false;
}

static bool insert_recycle_record(const std::string& upload_dir,
	const std::string& recycle_rel, const std::string& original_path,
	std::string& err)
{
	err.clear();
	if (!ensure_recycle_db_for_request(upload_dir, err)) {
		return false;
	}

	const std::string recycle_name = base_name_from_relative_path(recycle_rel);
	const std::string original_name = base_name_from_relative_path(original_path);

	std::lock_guard<std::mutex> guard(g_recycle_mutex);
	acl::db_sqlite db(recycle_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
	if (!db.open()) {
		err = db.get_error();
		return false;
	}
	db.set_busy_timeout(3000);

	acl::query q;
	q.create("INSERT INTO recycle_bin(recycle_name, original_path, original_name, deleted_at)"
		" VALUES(:recycle_name, :original_path, :original_name, strftime('%s','now'))")
		.set_parameter("recycle_name", recycle_name.c_str())
		.set_parameter("original_path", original_path.c_str())
		.set_parameter("original_name", original_name.c_str());
	if (!db.exec_update(q)) {
		err = db.get_error();
		return false;
	}
	return true;
}

static bool load_recycle_records_map(const std::string& upload_dir,
	std::map<std::string, recycle_record_t>& out, std::string& err)
{
	err.clear();
	out.clear();
	if (!ensure_recycle_db_for_request(upload_dir, err)) {
		return false;
	}

	std::lock_guard<std::mutex> guard(g_recycle_mutex);
	acl::db_sqlite db(recycle_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
	if (!db.open()) {
		err = db.get_error();
		return false;
	}
	db.set_busy_timeout(3000);

	acl::query q;
	q.create("SELECT recycle_name, original_path, original_name FROM recycle_bin");
	if (!db.exec_select(q)) {
		err = db.get_error();
		return false;
	}

	for (size_t i = 0; i < db.length(); ++i) {
		const acl::db_row* row = db[i];
		if (row == NULL) {
			continue;
		}
		const char* recycle_name = (*row)["recycle_name"];
		if (recycle_name == NULL || *recycle_name == '\0') {
			continue;
		}
		recycle_record_t rec;
		const char* original_path = (*row)["original_path"];
		const char* original_name = (*row)["original_name"];
		rec.original_path = original_path ? original_path : "";
		rec.original_name = original_name ? original_name : "";
		out[recycle_name] = rec;
	}
	db.free_result();
	return true;
}

static bool get_recycle_record(const std::string& upload_dir,
	const std::string& recycle_rel, recycle_record_t& rec,
	bool& found, std::string& err)
{
	err.clear();
	found = false;
	rec.original_name.clear();
	rec.original_path.clear();
	if (!ensure_recycle_db_for_request(upload_dir, err)) {
		return false;
	}

	const std::string recycle_name = base_name_from_relative_path(recycle_rel);
	std::lock_guard<std::mutex> guard(g_recycle_mutex);
	acl::db_sqlite db(recycle_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
	if (!db.open()) {
		err = db.get_error();
		return false;
	}
	db.set_busy_timeout(3000);

	acl::query q;
	q.create("SELECT original_path, original_name FROM recycle_bin WHERE recycle_name=:recycle_name")
		.set_parameter("recycle_name", recycle_name.c_str());
	if (!db.exec_select(q)) {
		err = db.get_error();
		return false;
	}

	if (!db.empty()) {
		const acl::db_row* row = db.get_first_row();
		if (row != NULL) {
			const char* original_path = (*row)["original_path"];
			const char* original_name = (*row)["original_name"];
			rec.original_path = original_path ? original_path : "";
			rec.original_name = original_name ? original_name : "";
			found = true;
		}
	}
	db.free_result();
	return true;
}

static bool resolve_restore_target_path(const std::string& upload_dir,
	const recycle_record_t& rec, std::string& target_path,
	std::string& err)
{
	err.clear();
	target_path.clear();
	if (rec.original_path.empty()) {
		err = "recycle record missing original path";
		return false;
	}
	if (is_recycle_file_path(rec.original_path)) {
		err = "invalid recycle record";
		return false;
	}

	for (int i = 0; i < 1024; ++i) {
		const std::string candidate = build_restore_candidate_path(rec.original_path, i);
		if (!upload_regular_file_exists(upload_dir, candidate)
			&& !upload_directory_exists(upload_dir, candidate))
		{
			target_path = candidate;
			return true;
		}
	}

	err = "cannot allocate restore target path";
	return false;
}

static bool delete_recycle_record(const std::string& upload_dir,
	const std::string& recycle_rel, std::string& err)
{
	err.clear();
	if (!ensure_recycle_db_for_request(upload_dir, err)) {
		return false;
	}

	const std::string recycle_name = base_name_from_relative_path(recycle_rel);
	std::lock_guard<std::mutex> guard(g_recycle_mutex);
	acl::db_sqlite db(recycle_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
	if (!db.open()) {
		err = db.get_error();
		return false;
	}
	db.set_busy_timeout(3000);

	acl::query q;
	q.create("DELETE FROM recycle_bin WHERE recycle_name=:recycle_name")
		.set_parameter("recycle_name", recycle_name.c_str());
	if (!db.exec_update(q)) {
		err = db.get_error();
		return false;
	}
	return true;
}

static bool collect_recycle_directory_entries(const std::string& upload_dir,
	std::map<std::string, recycle_record_t>& recycle_records,
	std::vector<file_entry_t>& out, std::string& err)
{
	err.clear();
	const std::string recycle_dir = join_upload_path(upload_dir, recycle_folder_name());
	DIR* dir = opendir(recycle_dir.c_str());
	if (dir == NULL) {
		if (errno == ENOENT) {
			return true;
		}
		err = strerror(errno);
		return false;
	}

	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (should_skip_entry(entry->d_name, false)) {
			continue;
		}
		const std::string recycle_name(entry->d_name);
		const std::string rel_path = std::string(recycle_folder_name()) + "/" + recycle_name;
		const std::string full_path = join_upload_path(upload_dir, rel_path);
		struct stat st;
		if (stat(full_path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
			continue;
		}

		std::map<std::string, recycle_record_t>::const_iterator it =
			recycle_records.find(recycle_name);
		if (it == recycle_records.end()) {
			continue;
		}

		char uploaded_time[32];
		uploaded_time[0] = '\0';
		format_upload_time(st.st_mtime, uploaded_time, sizeof(uploaded_time));

		file_entry_t item;
		item.name = it->second.original_name.empty()
			? recycle_name
			: it->second.original_name;
		item.path = rel_path;
		item.folder_path = recycle_folder_name();
		item.recycle_original_name = it->second.original_name;
		item.recycle_original_path = it->second.original_path;
		item.size = 0;
		item.uploaded_at = (long long) st.st_mtime;
		item.uploaded_time = uploaded_time;
		item.directory = true;
		item.locked = false;
		out.push_back(item);
	}
	closedir(dir);
	return true;
}

static bool delete_directory_recursive(const std::string& full_path,
	std::string& err)
{
	struct stat st;
	if (lstat(full_path.c_str(), &st) != 0) {
		if (errno == ENOENT) {
			return true;
		}
		err = strerror(errno);
		logger_error("stat %s error=%s", full_path.c_str(), err.c_str());
		return false;
	}

	if (!S_ISDIR(st.st_mode)) {
		if (::unlink(full_path.c_str()) == 0 || errno == ENOENT) {
			return true;
		}
		err = strerror(errno);
		logger_error("unlink %s error=%s", full_path.c_str(), err.c_str());
		return false;
	}

	DIR* dir = opendir(full_path.c_str());
	if (dir == NULL) {
		if (errno == ENOENT) {
			return true;
		}
		err = strerror(errno);
		logger_error("opendir %s error=%s", full_path.c_str(), err.c_str());
		return false;
	}

	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0
			|| strcmp(entry->d_name, "..") == 0)
		{
			continue;
		}
		const std::string child = full_path + "/" + entry->d_name;
		if (!delete_directory_recursive(child, err)) {
			closedir(dir);
			return false;
		}
	}
	closedir(dir);

	if (::rmdir(full_path.c_str()) == 0 || errno == ENOENT) {
		logger_debug(DEBUG_FILE, 1, "[DEBUG] remove %s ok",
			full_path.c_str());
		return true;
	}
	err = strerror(errno);
	logger_error("rmdir %s error=%s", full_path.c_str(), err.c_str());
	return false;
}

static bool remove_upload_path_recursive(const std::string& full_path,
	std::string& err)
{
	struct stat st;
	if (lstat(full_path.c_str(), &st) != 0) {
		if (errno == ENOENT) {
			return true;
		}
		err = strerror(errno);
		return false;
	}
	if (S_ISDIR(st.st_mode)) {
		return delete_directory_recursive(full_path, err);
	}
	if (::unlink(full_path.c_str()) != 0) {
		err = strerror(errno);
		return false;
	}
	return true;
}

static bool copy_regular_file_plain(const std::string& source,
	const std::string& dest, mode_t mode, std::string& err)
{
	FILE* in = fopen(source.c_str(), "rb");
	if (in == NULL) {
		err = strerror(errno);
		return false;
	}
	FILE* out = fopen(dest.c_str(), "wb");
	if (out == NULL) {
		err = strerror(errno);
		fclose(in);
		return false;
	}

	char buf[1024 * 64];
	bool ok = true;
	while (true) {
		const size_t n = fread(buf, 1, sizeof(buf), in);
		if (n > 0 && fwrite(buf, 1, n, out) != n) {
			err = strerror(errno);
			ok = false;
			break;
		}
		if (n < sizeof(buf)) {
			if (ferror(in)) {
				err = strerror(errno);
				ok = false;
			}
			break;
		}
	}
	if (fclose(out) != 0 && ok) {
		err = strerror(errno);
		ok = false;
	}
	fclose(in);
	if (!ok) {
		::unlink(dest.c_str());
		return false;
	}
	(void) chmod(dest.c_str(), mode & 0777);
	return true;
}

static bool soft_delete_to_recycle(const std::string& upload_dir,
	const std::string& file_path, std::string& recycle_path, std::string& err)
{
	err.clear();
	recycle_path.clear();

	const std::string original_name = base_name_from_relative_path(file_path);
	if (original_name.empty()) {
		err = "invalid file path";
		return false;
	}
	if (!alloc_recycle_target_path(upload_dir, original_name, recycle_path, err)) {
		return false;
	}

	const std::string from_full = join_upload_path(upload_dir, file_path);
	const std::string to_full = join_upload_path(upload_dir, recycle_path);
	if (::rename(from_full.c_str(), to_full.c_str()) != 0) {
		err = "move file to recycle bin failed";
		return false;
	}

	bool tag_renamed = false;
	if (!tag_rename_file(upload_dir, file_path, recycle_path, err)) {
		(void) ::rename(to_full.c_str(), from_full.c_str());
		return false;
	}
	tag_renamed = true;

	if (!video_resume_rename_file(upload_dir, file_path, recycle_path, err)
		|| !file_lock_rename_key(upload_dir, remote_file_lock_key(file_path),
			remote_file_lock_key(recycle_path), err))
	{
		if (tag_renamed) {
			std::string rollback_err;
			tag_rename_file(upload_dir, recycle_path, file_path, rollback_err);
		}
		(void) ::rename(to_full.c_str(), from_full.c_str());
		return false;
	}

	if (!insert_recycle_record(upload_dir, recycle_path, file_path, err)) {
		std::string rollback_err;
		file_lock_rename_key(upload_dir, remote_file_lock_key(recycle_path),
			remote_file_lock_key(file_path), rollback_err);
		video_resume_rename_file(upload_dir, recycle_path, file_path, rollback_err);
		tag_rename_file(upload_dir, recycle_path, file_path, rollback_err);
		(void) ::rename(to_full.c_str(), from_full.c_str());
		return false;
	}

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

static void format_upload_time(time_t ts, char* buf, size_t size) {
	if (buf == NULL || size == 0) {
		return;
	}
	if (ts <= 0) {
		buf[0] = '\0';
		return;
	}
	struct tm tmv;
	acl_localtime_r(&ts, &tmv);
	if (strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tmv) == 0) {
		buf[0] = '\0';
	}
}

static bool request_bool_param(request_t& req, const char* name) {
	const char* value = req.getParameter(name);
	return value != NULL && (
		strcmp(value, "1") == 0
		|| strcasecmp(value, "true") == 0
		|| strcasecmp(value, "yes") == 0
		|| strcasecmp(value, "on") == 0);
}

static bool should_skip_entry(const char* name, bool show_hidden) {
	if (name == NULL || *name == '\0') {
		return true;
	}
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
		return true;
	}
	if (!show_hidden && name[0] == '.') {
		return true;
	}
	return false;
}

static bool is_protected_project_db_file(const std::string& relative_dir,
	const char* name)
{
	if (!relative_dir.empty() || name == NULL) {
		return false;
	}
	return strcmp(name, ".video_resume.db") == 0
		|| strcmp(name, ".tag_catalog.db") == 0
		|| strcmp(name, ".recycle_bin.db") == 0
		|| strcmp(name, ".folder_catalog.db") == 0;
}

static bool is_image_file(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	return dot != NULL && (
		strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".jpg") == 0
		|| strcasecmp(dot, ".jpeg") == 0 || strcasecmp(dot, ".gif") == 0);
}

static bool is_video_file(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	return dot != NULL && (
		strcasecmp(dot, ".mp4") == 0 || strcasecmp(dot, ".avi") == 0
		|| strcasecmp(dot, ".mkv") == 0 || strcasecmp(dot, ".rmvb") == 0);
}

static bool is_audio_file(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	return dot != NULL && (
		strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".m4a") == 0
		|| strcasecmp(dot, ".aac") == 0 || strcasecmp(dot, ".wav") == 0
		|| strcasecmp(dot, ".ogg") == 0 || strcasecmp(dot, ".flac") == 0);
}

static bool is_text_file(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	return dot != NULL && (
		strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".md") == 0
		|| strcasecmp(dot, ".log") == 0 || strcasecmp(dot, ".csv") == 0
		|| strcasecmp(dot, ".json") == 0 || strcasecmp(dot, ".xml") == 0
		|| strcasecmp(dot, ".yaml") == 0 || strcasecmp(dot, ".yml") == 0
		|| strcasecmp(dot, ".ini") == 0 || strcasecmp(dot, ".conf") == 0
		|| strcasecmp(dot, ".c") == 0 || strcasecmp(dot, ".h") == 0
		|| strcasecmp(dot, ".cpp") == 0 || strcasecmp(dot, ".hpp") == 0
		|| strcasecmp(dot, ".cc") == 0 || strcasecmp(dot, ".java") == 0
		|| strcasecmp(dot, ".py") == 0 || strcasecmp(dot, ".js") == 0
		|| strcasecmp(dot, ".ts") == 0 || strcasecmp(dot, ".sh") == 0
		|| strcasecmp(dot, ".go") == 0 || strcasecmp(dot, ".sql") == 0
		|| strcasecmp(dot, ".proto") == 0);
}

static bool is_pdf_file(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	return dot != NULL && strcasecmp(dot, ".pdf") == 0;
}

static const char* image_content_type(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	if (dot == NULL) {
		return "application/octet-stream";
	}
	if (strcasecmp(dot, ".png") == 0) {
		return "image/png";
	}
	if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) {
		return "image/jpeg";
	}
	if (strcasecmp(dot, ".gif") == 0) {
		return "image/gif";
	}
	return "application/octet-stream";
}

static const char* video_content_type(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	if (dot == NULL) {
		return "application/octet-stream";
	}
	if (strcasecmp(dot, ".mp4") == 0) {
		return "video/mp4";
	}
	if (strcasecmp(dot, ".avi") == 0) {
		return "video/x-msvideo";
	}
	if (strcasecmp(dot, ".mkv") == 0) {
		return "video/x-matroska";
	}
	if (strcasecmp(dot, ".rmvb") == 0) {
		return "application/vnd.rn-realmedia-vbr";
	}
	return "application/octet-stream";
}

static const char* audio_content_type(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	if (dot == NULL) {
		return "application/octet-stream";
	}
	if (strcasecmp(dot, ".mp3") == 0) {
		return "audio/mpeg";
	}
	if (strcasecmp(dot, ".m4a") == 0) {
		return "audio/mp4";
	}
	if (strcasecmp(dot, ".aac") == 0) {
		return "audio/aac";
	}
	if (strcasecmp(dot, ".wav") == 0) {
		return "audio/wav";
	}
	if (strcasecmp(dot, ".ogg") == 0) {
		return "audio/ogg";
	}
	if (strcasecmp(dot, ".flac") == 0) {
		return "audio/flac";
	}
	return "application/octet-stream";
}

static const char* text_content_type(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	if (dot == NULL) {
		return "text/plain; charset=utf-8";
	}
	if (strcasecmp(dot, ".json") == 0) {
		return "application/json; charset=utf-8";
	}
	if (strcasecmp(dot, ".xml") == 0) {
		return "application/xml; charset=utf-8";
	}
	if (strcasecmp(dot, ".csv") == 0) {
		return "text/csv; charset=utf-8";
	}
	return "text/plain; charset=utf-8";
}

static const char* document_content_type(const char* filename) {
	return is_pdf_file(filename) ? "application/pdf" : "application/octet-stream";
}

static bool parse_range_value(const char* s, long long& out) {
	if (s == NULL || *s == '\0') {
		return false;
	}
	errno = 0;
	char* end = NULL;
	long long v = strtoll(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0' || v < 0) {
		return false;
	}
	out = v;
	return true;
}

static bool parse_range_header(const char* range, long long size,
	long long& begin, long long& end)
{
	if (range == NULL || size <= 0) {
		return false;
	}
	if (strncasecmp(range, "bytes=", 6) != 0) {
		return false;
	}
	const char* expr = range + 6;
	if (*expr == '\0' || strchr(expr, ',') != NULL) {
		return false;
	}
	const char* dash = strchr(expr, '-');
	if (dash == NULL) {
		return false;
	}
	if (dash == expr) {
		long long suffix = 0;
		if (!parse_range_value(dash + 1, suffix) || suffix <= 0) {
			return false;
		}
		if (suffix > size) {
			suffix = size;
		}
		begin = size - suffix;
		end = size - 1;
		return true;
	}

	acl::string left(expr, (size_t) (dash - expr));
	long long start = 0;
	if (!parse_range_value(left.c_str(), start) || start >= size) {
		return false;
	}
	if (*(dash + 1) == '\0') {
		begin = start;
		end = size - 1;
		return true;
	}

	long long stop = 0;
	if (!parse_range_value(dash + 1, stop) || stop < start) {
		return false;
	}
	if (stop >= size) {
		stop = size - 1;
	}
	begin = start;
	end = stop;
	return true;
}

static bool collect_files_recursive(const std::string& upload_dir,
	const std::string& relative_dir, const std::string& folder_password,
	std::vector<file_entry_t>& out, std::string& err, bool show_hidden)
{
	err.clear();
	const std::string full_dir = join_upload_path(upload_dir, relative_dir);
	DIR* dir = opendir(full_dir.c_str());
	if (dir == NULL) {
		err = strerror(errno);
		return false;
	}

	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (should_skip_entry(entry->d_name, show_hidden)) {
			continue;
		}
		if (is_protected_project_db_file(relative_dir, entry->d_name)) {
			continue;
		}
		const std::string name(entry->d_name);
		const std::string rel_path = relative_dir.empty() ? name : (relative_dir + "/" + name);
		const std::string full_path = join_upload_path(upload_dir, rel_path);
		struct stat st;
		if (stat(full_path.c_str(), &st) != 0) {
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			bool lock_allowed = false;
			std::string locked_path;
			if (!folder_lock_path_allows(upload_dir, rel_path, folder_password,
				lock_allowed, locked_path, err))
			{
				closedir(dir);
				return false;
			}
			if (!lock_allowed) {
				continue;
			}
			if (!collect_files_recursive(upload_dir, rel_path, folder_password, out, err, show_hidden)) {
				closedir(dir);
				return false;
			}
			continue;
		}
		if (!S_ISREG(st.st_mode)) {
			continue;
		}

		char uploaded_time[32];
		uploaded_time[0] = '\0';
		format_upload_time(st.st_mtime, uploaded_time, sizeof(uploaded_time));

		file_entry_t item;
		item.name = name;
		item.path = rel_path;
		item.folder_path = relative_dir;
		item.size = regular_file_size(full_path);
		item.uploaded_at = (long long) st.st_mtime;
		item.uploaded_time = uploaded_time;
		item.directory = false;
		item.locked = false;
		out.push_back(item);
	}
	closedir(dir);
	return true;
}

} // namespace

bool recycle_bin_insert_record(const std::string& upload_dir,
	const std::string& recycle_rel, const std::string& original_path,
	std::string& err)
{
	return insert_recycle_record(upload_dir, recycle_rel, original_path, err);
}

bool FilesAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	if (!make_dir_recursive(upload_dir.c_str())) {
		json_error(res, 500, "cannot access upload dir", req.isKeepAlive());
		return true;
	}

	std::string filter_folder;
	std::string err;
	const char* folder_text = req.getParameter("folder");
	if (folder_text != NULL && *folder_text != '\0'
		&& !normalize_relative_path(folder_text, filter_folder, err, true))
	{
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	const std::string folder_password = req.getParameter("folder_password")
		? req.getParameter("folder_password")
		: "";
	const bool show_hidden = request_bool_param(req, "show_hidden");
	bool lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, filter_folder, folder_password,
		lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!lock_allowed) {
		json_error(res, 403, "folder is locked", req.isKeepAlive());
		return true;
	}

	std::vector<file_entry_t> entries;
	if (!collect_files_recursive(upload_dir, filter_folder, folder_password, entries, err, show_hidden)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	std::sort(entries.begin(), entries.end(),
		[](const file_entry_t& a, const file_entry_t& b) {
			return a.path < b.path;
		});

	std::map<std::string, recycle_record_t> recycle_records;
	const bool recycle_root_view = filter_folder == recycle_folder_name();
	bool need_recycle_records = recycle_root_view;
	for (size_t i = 0; i < entries.size(); ++i) {
		if (is_recycle_file_path(entries[i].path)) {
			need_recycle_records = true;
			break;
		}
	}
	if (need_recycle_records && !load_recycle_records_map(upload_dir, recycle_records, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (recycle_root_view
		&& !collect_recycle_directory_entries(upload_dir, recycle_records, entries, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	std::sort(entries.begin(), entries.end(),
		[](const file_entry_t& a, const file_entry_t& b) {
			if (a.folder_path != b.folder_path) {
				return a.folder_path < b.folder_path;
			}
			if (a.directory != b.directory) {
				return a.directory && !b.directory;
			}
			return a.path < b.path;
		});

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	acl::json_node& files = json.create_array();
	root.add_child("files", files);
	long long count = 0;
	for (size_t i = 0; i < entries.size(); ++i) {
		file_entry_t item_ref = entries[i];
		if (is_recycle_file_path(item_ref.path)) {
			const std::string recycle_name = base_name_from_relative_path(item_ref.path);
			std::map<std::string, recycle_record_t>::const_iterator it = recycle_records.find(recycle_name);
			if (it != recycle_records.end()) {
				item_ref.recycle_original_name = it->second.original_name;
				item_ref.recycle_original_path = it->second.original_path;
				if (!item_ref.recycle_original_name.empty()) {
					item_ref.name = item_ref.recycle_original_name;
				}
			}
		}
		if (!filter_folder.empty() && item_ref.folder_path != filter_folder) {
			continue;
		}
		bool file_locked = false;
		std::string file_lock_err;
		if (file_lock_path_has_lock(upload_dir, remote_file_lock_key(item_ref.path),
			file_locked, file_lock_err))
		{
			item_ref.locked = file_locked;
		}
		acl::json_node& item = files.add_child(false, true);
		item.add_text("name", item_ref.name.c_str());
		item.add_text("path", item_ref.path.c_str());
		item.add_text("folder_path", item_ref.folder_path.c_str());
		item.add_text("recycle_original_name", item_ref.recycle_original_name.c_str());
		item.add_text("recycle_original_path", item_ref.recycle_original_path.c_str());
		item.add_number("size", item_ref.size);
		item.add_number("uploaded_at", item_ref.uploaded_at);
		item.add_text("uploaded_time", item_ref.uploaded_time.c_str());
		item.add_bool("directory", item_ref.directory);
		item.add_bool("locked", item_ref.locked);
		count++;
	}
	if (!filter_folder.empty()) {
		root.add_text("folder", filter_folder.c_str());
	}
	root.add_number("count", count);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool DeleteAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string file_path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("file"), file_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	const bool hard_delete = is_recycle_file_path(file_path);
	const bool target_is_dir = upload_directory_exists(upload_dir, file_path);
	if (!target_is_dir && !upload_regular_file_exists(upload_dir, file_path)) {
		json_error(res, 404, hard_delete ? "recycle item not found" : "file not found", req.isKeepAlive());
		return true;
	}
	if (target_is_dir && !hard_delete) {
		json_error(res, 400, "directory delete should use folder delete API", req.isKeepAlive());
		return true;
	}
	bool lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, parent_relative_path(file_path),
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!lock_allowed) {
		json_error(res, 403, "folder is locked", req.isKeepAlive());
		return true;
	}
	bool file_lock_allowed = false;
	if (!file_lock_path_allows(upload_dir, remote_file_lock_key(file_path),
		req.getParameter("file_password") ? req.getParameter("file_password") : "",
		file_lock_allowed, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!file_lock_allowed) {
		json_error(res, 403, "file is locked", req.isKeepAlive());
		return true;
	}

	if (hard_delete) {
		const std::string fullpath = join_upload_path(upload_dir, file_path);
		if (target_is_dir) {
			if (!delete_directory_recursive(fullpath, err)) {
				json_error(res, 500, err.c_str(), req.isKeepAlive());
				return true;
			}
		} else {
			if (::unlink(fullpath.c_str()) != 0) {
				json_error(res, 404, "file not found or delete failed", req.isKeepAlive());
				return true;
			}
		}

		err.clear();
		if (!delete_recycle_record(upload_dir, file_path, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!target_is_dir
			&& (!folder_unbind_file(upload_dir, file_path, err)
				|| !tag_unbind_file(upload_dir, file_path, err)))
		{
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	} else {
		std::string recycle_path;
		if (!soft_delete_to_recycle(upload_dir, file_path, recycle_path, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("file", file_path.c_str());
	root.add_text("message", hard_delete ? "deleted permanently" : "moved to recycle bin");
	root.add_bool("permanent", hard_delete);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool RestoreAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string file_path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("file"), file_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!is_recycle_file_path(file_path)) {
		json_error(res, 400, "restore only supports recycle files", req.isKeepAlive());
		return true;
	}
	const bool restore_is_dir = upload_directory_exists(upload_dir, file_path);
	if (!restore_is_dir && !upload_regular_file_exists(upload_dir, file_path)) {
		json_error(res, 404, "recycle item not found", req.isKeepAlive());
		return true;
	}

	recycle_record_t rec;
	bool found = false;
	if (!get_recycle_record(upload_dir, file_path, rec, found, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!found) {
		json_error(res, 404, "recycle record not found", req.isKeepAlive());
		return true;
	}

	std::string target_path;
	if (!resolve_restore_target_path(upload_dir, rec, target_path, err)) {
		json_error(res, 409, err.c_str(), req.isKeepAlive());
		return true;
	}

	const std::string target_parent = parent_relative_path(target_path);
	bool restore_lock_allowed = false;
	std::string restore_locked_path;
	if (!folder_lock_path_allows(upload_dir, target_parent,
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		restore_lock_allowed, restore_locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!restore_lock_allowed) {
		json_error(res, 403, "target folder is locked", req.isKeepAlive());
		return true;
	}
	if (!target_parent.empty()) {
		const std::string parent_full = join_upload_path(upload_dir, target_parent);
		if (!make_dir_recursive(parent_full.c_str())) {
			json_error(res, 500, "cannot restore target folder", req.isKeepAlive());
			return true;
		}
	}

	const std::string from_full = join_upload_path(upload_dir, file_path);
	const std::string to_full = join_upload_path(upload_dir, target_path);
	if (::rename(from_full.c_str(), to_full.c_str()) != 0) {
		json_error(res, 500, restore_is_dir ? "restore folder failed" : "restore file failed", req.isKeepAlive());
		return true;
	}

	bool tag_renamed = false;
	if (!(restore_is_dir
		? tag_rename_folder_prefix(upload_dir, file_path, target_path, err)
		: tag_rename_file(upload_dir, file_path, target_path, err)))
	{
		(void) ::rename(to_full.c_str(), from_full.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	tag_renamed = true;

	const bool rename_ok = restore_is_dir
		? (video_resume_rename_folder_prefix(upload_dir, file_path, target_path, err)
			&& folder_lock_rename_prefix(upload_dir, file_path, target_path, err))
		: (video_resume_rename_file(upload_dir, file_path, target_path, err)
			&& file_lock_rename_key(upload_dir, remote_file_lock_key(file_path),
				remote_file_lock_key(target_path), err));
	if (!rename_ok)
	{
		if (tag_renamed) {
			std::string rollback_err;
			if (restore_is_dir) {
				tag_rename_folder_prefix(upload_dir, target_path, file_path, rollback_err);
			} else {
				tag_rename_file(upload_dir, target_path, file_path, rollback_err);
			}
		}
		(void) ::rename(to_full.c_str(), from_full.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	if (!delete_recycle_record(upload_dir, file_path, err)) {
		std::string rollback_err;
		if (restore_is_dir) {
			folder_lock_rename_prefix(upload_dir, target_path, file_path, rollback_err);
			video_resume_rename_folder_prefix(upload_dir, target_path, file_path, rollback_err);
			tag_rename_folder_prefix(upload_dir, target_path, file_path, rollback_err);
		} else {
			file_lock_rename_key(upload_dir, remote_file_lock_key(target_path),
				remote_file_lock_key(file_path), rollback_err);
			video_resume_rename_file(upload_dir, target_path, file_path, rollback_err);
			tag_rename_file(upload_dir, target_path, file_path, rollback_err);
		}
		(void) ::rename(to_full.c_str(), from_full.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("file", file_path.c_str());
	root.add_text("path", target_path.c_str());
	root.add_bool("directory", restore_is_dir);
	root.add_text("message", "restored");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool MoveFileAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string file_path;
	std::string target_folder;
	std::string err;
	if (!normalize_relative_path(req.getParameter("file"), file_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!normalize_relative_path(req.getParameter("folder"), target_folder, err, true)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!upload_regular_file_exists(upload_dir, file_path)) {
		json_error(res, 404, "source file not found", req.isKeepAlive());
		return true;
	}
	if (!target_folder.empty() && !upload_directory_exists(upload_dir, target_folder)) {
		json_error(res, 404, "target folder not found", req.isKeepAlive());
		return true;
	}
	bool source_lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, parent_relative_path(file_path),
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		source_lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!source_lock_allowed) {
		json_error(res, 403, "source folder is locked", req.isKeepAlive());
		return true;
	}
	bool file_lock_allowed = false;
	if (!file_lock_path_allows(upload_dir, remote_file_lock_key(file_path),
		req.getParameter("file_password") ? req.getParameter("file_password") : "",
		file_lock_allowed, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!file_lock_allowed) {
		json_error(res, 403, "file is locked", req.isKeepAlive());
		return true;
	}
	bool target_lock_allowed = false;
	if (!folder_lock_path_allows(upload_dir, target_folder,
		req.getParameter("target_folder_password") ? req.getParameter("target_folder_password") : "",
		target_lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!target_lock_allowed) {
		json_error(res, 403, "target folder is locked", req.isKeepAlive());
		return true;
	}

	const std::string target_path = target_folder.empty()
		? base_name_from_relative_path(file_path)
		: (target_folder + "/" + base_name_from_relative_path(file_path));
	if (target_path == file_path) {
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("file", file_path.c_str());
		root.add_text("path", target_path.c_str());
		root.add_text("message", "file unchanged");
		return sendJson(res, 200, root, req.isKeepAlive());
	}
	if (upload_regular_file_exists(upload_dir, target_path)) {
		json_error(res, 409, "target file already exists", req.isKeepAlive());
		return true;
	}

	const std::string from_full = join_upload_path(upload_dir, file_path);
	const std::string to_full = join_upload_path(upload_dir, target_path);
	if (::rename(from_full.c_str(), to_full.c_str()) != 0) {
		json_error(res, 500, "move file failed", req.isKeepAlive());
		return true;
	}

	std::string rename_err;
	if (!tag_rename_file(upload_dir, file_path, target_path, rename_err)
		|| !video_resume_rename_file(upload_dir, file_path, target_path, rename_err)
		|| !file_lock_rename_key(upload_dir, remote_file_lock_key(file_path),
			remote_file_lock_key(target_path), rename_err))
	{
		(void) ::rename(to_full.c_str(), from_full.c_str());
		json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("file", file_path.c_str());
	root.add_text("path", target_path.c_str());
	root.add_text("folder_path", target_folder.c_str());
	root.add_text("message", "file moved");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool CopyFileAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string file_path;
	std::string err;
	const bool overwrite = req.getParameter("overwrite") != NULL
		&& strcmp(req.getParameter("overwrite"), "1") == 0;
	if (!normalize_relative_path(req.getParameter("file"), file_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(file_path)) {
		json_error(res, 409, "recycle file is protected", req.isKeepAlive());
		return true;
	}
	if (!upload_regular_file_exists(upload_dir, file_path)) {
		json_error(res, 404, "source file not found", req.isKeepAlive());
		return true;
	}

	std::string target_folder;
	if (!normalize_relative_path(req.getParameter("folder"), target_folder, err, true)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(target_folder)) {
		json_error(res, 409, "cannot copy file into recycle folder", req.isKeepAlive());
		return true;
	}
	if (!target_folder.empty() && !upload_directory_exists(upload_dir, target_folder)) {
		json_error(res, 404, "target folder not found", req.isKeepAlive());
		return true;
	}

	bool source_lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, parent_relative_path(file_path),
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		source_lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!source_lock_allowed) {
		json_error(res, 403, "source folder is locked", req.isKeepAlive());
		return true;
	}

	bool file_lock_allowed = false;
	if (!file_lock_path_allows(upload_dir, remote_file_lock_key(file_path),
		req.getParameter("file_password") ? req.getParameter("file_password") : "",
		file_lock_allowed, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!file_lock_allowed) {
		json_error(res, 403, "file is locked", req.isKeepAlive());
		return true;
	}

	bool target_lock_allowed = false;
	if (!folder_lock_path_allows(upload_dir, target_folder,
		req.getParameter("target_folder_password") ? req.getParameter("target_folder_password") : "",
		target_lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!target_lock_allowed) {
		json_error(res, 403, "target folder is locked", req.isKeepAlive());
		return true;
	}

	const std::string target_path = target_folder.empty()
		? base_name_from_relative_path(file_path)
		: (target_folder + "/" + base_name_from_relative_path(file_path));
	if (target_path == file_path) {
		json_error(res, 409, "source and destination are the same", req.isKeepAlive());
		return true;
	}

	const std::string from_full = join_upload_path(upload_dir, file_path);
	const std::string to_full = join_upload_path(upload_dir, target_path);
	struct stat target_st;
	if (lstat(to_full.c_str(), &target_st) == 0) {
		if (!overwrite) {
			json_error(res, 409, "target already contains a path with same name",
				req.isKeepAlive());
			return true;
		}
		if (S_ISDIR(target_st.st_mode)) {
			bool dest_lock_allowed = false;
			if (!folder_lock_path_allows(upload_dir, target_path,
				req.getParameter("target_folder_password") ? req.getParameter("target_folder_password") : "",
				dest_lock_allowed, locked_path, err))
			{
				json_error(res, 500, err.c_str(), req.isKeepAlive());
				return true;
			}
			if (!dest_lock_allowed) {
				json_error(res, 403, "destination folder is locked", req.isKeepAlive());
				return true;
			}
		} else {
			bool dest_file_allowed = false;
			if (!file_lock_path_allows(upload_dir, remote_file_lock_key(target_path),
				req.getParameter("file_password") ? req.getParameter("file_password") : "",
				dest_file_allowed, err))
			{
				json_error(res, 500, err.c_str(), req.isKeepAlive());
				return true;
			}
			if (!dest_file_allowed) {
				json_error(res, 403, "destination file is locked", req.isKeepAlive());
				return true;
			}
		}
		if (!remove_upload_path_recursive(to_full, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	} else if (errno != ENOENT) {
		json_error(res, 500, strerror(errno), req.isKeepAlive());
		return true;
	}

	struct stat source_st;
	if (stat(from_full.c_str(), &source_st) != 0 || !S_ISREG(source_st.st_mode)) {
		json_error(res, 404, "source file not found", req.isKeepAlive());
		return true;
	}
	if (req.getParameter("async") != NULL && strcmp(req.getParameter("async"), "1") == 0) {
		const std::string task_id = start_remote_copy_task(from_full, to_full,
			target_path, false);
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("task_id", task_id.c_str());
		root.add_text("path", target_path.c_str());
		root.add_text("folder_path", target_folder.c_str());
		root.add_bool("directory", false);
		root.add_text("message", "copy task started");
		return sendJson(res, 200, root, req.isKeepAlive());
	}
	if (!copy_regular_file_plain(from_full, to_full, source_st.st_mode, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("file", file_path.c_str());
	root.add_text("path", target_path.c_str());
	root.add_text("folder_path", target_folder.c_str());
	root.add_bool("overwritten", overwrite);
	root.add_text("message", "file copied");
	return sendJson(res, 200, root, req.isKeepAlive());
}

static bool send_remote_copy_task_snapshot(response_t& res,
	const remote_copy_task_snapshot_t& task, bool keep_alive)
{
	const double progress = task.total_bytes > 0
		? (double) task.copied_bytes * 100.0 / (double) task.total_bytes
		: (task.state == "done" ? 100.0 : 0.0);
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", task.id.c_str());
	root.add_text("state", task.state.c_str());
	root.add_text("message", task.message.c_str());
	root.add_text("error", task.error.c_str());
	root.add_text("path", task.path.c_str());
	root.add_number("total_bytes", task.total_bytes);
	root.add_number("copied_bytes", task.copied_bytes);
	root.add_number("progress", (long long) progress);
	root.add_bool("directory", task.directory);
	root.add_bool("cancel_requested", task.cancel_requested);
	return sendJson(res, 200, root, keep_alive);
}

bool RemoteCopyProgressAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	(void) upload_dir;
	const char* task_id = req.getParameter("task_id");
	if (task_id == NULL || *task_id == '\0') {
		json_error(res, 400, "task_id is required", req.isKeepAlive());
		return true;
	}
	remote_copy_task_snapshot_t task;
	if (!remote_copy_task_snapshot(task_id, task)) {
		json_error(res, 404, "copy task not found", req.isKeepAlive());
		return true;
	}
	return send_remote_copy_task_snapshot(res, task, req.isKeepAlive());
}

bool RemoteCopyCancelAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	(void) upload_dir;
	const char* task_id = req.getParameter("task_id");
	if (task_id == NULL || *task_id == '\0') {
		json_error(res, 400, "task_id is required", req.isKeepAlive());
		return true;
	}
	remote_copy_task_snapshot_t task;
	if (!remote_copy_task_cancel(task_id, task)) {
		json_error(res, 404, "copy task not found", req.isKeepAlive());
		return true;
	}
	return send_remote_copy_task_snapshot(res, task, req.isKeepAlive());
}

bool RenameFileAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string file_path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("file"), file_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(file_path)) {
		json_error(res, 409, "recycle file is protected", req.isKeepAlive());
		return true;
	}
	if (!upload_regular_file_exists(upload_dir, file_path)) {
		json_error(res, 404, "source file not found", req.isKeepAlive());
		return true;
	}

	std::string new_name = req.getParameter("name") ? req.getParameter("name") : "";
	std::string normalized_name;
	if (!normalize_relative_path(new_name.c_str(), normalized_name, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (normalized_name != base_name_from_relative_path(normalized_name)) {
		json_error(res, 400, "file name must not contain path separator", req.isKeepAlive());
		return true;
	}
	if (is_recycle_root_path(normalized_name)) {
		json_error(res, 409, "recycle folder name is protected", req.isKeepAlive());
		return true;
	}

	bool source_lock_allowed = false;
	std::string locked_path;
	const std::string parent_path = parent_relative_path(file_path);
	if (!folder_lock_path_allows(upload_dir, parent_path,
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		source_lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!source_lock_allowed) {
		json_error(res, 403, "source folder is locked", req.isKeepAlive());
		return true;
	}

	bool file_lock_allowed = false;
	if (!file_lock_path_allows(upload_dir, remote_file_lock_key(file_path),
		req.getParameter("file_password") ? req.getParameter("file_password") : "",
		file_lock_allowed, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!file_lock_allowed) {
		json_error(res, 403, "file is locked", req.isKeepAlive());
		return true;
	}

	const std::string target_path = parent_path.empty()
		? normalized_name
		: (parent_path + "/" + normalized_name);
	if (target_path == file_path) {
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("file", file_path.c_str());
		root.add_text("path", target_path.c_str());
		root.add_text("message", "file unchanged");
		return sendJson(res, 200, root, req.isKeepAlive());
	}
	if (upload_regular_file_exists(upload_dir, target_path)
		|| upload_directory_exists(upload_dir, target_path))
	{
		json_error(res, 409, "target path already exists", req.isKeepAlive());
		return true;
	}

	const std::string from_full = join_upload_path(upload_dir, file_path);
	const std::string to_full = join_upload_path(upload_dir, target_path);
	if (::rename(from_full.c_str(), to_full.c_str()) != 0) {
		json_error(res, 500, "rename file failed", req.isKeepAlive());
		return true;
	}

	std::string rename_err;
	if (!tag_rename_file(upload_dir, file_path, target_path, rename_err)
		|| !video_resume_rename_file(upload_dir, file_path, target_path, rename_err)
		|| !file_lock_rename_key(upload_dir, remote_file_lock_key(file_path),
			remote_file_lock_key(target_path), rename_err))
	{
		(void) ::rename(to_full.c_str(), from_full.c_str());
		json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("file", file_path.c_str());
	root.add_text("path", target_path.c_str());
	root.add_text("folder_path", parent_path.c_str());
	root.add_text("name", normalized_name.c_str());
	root.add_text("message", "file renamed");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool DownloadAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string file_path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("file"), file_path, err, false)) {
		return sendText(res, 400, "invalid file name\n", req.isKeepAlive());
	}
	if (!resolve_upload_regular_file_path(upload_dir, file_path, file_path)) {
		return sendText(res, 404, "file not found\n", req.isKeepAlive());
	}
	bool lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, parent_relative_path(file_path),
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		lock_allowed, locked_path, err))
	{
		return sendText(res, 500, err.c_str(), req.isKeepAlive());
	}
	if (!lock_allowed) {
		return sendText(res, 403, "folder is locked\n", req.isKeepAlive());
	}
	bool file_lock_allowed = false;
	if (!file_lock_path_allows(upload_dir, remote_file_lock_key(file_path),
		req.getParameter("file_password") ? req.getParameter("file_password") : "",
		file_lock_allowed, err))
	{
		return sendText(res, 500, err.c_str(), req.isKeepAlive());
	}
	if (!file_lock_allowed) {
		return sendText(res, 403, "file is locked\n", req.isKeepAlive());
	}

	const std::string fullpath = join_upload_path(upload_dir, file_path);
	const std::string basename = base_name_from_relative_path(file_path);

	struct stat st;
	if (stat(fullpath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		return sendText(res, 404, "file not found\n", req.isKeepAlive());
	}

	const long long fsize = regular_file_size(fullpath);
	if (fsize < 0) {
		return sendText(res, 500, "cannot read file size\n", req.isKeepAlive());
	}
	if (fsize == 0) {
		return sendText(res, 409, "file is empty, please re-upload\n", req.isKeepAlive());
	}

	FILE* in = fopen(fullpath.c_str(), "rb");
	if (in == NULL) {
		return sendText(res, 403, "file cannot be read\n", req.isKeepAlive());
	}

	const bool is_image = is_image_file(basename.c_str());
	const bool is_video = is_video_file(basename.c_str());
	const bool is_audio = is_audio_file(basename.c_str());
	const bool is_text = is_text_file(basename.c_str());
	const bool is_pdf = is_pdf_file(basename.c_str());
	const char* preview = req.getParameter("preview");
	const bool inline_preview = (is_image || is_video || is_audio || is_text || is_pdf)
		&& preview != NULL && strcmp(preview, "1") == 0;
	const char* range = req.getHeader("Range");
	long long range_begin = 0;
	long long range_end = 0;
	const bool has_range = parse_range_header(range, fsize, range_begin, range_end);
	const bool want_range = range != NULL && *range != '\0';
	if (want_range && !has_range) {
		acl::string cr;
		cr.format("bytes */%lld", fsize);
		res.setStatus(416)
			.setKeepAlive(req.isKeepAlive())
			.setHeader("Content-Range", cr.c_str())
			.setHeader("Accept-Ranges", "bytes")
			.setContentType("text/plain; charset=utf-8");
		const char* msg = "invalid range\n";
		res.setContentLength((long long) strlen(msg));
		fclose(in);
		return res.write(msg, strlen(msg)) && res.write(NULL, 0);
	}

	acl::string dispo;
	if (inline_preview) {
		dispo.format("inline; filename=\"%s\"", basename.c_str());
	} else {
		dispo.format("attachment; filename=\"%s\"", basename.c_str());
	}

	const char* ctype = "application/octet-stream";
	if (is_image) {
		ctype = image_content_type(basename.c_str());
	} else if (is_video) {
		ctype = video_content_type(basename.c_str());
	} else if (is_audio) {
		ctype = audio_content_type(basename.c_str());
	} else if (is_text) {
		ctype = text_content_type(basename.c_str());
	} else if (is_pdf) {
		ctype = document_content_type(basename.c_str());
	}

	long long send_begin = has_range ? range_begin : 0;
	long long send_end = has_range ? range_end : (fsize - 1);
	long long send_size = send_end - send_begin + 1;
	if (send_size < 0) {
		fclose(in);
		return sendText(res, 500, "invalid send size\n", req.isKeepAlive());
	}
	if (send_begin > 0 && !seek_file64(in, send_begin)) {
		fclose(in);
		return sendText(res, 500, "seek file failed\n", req.isKeepAlive());
	}

	if (has_range) {
		acl::string content_range;
		content_range.format("bytes %lld-%lld/%lld", send_begin, send_end, fsize);
		logger_debug(DEBUG_FILE, 1, "content range: %s", content_range.c_str());

		res.setStatus(206)
			.setKeepAlive(req.isKeepAlive())
			.setContentType(ctype)
			.setHeader("Content-Disposition", dispo.c_str())
			.setHeader("Cache-Control", "no-store, no-cache, must-revalidate")
			.setHeader("Pragma", "no-cache")
			.setHeader("Expires", "0")
			.setHeader("Accept-Ranges", "bytes")
			.setHeader("Content-Range", content_range.c_str())
			.setContentLength(send_size);
	} else {
		res.setStatus(200)
			.setKeepAlive(req.isKeepAlive())
			.setContentType(ctype)
			.setHeader("Content-Disposition", dispo.c_str())
			.setHeader("Cache-Control", "no-store, no-cache, must-revalidate")
			.setHeader("Pragma", "no-cache")
			.setHeader("Expires", "0")
			.setHeader("Accept-Ranges", "bytes")
			.setContentLength(fsize);
	}

	char buf[8192];
	long long remain = send_size;
	while (remain > 0) {
		size_t want = sizeof(buf);
		if ((long long) want > remain) {
			want = (size_t) remain;
		}
		const size_t n = fread(buf, 1, want, in);
		if (n == 0) {
			break;
		}
		if (!res.write(buf, (size_t) n)) {
			fclose(in);
			return false;
		}
		remain -= (long long) n;
	}

	fclose(in);
	return res.write(NULL, 0);
}

bool init_recycle_bin_db(const std::string& upload_dir, std::string& err) {
	err.clear();

	std::lock_guard<std::mutex> guard(g_recycle_mutex);
	acl::string next_db_file;
	next_db_file.format("%s/.recycle_bin.db", upload_dir.c_str());
	if (g_recycle_db_ready && g_recycle_db_file == next_db_file.c_str()) {
		return true;
	}

	if (!make_dir_recursive(upload_dir.c_str())) {
		err = "cannot access upload dir";
		return false;
	}
	const std::string recycle_dir = join_upload_path(upload_dir, recycle_folder_name());
	if (!make_dir_recursive(recycle_dir.c_str())) {
		err = "cannot access recycle folder(0): ";
		err += recycle_dir;
		return false;
	}

	std::string sqlite_lib_path = choose_sqlite_lib_path();
	if (sqlite_lib_path.empty()) {
		err = "sqlite dynamic library not found";
		return false;
	}
	acl::db_handle::set_loadpath(sqlite_lib_path.c_str());

	g_recycle_db_file = next_db_file.c_str();

	if (!ensure_recycle_tables_locked(err)) {
		return false;
	}

	g_recycle_db_ready = true;
	return true;
}

} // namespace action

// ===== OpenFileAction (remote file open with local player) =====

namespace {

static void set_display_env_for_open(void)
{
#ifdef _WIN32
	return;
#else
	const char *display = getenv("DISPLAY");
	if (!display || display[0] == '\0') {
		struct stat st;
		if (stat("/tmp/.X11-unix/X0", &st) == 0) {
			setenv("DISPLAY", ":0", 1);
		} else if (stat("/tmp/.X11-unix", &st) == 0) {
			DIR *d = opendir("/tmp/.X11-unix");
			if (d) {
				struct dirent *de;
				while ((de = readdir(d)) != NULL) {
					if (de->d_name[0] == 'X') {
						char buf[16];
						snprintf(buf, sizeof(buf), ":%s",
							de->d_name + 1);
						setenv("DISPLAY", buf, 1);
						break;
					}
				}
				closedir(d);
			}
		}
	}
	const char *dbus = getenv("DBUS_SESSION_BUS_ADDRESS");
	if (!dbus || dbus[0] == '\0') {
		const char *uid_s = getenv("UID");
		uid_t uid = uid_s ? (uid_t)atoi(uid_s) : getuid();
		char addr[128];
		snprintf(addr, sizeof(addr),
			"unix:path=/run/user/%u/bus", (unsigned)uid);
		setenv("DBUS_SESSION_BUS_ADDRESS", addr, 1);
	}
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (!runtime || runtime[0] == '\0') {
		char rd[64];
		snprintf(rd, sizeof(rd), "/run/user/%u", (unsigned)getuid());
		setenv("XDG_RUNTIME_DIR", rd, 1);
	}
#endif
}

static bool run_open_file_command(const std::string& path,
	bool choose_app, std::string& err)
{
	err.clear();
#ifdef _WIN32
	(void) choose_app;
	return webcool_shell_open(path, err);
#else
	pid_t pid = fork();
	if (pid < 0) {
		err = strerror(errno);
		return false;
	}
	if (pid == 0) {
		set_display_env_for_open();
#ifdef __APPLE__
		if (choose_app) {
			execlp("osascript", "osascript",
				"-e", "on run argv",
				"-e", "set targetPath to item 1 of argv",
				"-e", "set chosenApp to choose application with prompt \"选择本地播放器\"",
				"-e", "set appName to name of chosenApp",
				"-e", "do shell script \"open -a \" & quoted form of appName & \" \" & quoted form of targetPath",
				"-e", "end run",
				path.c_str(), (char*) NULL);
		}
		execlp("open", "open", path.c_str(), (char*) NULL);
#else
		if (choose_app) {
			char cmd[4096];
			snprintf(cmd, sizeof(cmd),
				"zenity --file-selection "
				"--title='选择本地播放器' "
				"--filename=/usr/bin/ "
				"2>/dev/null");
			FILE *fp = popen(cmd, "r");
			if (!fp) {
				_exit(127);
			}
			char chosen[2048];
			if (!fgets(chosen, sizeof(chosen), fp)) {
				pclose(fp);
				_exit(1);
			}
			pclose(fp);
			size_t len = strlen(chosen);
			while (len > 0 && (chosen[len-1] == '\n'
				|| chosen[len-1] == '\r')) {
				chosen[--len] = '\0';
			}
			if (len == 0) {
				_exit(0);
			}
			execlp(chosen, chosen, path.c_str(), (char*) NULL);
			_exit(127);
		}
		execlp("xdg-open", "xdg-open", path.c_str(), (char*) NULL);
		execlp("gio", "gio", "open", path.c_str(), (char*) NULL);
#endif
		_exit(127);
	}

	int status = 0;
	if (waitpid(pid, &status, 0) < 0) {
		err = strerror(errno);
		return false;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		err = choose_app
			? "failed to choose local player"
			: "failed to open file with local player";
		return false;
	}
	return true;
#endif
}

static std::string get_tmp_open_dir()
{
	char buf[256];
	snprintf(buf, sizeof(buf), "/tmp/aicool-open-%u", (unsigned)getuid());
	return std::string(buf);
}

} // anonymous namespace

bool action::OpenFileAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	const bool choose_app = req.getParameter("chooser") != NULL
		&& strcmp(req.getParameter("chooser"), "1") == 0;
	std::string file_path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("file"), file_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	// Check folder lock
	bool lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, parent_relative_path(file_path),
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!lock_allowed) {
		json_error(res, 403, "folder is locked", req.isKeepAlive());
		return true;
	}
	// Check file lock
	bool file_lock_allowed = false;
	if (!file_lock_path_allows(upload_dir, remote_file_lock_key(file_path),
		req.getParameter("file_password") ? req.getParameter("file_password") : "",
		file_lock_allowed, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!file_lock_allowed) {
		json_error(res, 403, "file is locked", req.isKeepAlive());
		return true;
	}
	// Resolve the full path on disk
	const std::string fullpath = join_upload_path(upload_dir, file_path);
	struct stat st;
	if (stat(fullpath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		json_error(res, 404, "file not found", req.isKeepAlive());
		return true;
	}
	// Copy to a temp directory so the player can access it by name
	const std::string tmp_dir = get_tmp_open_dir();
	mkdir(tmp_dir.c_str(), 0755);
	const std::string basename = base_name_from_relative_path(file_path);
	const std::string tmp_path = tmp_dir + "/" + basename;
	// Remove old temp copy if exists
	unlink(tmp_path.c_str());
	// Copy file
	{
		FILE *src = fopen(fullpath.c_str(), "rb");
		if (!src) {
			json_error(res, 500, "cannot read source file", req.isKeepAlive());
			return true;
		}
		FILE *dst = fopen(tmp_path.c_str(), "wb");
		if (!dst) {
			fclose(src);
			json_error(res, 500, "cannot create temp file", req.isKeepAlive());
			return true;
		}
		char buf[65536];
		size_t n;
		while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
			if (fwrite(buf, 1, n, dst) != n) {
				fclose(src);
				fclose(dst);
				unlink(tmp_path.c_str());
				json_error(res, 500, "write error", req.isKeepAlive());
				return true;
			}
		}
		fclose(src);
		fclose(dst);
	}
	if (!run_open_file_command(tmp_path, choose_app, err)) {
		unlink(tmp_path.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("file", file_path.c_str());
	root.add_text("message", choose_app ? "local player chooser opened" : "file opened");
	return sendJson(res, 200, root, req.isKeepAlive());
}

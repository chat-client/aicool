#include "stdafx.h"
#include "actions.h"
#include "action_util.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include "../platform_compat.h"
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace action {

namespace {

struct TagRow {
	std::string id;
	std::string parent_id;
	std::string name;
	long long sort_order;
};

static bool open_tag_db_locked(acl::db_sqlite& db, std::string& err);
static long long next_sort_order_locked(acl::db_sqlite& db,
	const std::string& parent_id, std::string& err);

static std::mutex g_tag_mutex;
static std::string g_tag_db_file;
static bool g_tag_db_ready = false;
static unsigned long g_tag_id_seq = 0;

static const char* g_tag_table_create_sql =
	"CREATE TABLE IF NOT EXISTS tag_catalog ("
	"id TEXT PRIMARY KEY,"
	"parent_id TEXT NOT NULL DEFAULT '',"
	"tag_name TEXT NOT NULL,"
	"sort_order INTEGER NOT NULL DEFAULT 0,"
	"created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
	"updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
	")";

static const char* g_tag_table_index_sql =
	"CREATE INDEX IF NOT EXISTS idx_tag_catalog_parent"
	" ON tag_catalog(parent_id, sort_order, created_at)";

static const char* g_tag_file_rel_table_create_sql =
	"CREATE TABLE IF NOT EXISTS file_tag_rel ("
	"tag_id TEXT NOT NULL,"
	"file_name TEXT NOT NULL,"
	"updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
	"PRIMARY KEY(tag_id, file_name),"
	"FOREIGN KEY(tag_id) REFERENCES tag_catalog(id) ON DELETE CASCADE"
	")";

static const char* g_tag_file_rel_index_sql =
	"CREATE INDEX IF NOT EXISTS idx_file_tag_rel_tag"
	" ON file_tag_rel(tag_id, updated_at)";

const char* g_default_video_tag_id = "builtin_video";
const char* g_default_audio_tag_id = "builtin_audio";
const char* g_default_image_tag_id = "builtin_image";
const char* g_default_video_tag_name = "视频";
const char* g_default_audio_tag_name = "音频";
const char* g_default_image_tag_name = "图片";
const char* g_local_tag_file_prefix = "local:";
const char* g_tag_lock_prefix = "tag:";

bool is_local_tag_file_name(const std::string& file_name)
{
	return file_name.compare(0, strlen(g_local_tag_file_prefix),
		g_local_tag_file_prefix) == 0;
}

std::string local_tag_storage_name(const std::string& path)
{
	return std::string(g_local_tag_file_prefix) + path;
}

std::string local_tag_path_from_storage_name(const std::string& name)
{
	return is_local_tag_file_name(name)
		? name.substr(strlen(g_local_tag_file_prefix))
		: name;
}

std::string tag_lock_key(const std::string& tag_id)
{
	return std::string(g_tag_lock_prefix) + tag_id;
}

std::string tag_db_file_for_upload_dir(const std::string& upload_dir)
{
	acl::string path;
	path.format("%s/.tag_catalog.db", upload_dir.c_str());
	return std::string(path.c_str());
}

bool normalize_existing_local_file_path(const char* input,
	std::string& out, std::string& err)
{
	err.clear();
	if (input == NULL || *input == '\0') {
		err = "missing local file";
		return false;
	}
#ifdef _WIN32
	const size_t len = strlen(input);
	const bool absolute = (len >= 3
			&& ((input[0] >= 'A' && input[0] <= 'Z')
				|| (input[0] >= 'a' && input[0] <= 'z'))
			&& input[1] == ':'
			&& (input[2] == '/' || input[2] == '\\'))
		|| (len >= 2
			&& (input[0] == '/' || input[0] == '\\')
			&& (input[1] == '/' || input[1] == '\\'));
	if (!absolute) {
		err = "absolute path is required";
		return false;
	}
#else
	if (input[0] != '/') {
		err = "absolute path is required";
		return false;
	}
#endif
	char resolved[PATH_MAX];
	if (realpath(input, resolved) == NULL) {
		err = strerror(errno);
		return false;
	}
	struct stat st;
	if (stat(resolved, &st) != 0 || !S_ISREG(st.st_mode)) {
		err = "file not found";
		return false;
	}
	out = resolved;
	return true;
}

std::string local_parent_path(const std::string& path)
{
	std::string::size_type pos = path.find_last_of("/\\");
	if (pos == std::string::npos || pos == 0) {
		return "/";
	}
	return path.substr(0, pos);
}

#if 0
static void json_error(response_t& res, int status, const char* msg,
	bool keep_alive)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", msg ? msg : "unknown error");
	sendJson(res, status, root, keep_alive);
}
#endif

std::string trim_copy(const char* text) {
	const char* s = text ? text : "";
	while (*s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) {
		++s;
	}

	const char* e = s + strlen(s);
	while (e > s && (*(e - 1) == ' ' || *(e - 1) == '\t'
		|| *(e - 1) == '\r' || *(e - 1) == '\n'))
	{
		--e;
	}

	return std::string(s, (size_t) (e - s));
}

void format_upload_time(time_t ts, char* buf, size_t size) {
	if (buf == NULL || size == 0) {
		return;
	}

	if (ts <= 0) {
		buf[0] = '\0';
		return;
	}

	tm tmv;
	acl_localtime_r(&ts, &tmv);
	if (strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tmv) == 0) {
		buf[0] = '\0';
	}
}

bool validate_tag_name(const std::string& name, std::string& err) {
	err.clear();
	if (name.empty()) {
		err = "tag name is empty";
		return false;
	}
	if (name.size() > 60) {
		err = "tag name is too long";
		return false;
	}
	for (size_t i = 0; i < name.size(); ++i) {
		unsigned char c = (unsigned char) name[i];
		if (c < 32 || c == 127) {
			err = "tag name contains control character";
			return false;
		}
	}
	return true;
}

bool validate_tag_id(const std::string& tag_id, std::string& err) {
	err.clear();
	if (tag_id.empty()) {
		err = "tag id is empty";
		return false;
	}
	if (tag_id.size() > 120) {
		err = "tag id is too long";
		return false;
	}
	for (size_t i = 0; i < tag_id.size(); ++i) {
		unsigned char c = (unsigned char) tag_id[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '_' || c == '-')
		{
			continue;
		}
		err = "tag id contains invalid character";
		return false;
	}
	return true;
}

bool ends_with_ignore_case(const std::string& text, const char* suffix) {
	if (suffix == NULL) {
		return false;
	}
	size_t suffix_len = strlen(suffix);
	if (text.size() < suffix_len) {
		return false;
	}
	const size_t start = text.size() - suffix_len;
	for (size_t i = 0; i < suffix_len; ++i) {
		unsigned char a = (unsigned char) text[start + i];
		unsigned char b = (unsigned char) suffix[i];
		if (a >= 'A' && a <= 'Z') {
			a = (unsigned char) (a - 'A' + 'a');
		}
		if (b >= 'A' && b <= 'Z') {
			b = (unsigned char) (b - 'A' + 'a');
		}
		if (a != b) {
			return false;
		}
	}
	return true;
}

bool is_video_file_name(const std::string& name) {
	static const char* kVideoSuffixes[] = {
		".mp4", ".avi", ".mkv", ".rm", ".rmvb", ".mov", ".wmv",
		".mpg", ".mpeg"
	};
	for (size_t i = 0; i < sizeof(kVideoSuffixes) / sizeof(kVideoSuffixes[0]); ++i) {
		if (ends_with_ignore_case(name, kVideoSuffixes[i])) {
			return true;
		}
	}
	return false;
}

bool is_audio_file_name(const std::string& name) {
	static const char* kAudioSuffixes[] = {
		".mp3", ".m4a", ".aac", ".wav", ".ogg", ".flac"
	};
	for (size_t i = 0; i < sizeof(kAudioSuffixes) / sizeof(kAudioSuffixes[0]); ++i) {
		if (ends_with_ignore_case(name, kAudioSuffixes[i])) {
			return true;
		}
	}
	return false;
}

bool is_image_file_name(const std::string& name) {
	static const char* kImageSuffixes[] = {
		".png", ".jpg", ".jpeg", ".gif", ".heic", ".heif"
	};
	for (size_t i = 0; i < sizeof(kImageSuffixes) / sizeof(kImageSuffixes[0]); ++i) {
		if (ends_with_ignore_case(name, kImageSuffixes[i])) {
			return true;
		}
	}
	return false;
}

bool ensure_tag_dir(const std::string& upload_dir, std::string& err) {
	err.clear();
	if (!make_dir(upload_dir.c_str())) {
		err = "cannot access upload dir";
		return false;
	}
	return true;
}

bool ensure_tag_tables_locked(std::string& err) {
	err.clear();
	if (g_tag_db_file.empty()) {
		err = "tag database file is empty";
		return false;
	}

	acl::db_sqlite db(g_tag_db_file.c_str(), "utf-8");
	if (!db.open()) {
		err = db.get_error();
		return false;
	}
	db.set_busy_timeout(3000);

	acl::query qfk;
	qfk.create("PRAGMA foreign_keys=ON");
	if (!db.exec_update(qfk)) {
		err = db.get_error();
		return false;
	}

	acl::query q1;
	q1.create(g_tag_table_create_sql);
	if (!db.exec_update(q1)) {
		err = db.get_error();
		return false;
	}

	acl::query q2;
	q2.create(g_tag_table_index_sql);
	if (!db.exec_update(q2)) {
		err = db.get_error();
		return false;
	}

	acl::query q3;
	q3.create(g_tag_file_rel_table_create_sql);
	if (!db.exec_update(q3)) {
		err = db.get_error();
		return false;
	}

	acl::query q4;
	q4.create(g_tag_file_rel_index_sql);
	if (!db.exec_update(q4)) {
		err = db.get_error();
		return false;
	}

	return true;
}

bool ensure_default_root_tags_locked(acl::db_sqlite& db, std::string& err) {
	err.clear();
	acl::query query;
	query.create("SELECT id, tag_name FROM tag_catalog WHERE parent_id='' ");
	if (!db.exec_select(query)) {
		err = db.get_error();
		return false;
	}

	bool has_video = false;
	bool has_audio = false;
	bool has_image = false;
	for (size_t i = 0; i < db.length(); ++i) {
		const acl::db_row* row = db[i];
		if (row == NULL) {
			continue;
		}
		const char* id = (*row)["id"];
		const char* tag_name = (*row)["tag_name"];
		const std::string id_text = id ? id : "";
		const std::string name_text = tag_name ? tag_name : "";
		if (id_text == g_default_video_tag_id || name_text == g_default_video_tag_name) {
			has_video = true;
		}
		if (id_text == g_default_audio_tag_id || name_text == g_default_audio_tag_name) {
			has_audio = true;
		}
		if (id_text == g_default_image_tag_id || name_text == g_default_image_tag_name) {
			has_image = true;
		}
	}
	db.free_result();

	struct DefaultTagSpec {
		const char* id;
		const char* name;
		bool present;
	};
	DefaultTagSpec specs[] = {
		{ g_default_video_tag_id, g_default_video_tag_name, has_video },
		{ g_default_audio_tag_id, g_default_audio_tag_name, has_audio },
		{ g_default_image_tag_id, g_default_image_tag_name, has_image }
	};

	for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); ++i) {
		if (specs[i].present) {
			continue;
		}
		long long sort_order = next_sort_order_locked(db, std::string(), err);
		if (sort_order <= 0) {
			if (err.empty()) {
				err = "failed to allocate default tag sort order";
			}
			return false;
		}
		acl::query insert;
		insert.create("INSERT INTO tag_catalog(id, parent_id, tag_name, sort_order, updated_at)"
			" VALUES(:id, '', :tag_name, :sort_order, strftime('%s','now'))")
			.set_parameter("id", specs[i].id)
			.set_parameter("tag_name", specs[i].name)
			.set_parameter("sort_order", sort_order);
		if (!db.exec_update(insert)) {
			err = db.get_error();
			return false;
		}
	}

	return true;
}

bool ensure_tag_db_for_request(const std::string& upload_dir,
	std::string& err)
{
	err.clear();
	acl::string expected_db_file;
	expected_db_file.format("%s/.tag_catalog.db", upload_dir.c_str());
	if (!g_tag_db_ready || g_tag_db_file != expected_db_file.c_str()) {
		if (!init_tag_db(upload_dir, err)) {
			return false;
		}
	}

	std::lock_guard<std::mutex> guard(g_tag_mutex);
	if (!ensure_tag_dir(upload_dir, err)) {
		g_tag_db_ready = false;
		return false;
	}
	if (!ensure_tag_tables_locked(err)) {
		g_tag_db_ready = false;
		return false;
	}

	acl::db_sqlite db(tag_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
	if (!open_tag_db_locked(db, err)) {
		g_tag_db_ready = false;
		return false;
	}
	if (!ensure_default_root_tags_locked(db, err)) {
		g_tag_db_ready = false;
		return false;
	}

	g_tag_db_ready = true;
	return true;
}

bool open_tag_db_locked(acl::db_sqlite& db, std::string& err) {
	err.clear();
	if (!db.open()) {
		err = db.get_error();
		return false;
	}
	db.set_busy_timeout(3000);

	acl::query qfk;
	qfk.create("PRAGMA foreign_keys=ON");
	if (!db.exec_update(qfk)) {
		err = db.get_error();
		return false;
	}
	return true;
}

bool fetch_tag_locked(acl::db_sqlite& db, const std::string& tag_id,
	TagRow* out, std::string& err)
{
	err.clear();
	acl::query query;
	query.create("SELECT id, parent_id, tag_name, sort_order"
		" FROM tag_catalog WHERE id=:id")
		.set_parameter("id", tag_id.c_str());
	if (!db.exec_select(query)) {
		err = db.get_error();
		return false;
	}
	if (db.empty()) {
		db.free_result();
		return false;
	}

	const acl::db_row* row = db.get_first_row();
	if (row != NULL && out != NULL) {
		const char* id = (*row)["id"];
		const char* parent_id = (*row)["parent_id"];
		const char* tag_name = (*row)["tag_name"];
		const char* sort_order = (*row)["sort_order"];
		out->id = id ? id : "";
		out->parent_id = parent_id ? parent_id : "";
		out->name = tag_name ? tag_name : "";
		out->sort_order = sort_order ? atoll(sort_order) : 0;
	}
	db.free_result();
	return true;
}

int get_tag_level_locked(acl::db_sqlite& db, const std::string& tag_id,
	std::string& err)
{
	int level = 0;
	std::string current = tag_id;
	while (!current.empty()) {
		TagRow row;
		if (!fetch_tag_locked(db, current, &row, err)) {
			if (err.empty()) {
				err = "tag not found";
			}
			return -1;
		}
		level++;
		current = row.parent_id;
		if (level > 32) {
			err = "tag level overflow";
			return -1;
		}
	}
	return level;
}

long long next_sort_order_locked(acl::db_sqlite& db,
	const std::string& parent_id, std::string& err)
{
	err.clear();
	acl::query query;
	if (parent_id.empty()) {
		query.create("SELECT COALESCE(MAX(sort_order), 0) + 1 AS next_sort"
			" FROM tag_catalog WHERE parent_id='' ");
	} else {
		query.create("SELECT COALESCE(MAX(sort_order), 0) + 1 AS next_sort"
			" FROM tag_catalog WHERE parent_id=:parent_id")
			.set_parameter("parent_id", parent_id.c_str());
	}
	if (!db.exec_select(query)) {
		err = db.get_error();
		return -1;
	}

	long long next_sort = 1;
	const acl::db_row* row = db.get_first_row();
	if (row != NULL) {
		const char* text = (*row)["next_sort"];
		if (text != NULL && *text != '\0') {
			next_sort = atoll(text);
			if (next_sort <= 0) {
				next_sort = 1;
			}
		}
	}
	db.free_result();
	return next_sort;
}

std::string make_tag_id_locked() {
	++g_tag_id_seq;
	acl::string buf;
	buf.format("tag_%lld_%lu", (long long) time(NULL), g_tag_id_seq);
	return std::string(buf.c_str());
}

bool file_exists_in_upload_dir(const std::string& upload_dir,
	const char* relative_path)
{
	std::string normalized;
	std::string err;
	if (!normalize_relative_path(relative_path, normalized, err, false)) {
		return false;
	}
	return upload_regular_file_exists(upload_dir, normalized);
}

void append_tag_json(const std::string& upload_dir,
	acl::json& json, acl::json_node& arr,
	const TagRow& row,
	const std::map<std::string, std::vector<std::string> >& children_by_parent,
	const std::map<std::string, TagRow>& rows_by_id,
	const std::map<std::string, long long>& direct_file_counts)
{
	acl::json_node& item = arr.add_child(false, true);
	item.add_text("id", row.id.c_str());
	item.add_text("name", row.name.c_str());
	item.add_text("parent_id", row.parent_id.c_str());
	std::map<std::string, long long>::const_iterator count_it =
		direct_file_counts.find(row.id);
	item.add_number("file_count",
		count_it == direct_file_counts.end() ? 0 : count_it->second);
	bool locked = false;
	std::string lock_err;
	if (file_lock_path_has_lock(upload_dir, tag_lock_key(row.id), locked, lock_err))
	{
		item.add_bool("locked", locked);
	}
	acl::json_node& files = json.create_array();
	item.add_child("files", files);
	acl::json_node& children = json.create_array();
	item.add_child("children", children);

	std::map<std::string, std::vector<std::string> >::const_iterator it =
		children_by_parent.find(row.id);
	if (it == children_by_parent.end()) {
		return;
	}

	for (size_t i = 0; i < it->second.size(); ++i) {
		std::map<std::string, TagRow>::const_iterator rit =
			rows_by_id.find(it->second[i]);
		if (rit == rows_by_id.end()) {
			continue;
		}
		append_tag_json(upload_dir, json, children, rit->second,
			children_by_parent, rows_by_id, direct_file_counts);
	}
}

bool collect_subtree_ids_locked(acl::db_sqlite& db,
	const std::string& root_id, std::vector<std::string>& ids, std::string& err)
{
	err.clear();
	ids.clear();
	ids.push_back(root_id);
	for (size_t index = 0; index < ids.size(); ++index) {
		acl::query query;
		query.create("SELECT id FROM tag_catalog WHERE parent_id=:parent_id"
			" ORDER BY sort_order ASC, created_at ASC")
			.set_parameter("parent_id", ids[index].c_str());
		if (!db.exec_select(query)) {
			err = db.get_error();
			return false;
		}
		for (size_t i = 0; i < db.length(); ++i) {
			const acl::db_row* row = db[i];
			if (row == NULL) {
				continue;
			}
			const char* child_id = (*row)["id"];
			if (child_id != NULL && *child_id != '\0') {
				ids.push_back(child_id);
			}
		}
		db.free_result();
	}
	return true;
}

bool get_root_tag_locked(acl::db_sqlite& db, const std::string& tag_id,
	TagRow* out, std::string& err)
{
	err.clear();
	if (tag_id.empty()) {
		err = "tag id is empty";
		return false;
	}

	std::set<std::string> visited;
	std::string current = tag_id;
	while (!current.empty()) {
		if (!visited.insert(current).second) {
			err = "tag parent cycle detected";
			return false;
		}
		TagRow row;
		if (!fetch_tag_locked(db, current, &row, err)) {
			if (err.empty()) {
				err = "tag not found";
			}
			return false;
		}
		if (row.parent_id.empty()) {
			if (out != NULL) {
				*out = row;
			}
			return true;
		}
		current = row.parent_id;
	}

	err = "root tag not found";
	return false;
}

int root_tag_priority(const TagRow& row) {
	if (row.id == g_default_video_tag_id || row.name == g_default_video_tag_name) {
		return 0;
	}
	if (row.id == g_default_audio_tag_id || row.name == g_default_audio_tag_name) {
		return 1;
	}
	if (row.id == g_default_image_tag_id || row.name == g_default_image_tag_name) {
		return 2;
	}
	return 3;
}

bool is_protected_root_tag(const TagRow& row) {
	if (!row.parent_id.empty()) {
		return false;
	}
	return row.id == g_default_video_tag_id
		|| row.id == g_default_audio_tag_id
		|| row.id == g_default_image_tag_id
		|| row.name == g_default_video_tag_name
		|| row.name == g_default_audio_tag_name
		|| row.name == g_default_image_tag_name;
}

} // namespace

bool init_tag_db(const std::string& upload_dir, std::string& err) {
	err.clear();
	std::lock_guard<std::mutex> guard(g_tag_mutex);
	acl::string next_db_file;
	next_db_file.format("%s/.tag_catalog.db", upload_dir.c_str());
	if (g_tag_db_ready && g_tag_db_file == next_db_file.c_str()) {
		return true;
	}

	if (!ensure_tag_dir(upload_dir, err)) {
		return false;
	}

	g_tag_db_file = next_db_file.c_str();

	if (!ensure_tag_tables_locked(err)) {
		return false;
	}

	g_tag_db_ready = true;
	return true;
}

bool tag_unbind_file(const std::string& upload_dir,
	const std::string& file_name, std::string& err)
{
	err.clear();
	if (file_name.empty()) {
		return true;
	}
	if (!ensure_tag_db_for_request(upload_dir, err)) {
		return false;
	}

	std::lock_guard<std::mutex> guard(g_tag_mutex);
	acl::db_sqlite db(tag_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
	if (!open_tag_db_locked(db, err)) {
		return false;
	}

	acl::query query;
	query.create("DELETE FROM file_tag_rel WHERE file_name=:file")
		.set_parameter("file", file_name.c_str());
	if (!db.exec_update(query)) {
		err = db.get_error();
		return false;
	}
	return true;
}

bool tag_rename_file(const std::string& upload_dir,
	const std::string& old_file_name, const std::string& new_file_name,
	std::string& err)
{
	err.clear();
	if (old_file_name.empty() || new_file_name.empty() || old_file_name == new_file_name) {
		return true;
	}
	if (!ensure_tag_db_for_request(upload_dir, err)) {
		return false;
	}

	std::lock_guard<std::mutex> guard(g_tag_mutex);
	acl::db_sqlite db(tag_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
	if (!open_tag_db_locked(db, err)) {
		return false;
	}

	acl::query query;
	query.create("UPDATE file_tag_rel SET file_name=:new_file, updated_at=strftime('%s','now')"
		" WHERE file_name=:old_file")
		.set_parameter("new_file", new_file_name.c_str())
		.set_parameter("old_file", old_file_name.c_str());
	if (!db.exec_update(query)) {
		err = db.get_error();
		return false;
	}
	return true;
}

bool tag_rename_folder_prefix(const std::string& upload_dir,
	const std::string& old_prefix, const std::string& new_prefix,
	std::string& err)
{
	err.clear();
	if (old_prefix.empty() || new_prefix.empty() || old_prefix == new_prefix) {
		return true;
	}
	if (!ensure_tag_db_for_request(upload_dir, err)) {
		return false;
	}

	std::lock_guard<std::mutex> guard(g_tag_mutex);
	acl::db_sqlite db(tag_db_file_for_upload_dir(upload_dir).c_str(), "utf-8");
	if (!db.open()) {
		err = db.get_error();
		return false;
	}
	db.set_busy_timeout(3000);

	const std::string old_like = old_prefix + "/%";
	acl::query query;
	query.create("UPDATE file_tag_rel "
		"SET file_name=:new_prefix || substr(file_name, length(:old_prefix) + 1), "
		"updated_at=strftime('%s','now') "
		"WHERE file_name LIKE :old_like")
		.set_parameter("new_prefix", new_prefix.c_str())
		.set_parameter("old_prefix", old_prefix.c_str())
		.set_parameter("old_like", old_like.c_str());
	if (!db.exec_update(query)) {
		err = db.get_error();
		return false;
	}
	return true;
}

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
		std::lock_guard<std::mutex> guard(g_tag_mutex);
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
	std::lock_guard<std::mutex> guard(g_tag_mutex);
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
		item.add_text("folder_path", (is_local_file ? local_parent_path(file_path) : parent_relative_path(file_path)).c_str());
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

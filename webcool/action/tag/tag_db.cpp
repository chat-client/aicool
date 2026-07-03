#include "stdafx.h"
#include "tag_common.h"

namespace action {

std::mutex g_tag_mutex;
std::string g_tag_db_file;
bool g_tag_db_ready = false;
unsigned long g_tag_id_seq = 0;

const char* g_tag_table_create_sql =
	"CREATE TABLE IF NOT EXISTS tag_catalog ("
	"id TEXT PRIMARY KEY,"
	"parent_id TEXT NOT NULL DEFAULT '',"
	"tag_name TEXT NOT NULL,"
	"sort_order INTEGER NOT NULL DEFAULT 0,"
	"created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
	"updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
	")";

const char* g_tag_table_index_sql =
	"CREATE INDEX IF NOT EXISTS idx_tag_catalog_parent"
	" ON tag_catalog(parent_id, sort_order, created_at)";

const char* g_tag_file_rel_table_create_sql =
	"CREATE TABLE IF NOT EXISTS file_tag_rel ("
	"tag_id TEXT NOT NULL,"
	"file_name TEXT NOT NULL,"
	"updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
	"PRIMARY KEY(tag_id, file_name),"
	"FOREIGN KEY(tag_id) REFERENCES tag_catalog(id) ON DELETE CASCADE"
	")";

const char* g_tag_file_rel_index_sql =
	"CREATE INDEX IF NOT EXISTS idx_file_tag_rel_tag"
	" ON file_tag_rel(tag_id, updated_at)";

const char* g_default_video_tag_id = "builtin_video";
const char* g_default_audio_tag_id = "builtin_audio";
const char* g_default_image_tag_id = "builtin_image";
const char* g_default_document_tag_id = "builtin_document";
const char* g_default_video_tag_name = "视频";
const char* g_default_audio_tag_name = "音频";
const char* g_default_image_tag_name = "图片";
const char* g_default_document_tag_name = "文档";
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
	bool has_document = false;
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
		if (id_text == g_default_document_tag_id || name_text == g_default_document_tag_name) {
			has_document = true;
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
		{ g_default_image_tag_id, g_default_image_tag_name, has_image },
		{ g_default_document_tag_id, g_default_document_tag_name, has_document }
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
	if (row.id == g_default_document_tag_id || row.name == g_default_document_tag_name) {
		return 3;
	}
	return 4;
}

bool is_protected_root_tag(const TagRow& row) {
	if (!row.parent_id.empty()) {
		return false;
	}
	return row.id == g_default_video_tag_id
		|| row.id == g_default_audio_tag_id
		|| row.id == g_default_image_tag_id
		|| row.id == g_default_document_tag_id
		|| row.name == g_default_video_tag_name
		|| row.name == g_default_audio_tag_name
		|| row.name == g_default_image_tag_name
		|| row.name == g_default_document_tag_name;
}
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

} // namespace action

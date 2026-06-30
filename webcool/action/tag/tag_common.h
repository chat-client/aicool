#pragma once

#include "../file/file_common.h"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace action {

struct TagRow {
	std::string id;
	std::string parent_id;
	std::string name;
	long long sort_order;
};

extern std::mutex g_tag_mutex;
extern std::string g_tag_db_file;
extern bool g_tag_db_ready;
extern unsigned long g_tag_id_seq;

extern const char* g_tag_table_create_sql;
extern const char* g_tag_table_index_sql;
extern const char* g_tag_file_rel_table_create_sql;
extern const char* g_tag_file_rel_index_sql;

extern const char* g_default_video_tag_id;
extern const char* g_default_audio_tag_id;
extern const char* g_default_image_tag_id;
extern const char* g_default_video_tag_name;
extern const char* g_default_audio_tag_name;
extern const char* g_default_image_tag_name;

bool is_local_tag_file_name(const std::string& file_name);
std::string local_tag_storage_name(const std::string& path);
std::string local_tag_path_from_storage_name(const std::string& name);
std::string tag_lock_key(const std::string& tag_id);
std::string tag_db_file_for_upload_dir(const std::string& upload_dir);
bool normalize_existing_local_file_path(const char* input, std::string& out,
	std::string& err);
std::string tag_local_parent_path(const std::string& path);
std::string trim_copy(const char* text);
bool validate_tag_name(const std::string& name, std::string& err);
bool validate_tag_id(const std::string& tag_id, std::string& err);
bool ends_with_ignore_case(const std::string& text, const char* suffix);
bool is_video_file_name(const std::string& name);
bool is_audio_file_name(const std::string& name);
bool is_image_file_name(const std::string& name);
bool file_exists_in_upload_dir(const std::string& upload_dir,
	const char* relative_path);

bool ensure_tag_dir(const std::string& upload_dir, std::string& err);
bool ensure_tag_tables_locked(std::string& err);
bool ensure_default_root_tags_locked(acl::db_sqlite& db, std::string& err);
bool ensure_tag_db_for_request(const std::string& upload_dir, std::string& err);
bool open_tag_db_locked(acl::db_sqlite& db, std::string& err);
bool fetch_tag_locked(acl::db_sqlite& db, const std::string& tag_id,
	TagRow* out, std::string& err);
int get_tag_level_locked(acl::db_sqlite& db, const std::string& tag_id,
	std::string& err);
long long next_sort_order_locked(acl::db_sqlite& db,
	const std::string& parent_id, std::string& err);
std::string make_tag_id_locked();
bool collect_subtree_ids_locked(acl::db_sqlite& db,
	const std::string& root_id, std::vector<std::string>& ids, std::string& err);
bool get_root_tag_locked(acl::db_sqlite& db, const std::string& tag_id,
	TagRow* out, std::string& err);
int root_tag_priority(const TagRow& row);
bool is_protected_root_tag(const TagRow& row);

void append_tag_json(const std::string& upload_dir,
	acl::json& json, acl::json_node& arr, const TagRow& row,
	const std::map<std::string, std::vector<std::string> >& children_by_parent,
	const std::map<std::string, TagRow>& rows_by_id,
	const std::map<std::string, long long>& direct_file_counts);

} // namespace action

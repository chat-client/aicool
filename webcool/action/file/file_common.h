#pragma once

#include "../actions.h"
#include "../action_util.h"

#ifdef _WIN32
#include "../../platform_compat.h"
#else
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <cerrno>
#include <cstdio>
#include <ctime>

#include <algorithm>
#include <map>
#include "common/webcool_mutex.h"
#include <cstdlib>
#include <string>
#include <vector>

namespace action {

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

extern webcool::mutex g_recycle_mutex;
extern std::string g_recycle_db_file;
extern bool g_recycle_db_ready;
extern unsigned long g_recycle_seq;

std::string recycle_db_file_for_upload_dir(const std::string& upload_dir);
extern const char* g_recycle_table_create_sql;
extern const char* g_recycle_table_index_sql;

bool ensure_recycle_tables_locked(std::string& err);
bool ensure_recycle_db_for_request(const std::string& upload_dir, std::string& err);
bool insert_recycle_record(const std::string& upload_dir,
	const std::string& recycle_rel, const std::string& original_path,
	std::string& err);
bool load_recycle_records_map(const std::string& upload_dir,
	std::map<std::string, recycle_record_t>& out, std::string& err);
bool get_recycle_record(const std::string& upload_dir,
	const std::string& recycle_rel, recycle_record_t& rec, bool& found,
	std::string& err);
bool resolve_restore_target_path(const std::string& upload_dir,
	const recycle_record_t& rec, std::string& target_path, std::string& err);
bool delete_recycle_record(const std::string& upload_dir,
	const std::string& recycle_rel, std::string& err);
bool resolve_recycle_item_path(const std::string& upload_dir,
	const std::string& requested_relative_path,
	std::string& resolved_relative_path, bool& is_directory);
bool collect_recycle_directory_entries(const std::string& upload_dir,
	std::map<std::string, recycle_record_t>& recycle_records,
	std::vector<file_entry_t>& entries, std::string& err);
bool delete_directory_recursive(const std::string& full_path, std::string& err);
bool remove_upload_path_recursive(const std::string& full_path, std::string& err);
bool copy_regular_file_plain(const std::string& source, const std::string& dest,
	mode_t mode, std::string& err);
bool soft_delete_to_recycle(const std::string& upload_dir,
	const std::string& file_path, std::string& recycle_path, std::string& err);

bool seek_file64(FILE* fp, long long offset);
std::string remote_file_lock_key(const std::string& path);
void format_upload_time(time_t ts, char* buf, size_t size);
bool request_bool_param(request_t& req, const char* name);
bool should_skip_entry(const char* name, bool show_hidden);
bool is_protected_project_db_file(const std::string& relative_dir,
	const char* name);
bool is_image_file(const char* filename);
bool is_video_file(const char* filename);
bool is_audio_file(const char* filename);
bool is_text_file(const char* filename);
bool is_pdf_file(const char* filename);
const char* image_content_type(const char* filename);
const char* video_content_type(const char* filename);
const char* audio_content_type(const char* filename);
const char* text_content_type(const char* filename);
const char* document_content_type(const char* filename);
bool parse_range_header(const char* range, long long size,
	long long& begin, long long& end);

bool collect_files_recursive(const std::string& upload_dir,
	const std::string& relative_dir, const std::string& folder_password,
	std::vector<file_entry_t>& out, std::string& err, bool show_hidden);

} // namespace action

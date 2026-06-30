#include "stdafx.h"
#include "file_common.h"

namespace action {

std::mutex g_recycle_mutex;
std::string g_recycle_db_file;
bool g_recycle_db_ready = false;
unsigned long g_recycle_seq = 0;
std::string recycle_db_file_for_upload_dir(const std::string& upload_dir)
{
	acl::string path;
	path.format("%s/.recycle_bin.db", upload_dir.c_str());
	return std::string(path.c_str());
}

const char* g_recycle_table_create_sql =
	"CREATE TABLE IF NOT EXISTS recycle_bin ("
	"id INTEGER PRIMARY KEY AUTOINCREMENT,"
	"recycle_name TEXT NOT NULL UNIQUE,"
	"original_path TEXT NOT NULL,"
	"original_name TEXT NOT NULL,"
	"deleted_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
	")";

const char* g_recycle_table_index_sql =
	"CREATE INDEX IF NOT EXISTS idx_recycle_bin_deleted_at"
	" ON recycle_bin(deleted_at DESC)";

bool ensure_recycle_tables_locked(std::string& err) {
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

bool ensure_recycle_db_for_request(const std::string& upload_dir,
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

std::string make_recycle_unique_name(const std::string& original_name) {
	(void) original_name;
	// Caller must hold g_recycle_mutex (see alloc_recycle_target_path).
	const time_t now = time(NULL);
	char buf[128];
	++g_recycle_seq;
	snprintf(buf, sizeof(buf), "%lld_%d_%lu",
		(long long) now, (int) getpid(), g_recycle_seq);
	return std::string(buf);
}

static bool recycle_entry_names_match(const std::string& requested,
	const std::string& actual)
{
	if (requested == actual) {
		return true;
	}
	if (requested.empty() || actual.empty()) {
		return false;
	}
	const size_t prefix = requested.size() > actual.size()
		? actual.size() : requested.size();
	if (prefix >= 16 && requested.compare(0, prefix, actual, 0, prefix) == 0) {
		return true;
	}
	if (requested.size() <= actual.size()) {
		return actual.compare(0, requested.size(), requested) == 0;
	}
	return requested.compare(0, actual.size(), actual) == 0;
}

bool resolve_recycle_item_path(const std::string& upload_dir,
	const std::string& requested_relative_path,
	std::string& resolved_relative_path, bool& is_directory)
{
	resolved_relative_path = requested_relative_path;
	is_directory = upload_directory_exists(upload_dir, requested_relative_path);
	if (is_directory || upload_regular_file_exists(upload_dir, requested_relative_path)) {
		return true;
	}
	if (!is_recycle_file_path(requested_relative_path)) {
		return false;
	}

	const std::string requested_base =
		base_name_from_relative_path(requested_relative_path);
	if (requested_base.empty()) {
		return false;
	}

	std::string matched_path;
	int match_count = 0;
	const std::string recycle_dir =
		join_upload_path(upload_dir, recycle_folder_name());
	DIR* dir = opendir(recycle_dir.c_str());
	if (dir != NULL) {
		struct dirent* entry = NULL;
		while ((entry = readdir(dir)) != NULL) {
			if (should_skip_entry(entry->d_name, false)) {
				continue;
			}
			if (!recycle_entry_names_match(requested_base, entry->d_name)) {
				continue;
			}
			const std::string candidate = std::string(recycle_folder_name())
				+ "/" + entry->d_name;
			matched_path = candidate;
			++match_count;
		}
		closedir(dir);
	}
	if (match_count == 1) {
		resolved_relative_path = matched_path;
		is_directory = upload_directory_exists(upload_dir, matched_path);
		return is_directory
			|| upload_regular_file_exists(upload_dir, matched_path);
	}

	std::map<std::string, recycle_record_t> recycle_records;
	std::string map_err;
	if (!load_recycle_records_map(upload_dir, recycle_records, map_err)) {
		return false;
	}
	match_count = 0;
	for (std::map<std::string, recycle_record_t>::const_iterator it =
		recycle_records.begin(); it != recycle_records.end(); ++it)
	{
		if (!recycle_entry_names_match(requested_base, it->first)) {
			continue;
		}
		const std::string candidate = std::string(recycle_folder_name())
			+ "/" + it->first;
		if (!upload_directory_exists(upload_dir, candidate)
			&& !upload_regular_file_exists(upload_dir, candidate))
		{
			continue;
		}
		matched_path = candidate;
		++match_count;
	}
	if (match_count != 1) {
		return false;
	}
	resolved_relative_path = matched_path;
	is_directory = upload_directory_exists(upload_dir, matched_path);
	return true;
}

std::string build_restore_candidate_path(const std::string& original_path,
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

bool alloc_recycle_target_path(const std::string& upload_dir,
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

	std::lock_guard<std::mutex> guard(g_recycle_mutex);
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

bool insert_recycle_record(const std::string& upload_dir,
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

bool load_recycle_records_map(const std::string& upload_dir,
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

bool get_recycle_record(const std::string& upload_dir,
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

bool resolve_restore_target_path(const std::string& upload_dir,
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

bool delete_recycle_record(const std::string& upload_dir,
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

bool collect_recycle_directory_entries(const std::string& upload_dir,
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

bool delete_directory_recursive(const std::string& full_path,
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

bool remove_upload_path_recursive(const std::string& full_path,
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

bool copy_regular_file_plain(const std::string& source,
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

bool soft_delete_to_recycle(const std::string& upload_dir,
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
bool recycle_bin_insert_record(const std::string& upload_dir,
	const std::string& recycle_rel, const std::string& original_path,
	std::string& err)
{
	return insert_recycle_record(upload_dir, recycle_rel, original_path, err);
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

	g_recycle_db_file = next_db_file.c_str();

	if (!ensure_recycle_tables_locked(err)) {
		return false;
	}

	g_recycle_db_ready = true;
	return true;
}

} // namespace action

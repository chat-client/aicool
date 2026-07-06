#include "stdafx.h"
#include "tag_common.h"

namespace action {

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

	std::lock_guard<webcool::mutex> guard(g_tag_mutex);
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

	std::lock_guard<webcool::mutex> guard(g_tag_mutex);
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

	std::lock_guard<webcool::mutex> guard(g_tag_mutex);
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

} // namespace action

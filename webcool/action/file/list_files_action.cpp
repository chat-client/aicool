#include "stdafx.h"
#include "file_common.h"

namespace action {

bool FilesAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	if (!make_dir_recursive(upload_dir.c_str())) {
		json_error(res, 500, "cannot access upload dir", req.isKeepAlive());
		return true;
	}
	std::string shared_err;
	if (!ensure_shared_upload_dir(shared_err)) {
		json_error(res, 500, shared_err.c_str(), req.isKeepAlive());
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
	if (filter_folder.empty()) {
		if (!collect_files_recursive(upload_dir, shared_folder_name(),
			folder_password, entries, err, show_hidden))
		{
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
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

} // namespace action

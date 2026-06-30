#include "stdafx.h"
#include "local_disk_common.h"

namespace action {

bool LocalDiskListAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string path;
	std::string err;
	std::vector<local_entry_t> entries;
	bool virtual_root = false;
#ifdef _WIN32
	if (is_windows_virtual_root_request(req.getParameter("path"))) {
		path = "/";
		virtual_root = true;
		collect_windows_drive_entries(entries);
	} else
#endif
	{
		if (!normalize_local_path(req.getParameter("path"), path, err)) {
			json_error(res, 400, err.c_str(), req.isKeepAlive());
			return true;
		}

		struct stat st;
		if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
			json_error(res, 404, "directory not found", req.isKeepAlive());
			return true;
		}
		bool allowed = false;
		std::string locked_path;
		const char* password = req.getParameter("local_dir_password");
		if (!local_dir_lock_path_allows(upload_dir, path,
			password ? password : "", allowed, locked_path, err))
		{
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!allowed)
		{
			json_locked_dir_error(res, "directory is locked", path, locked_path,
				req.isKeepAlive());
			return true;
		}

		const char* show_hidden_text = req.getParameter("show_hidden");
		const bool show_hidden = show_hidden_text != NULL
			&& (strcmp(show_hidden_text, "1") == 0
				|| strcasecmp(show_hidden_text, "true") == 0
				|| strcasecmp(show_hidden_text, "yes") == 0);
#ifdef _WIN32
		if (!collect_windows_directory_entries(path, show_hidden, entries, err)) {
			json_error(res, 403, err.c_str(), req.isKeepAlive());
			return true;
		}
#else
		DIR* dir = opendir(path.c_str());
		if (dir == NULL) {
#ifdef __APPLE__
			if ((errno == EPERM || errno == EACCES)
				&& is_current_trash_files_path(path))
			{
				json_error(res, 403,
					"macOS blocked access to Trash. Please grant Full Disk Access to the program or terminal that starts webcool, then restart webcool.",
					req.isKeepAlive());
				return true;
			}
#endif
			json_error(res, 403, strerror(errno), req.isKeepAlive());
			return true;
		}

		struct dirent* entry = NULL;
		while ((entry = readdir(dir)) != NULL) {
			if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
				continue;
			}
			if (!show_hidden && entry->d_name[0] == '.') {
				continue;
			}
			const std::string child_path = join_local_path(path, entry->d_name);
			struct stat child_st;
			if (stat(child_path.c_str(), &child_st) != 0) {
				continue;
			}
			if (!S_ISDIR(child_st.st_mode) && !S_ISREG(child_st.st_mode)) {
				continue;
			}

			char modified_buf[32];
			format_time(child_st.st_mtime, modified_buf, sizeof(modified_buf));
#ifdef __APPLE__
			const time_t created_ts = child_st.st_birthtimespec.tv_sec > 0
				? child_st.st_birthtimespec.tv_sec
				: child_st.st_ctime;
#else
			const time_t created_ts = child_st.st_ctime;
#endif
			char created_buf[32];
			format_time(created_ts, created_buf, sizeof(created_buf));
			local_entry_t item;
			item.name = entry->d_name;
			item.path = child_path;
			item.directory = S_ISDIR(child_st.st_mode);
			item.empty_directory = item.directory && directory_is_empty(child_path);
			item.size = item.directory ? 0 : regular_file_size(child_path);
			item.created_at = (long long) created_ts;
			item.created_time = created_buf;
			item.modified_at = (long long) child_st.st_mtime;
			item.modified_time = modified_buf;
			entries.push_back(item);
		}
		closedir(dir);

		std::sort(entries.begin(), entries.end(),
			[](const local_entry_t& a, const local_entry_t& b) {
				if (a.directory != b.directory) {
					return a.directory > b.directory;
				}
				return a.name < b.name;
			});
#endif
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", path.c_str());
	const std::string parent = virtual_root ? "/" : parent_path(path);
	root.add_text("parent_path", parent.c_str());
	root.add_text("home_path", current_home_path().c_str());
	root.add_bool("virtual_root", virtual_root);
	std::string trash_path;
	if (current_trash_files_path(trash_path, err)) {
		root.add_text("trash_path", trash_path.c_str());
	}
	acl::json_node& items = json.create_array();
	root.add_child("items", items);
	for (size_t i = 0; i < entries.size(); ++i) {
		acl::json_node& item = items.add_child(false, true);
		item.add_text("name", entries[i].name.c_str());
		item.add_text("path", entries[i].path.c_str());
		item.add_bool("directory", entries[i].directory);
		item.add_bool("empty_directory", entries[i].empty_directory);
		if (entries[i].directory && !virtual_root) {
			bool locked = false;
			std::string lock_err;
			if (local_dir_lock_path_has_lock(upload_dir, entries[i].path, locked, lock_err)) {
				item.add_bool("locked", locked);
			}
		} else {
			bool locked = false;
			std::string lock_err;
			if (file_lock_path_has_lock(upload_dir, local_file_lock_key(entries[i].path), locked, lock_err)) {
				item.add_bool("locked", locked);
			}
		}
		item.add_number("size", entries[i].size);
		item.add_number("created_at", entries[i].created_at);
		item.add_text("created_time", entries[i].created_time.c_str());
		item.add_number("modified_at", entries[i].modified_at);
		item.add_text("modified_time", entries[i].modified_time.c_str());
	}
	root.add_number("count", (long long) entries.size());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskDownloadAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string path;
	std::string err;
	if (!normalize_local_path(req.getParameter("path"), path, err)) {
		return sendText(res, 400, err.c_str(), req.isKeepAlive());
	}

	struct stat st;
	if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		return sendText(res, 404, "file not found\n", req.isKeepAlive());
	}
	bool dir_allowed = false;
	std::string locked_dir;
	std::string dir_lock_err;
	if (!local_dir_lock_path_allows(upload_dir, parent_path(path),
		req.getParameter("local_dir_password") ? req.getParameter("local_dir_password") : "",
		dir_allowed, locked_dir, dir_lock_err))
	{
		return sendText(res, 500, dir_lock_err.c_str(), req.isKeepAlive());
	}
	if (!dir_allowed) {
		return sendText(res, 403, "directory is locked\n", req.isKeepAlive());
	}
	bool file_lock_allowed = false;
	std::string lock_err;
	if (!file_lock_path_allows(upload_dir, local_file_lock_key(path),
		req.getParameter("file_password") ? req.getParameter("file_password") : "",
		file_lock_allowed, lock_err))
	{
		return sendText(res, 500, lock_err.c_str(), req.isKeepAlive());
	}
	if (!file_lock_allowed) {
		return sendText(res, 403, "file is locked\n", req.isKeepAlive());
	}

	FILE* in = fopen(path.c_str(), "rb");
	if (in == NULL) {
		return sendText(res, 403, "file cannot be read\n", req.isKeepAlive());
	}

	const long long fsize = regular_file_size(path);
	if (fsize < 0) {
		fclose(in);
		return sendText(res, 500, "cannot read file size\n", req.isKeepAlive());
	}
	if (fsize == 0) {
		fclose(in);
		return sendText(res, 409, "file is empty\n", req.isKeepAlive());
	}
	const std::string name = local_base_name(path);
	const char* ctype = content_type_for_file(name);
	acl::string dispo;
	dispo.format("inline; filename=\"%s\"", name.c_str());
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

	const long long send_begin = has_range ? range_begin : 0;
	const long long send_end = has_range ? range_end : (fsize - 1);
	const long long send_size = send_end - send_begin + 1;
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
		res.setStatus(206)
			.setKeepAlive(req.isKeepAlive())
			.setContentType(ctype)
			.setHeader("Content-Disposition", dispo.c_str())
			.setHeader("Accept-Ranges", "bytes")
			.setHeader("Content-Range", content_range.c_str())
			.setContentLength(send_size);
	} else {
		res.setStatus(200)
			.setKeepAlive(req.isKeepAlive())
			.setContentType(ctype)
			.setHeader("Content-Disposition", dispo.c_str())
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


} // namespace action

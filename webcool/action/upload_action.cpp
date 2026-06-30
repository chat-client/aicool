#include "stdafx.h"
#include "actions.h"
#include "action_util.h"
#ifdef _WIN32
#include "../platform_compat.h"
#else
//#include <sys/stat.h>
#include <unistd.h>
#endif
#include <cerrno>
#include <cstdio>

#include <string>

namespace action {

namespace {

bool seek_file64(FILE* fp, long long offset) {
#ifdef _WIN32
	return _fseeki64(fp, offset, SEEK_SET) == 0;
#else
	return fseeko(fp, offset, SEEK_SET) == 0;
#endif
}

bool copy_mime_body_to_file(const std::string& tmp_path,
	const std::string& dest, const long long body_begin, const long long body_end,
	long long& written, std::string& err) {
	written = 0;
	err.clear();

	if (body_begin < 0 || body_end <= body_begin) {
		err = "invalid mime body range";
		logger_error("invalid mime body range, file=%s, begin=%lld, end=%lld",
			tmp_path.c_str(), body_begin, body_end);
		return false;
	}

	FILE* in = fopen(tmp_path.c_str(), "rb");
	if (in == nullptr) {
		err = strerror(errno);
		logger_error("failed to open %s", tmp_path.c_str());
		return false;
	}

	if (!seek_file64(in, body_begin)) {
		err = strerror(errno);
		fclose(in);
		logger_error("failed to seek file %s: %s", tmp_path.c_str(), acl::last_serror());
		return false;
	}

	FILE* out = fopen(dest.c_str(), "wb");
	if (out == nullptr) {
		err = strerror(errno);
		fclose(in);
		logger_error("failed to open %s: %s", dest.c_str(), acl::last_serror());
		return false;
	}

	char buf[8192];
	long long remaining = body_end - body_begin;
	bool ok = true;
	while (remaining > 0) {
		const size_t want = remaining > static_cast<long long>(sizeof(buf))
			? sizeof(buf) : static_cast<size_t>(remaining);
		const size_t n = fread(buf, 1, want, in);
		if (n == 0) {
			err = ferror(in) ? strerror(errno) : "unexpected end of mime temp file";
			ok = false;
			logger_error("unexpected end of mime temp file %s: %s",
				tmp_path.c_str(), acl::last_serror());
			break;
		}
		if (fwrite(buf, 1, n, out) != n) {
			err = strerror(errno);
			ok = false;
			logger_error("fwrite to %s failed: %s", tmp_path.c_str(), acl::last_serror());
			break;
		}
		written += static_cast<long long>(n);
		remaining -= static_cast<long long>(n);
	}

	if (fclose(out) != 0 && ok) {
		err = strerror(errno);
		ok = false;
		logger_error("fclose %s error: %s", tmp_path.c_str(), acl::last_serror());
	}
	fclose(in);

	if (!ok) {
		unlink(dest.c_str());
		logger_error("unlink %s for %s", dest.c_str(), acl::last_serror());
		return false;
	}
	return true;
}

acl::json_node* check_reuqest(request_t& req, acl::json& json,
	  long long& content_length, acl::http_mime*& mime, int& status) {
	if (req.getRequestType() != acl::HTTP_REQUEST_MULTIPART_FORM) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "request type must be multipart/form-data");
		status = 400;
		return &root;
	}

	mime = req.getHttpMime();
	if (mime == nullptr) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "getHttpMime failed");
		status = 400;
		return &root;
	}

	content_length = req.getContentLength();
	if (content_length <= 0) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "empty request body");
		status = 400;
		return &root;
	}
	status = 200;
	return nullptr;
}

acl::json_node* create_file(const std::string& upload_dir, acl::json& json,
	  acl::string& filepath, acl::ofstream& fp, int& status) {
	if (!make_dir_recursive(upload_dir.c_str())) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "cannot access upload dir");
		logger_error("Create upload dir %s error: %s",
			upload_dir.c_str(), acl::last_serror());
		status = 500;
		return &root;
	}

	filepath.format("%s/.mime_tmp.%u.%d", upload_dir.c_str(),
		static_cast<unsigned>(getpid()), acl::fiber::self());

	if (!fp.open_write(filepath.c_str())) {
		logger_error("open %s error: %s", filepath.c_str(), acl::last_serror());
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "open temp file failed");
		status = 500;
		return &root;
	}
	status = 200;
	return nullptr;
}

acl::json_node* check_lock_permission(const char* passwd,
	  const std::string& upload_dir, const std::string& folder_path,
	  acl::json& json, int& status) {

	bool lock_allowed = false;
	std::string locked_path;
	std::string lock_err;

	if (!folder_lock_path_allows(upload_dir, folder_path, passwd,
		lock_allowed, locked_path, lock_err)) {
		acl::json_node& err = json.create_node();
		err.add_bool("ok", false);
		err.add_text("error", lock_err.c_str());
		status = 500;
		return &err;
	}
	if (!lock_allowed) {
		acl::json_node& err = json.create_node();
		err.add_bool("ok", false);
		err.add_text("error", "folder is locked");
		status = 403;
		return &err;
	}
	status = 200;
	return nullptr;
}

} // namespace

bool UploadAction::run(request_t& req, response_t& res,
	  const std::string& upload_dir) {

	acl::json json;
	acl::http_mime* mime;
	long long content_length;
	int status = 200;
	acl::json_node* err = check_reuqest(req, json, content_length, mime, status);
	if (err) {
		return sendJson(res, status, *err, req.isKeepAlive());
	}

	acl::string tmp_path;
	acl::ofstream fp;
	err = create_file(upload_dir, json, tmp_path, fp, status);
	if (err) {
		return sendJson(res, status, *err, req.isKeepAlive());
	}

	mime->set_saved_path(tmp_path.c_str());

	if (!readBody(req, content_length, fp, *mime)) {
		fp.close();
		unlink(tmp_path.c_str());
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "read request body failed");
		return sendJson(res, 500, root, req.isKeepAlive());
	}
	fp.close();

	////////////////////////////////////////////////////////////////////////////

	std::string folder_path;
	std::string path_err;
	if (!normalize_relative_path(req.getParameter("folder"), folder_path,
		  path_err, true)) {
		unlink(tmp_path.c_str());
		acl::json_node &eroot = json.create_node();
		eroot.add_bool("ok", false);
		eroot.add_text("error", path_err.c_str());
		return sendJson(res, 400, eroot, req.isKeepAlive());
	}

	if (!folder_path.empty() && !upload_directory_exists(upload_dir, folder_path)) {
		unlink(tmp_path.c_str());
		acl::json_node& eroot = json.create_node();
		eroot.add_bool("ok", false);
		eroot.add_text("error", "target folder not found");
		return sendJson(res, 404, eroot, req.isKeepAlive());
	}

	const char* passwd = req.getParameter("folder_password");
	if (passwd == nullptr) {
		passwd = "";
	}
	err = check_lock_permission(passwd, upload_dir, folder_path,
		json, status);
	if (err) {
		unlink(tmp_path.c_str());
		return sendJson(res, status, *err, req.isKeepAlive());
	}

	////////////////////////////////////////////////////////////////////////////

	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	acl::json_node& files = json.create_array();
	root.add_child("files", files);

	int saved_count = 0;
	std::vector<std::string> saved_paths;
	bool ok;
	// 一次写盘过程放在独立线程，防止阻塞当前协程线程
	acl::gofiber_wait_thread([&ok, &mime, &upload_dir, &tmp_path, &files,
			&saved_count, &folder_path, &saved_paths] {
		ok = saveFiles(*mime, upload_dir, tmp_path.c_str(), files,
		               saved_count, folder_path, saved_paths);
	});

	unlink(tmp_path.c_str());

	if (!ok) {
		acl::json_node& eroot = json.create_node();
		eroot.add_bool("ok", false);
		eroot.add_text("error", "save files failed");
		return sendJson(res, 500, eroot, req.isKeepAlive());
	}

	root.add_number("count", saved_count);
	if (!saved_paths.empty()) {
		std::string sync_err;
		if (storage_backup_upload_auto_sync_enabled(upload_dir, sync_err)) {
			std::vector<std::string> delete_paths;
			(void) storage_backup_sync_paths(upload_dir, saved_paths,
				delete_paths, sync_err);
		}
	}
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool UploadAction::readBody(const request_t& req, const long long content_length,
	acl::ofstream& fp, acl::http_mime& mime)
{
	const char* filepath = fp.file_path();
	if (filepath == nullptr) {
		logger_error("filepath is null");
		return false;
	}

	logger_debug(DEBUG_UPLOAD, 1, "[MIME-DEBUG] readBody: content_length=%lld",
		content_length);

	acl::istream& in = req.getInputStream();
	char buf[8192];
	long long read_total = 0;

	while (read_total < content_length) {
		size_t want = sizeof(buf);
		const long long remain = content_length - read_total;
		if (static_cast<long long>(want) > remain) {
			want = static_cast<size_t>(remain);
		}

		const int n = in.read(buf, want);
		if (n < 0) {
			logger_error("read request body error: %s, file=%s",
				acl::last_serror(), filepath);
			return false;
		}
		if (n == 0) {
			logger_error("read 0 bytes before completion, file=%s", filepath);
			return false;
		}

		if (fp.write(buf, n) == -1) {
			logger_error("write tmp file %s error: %s", filepath, acl::last_serror());
			return false;
		}

		read_total += n;
		(void) mime.update(buf, static_cast<size_t>(n));
	}

	if (read_total != content_length) {
		logger_error("request body incomplete: read=%lld, expect=%lld, file=%s",
			read_total, content_length, filepath);
		return false;
	}

	logger_debug(DEBUG_UPLOAD, 1, "readBody completed: total_read=%lld, file=%s",
		read_total, filepath);
	return true;
}

bool UploadAction::saveFiles(const acl::http_mime& mime, const std::string& upload_dir,
	const std::string& tmp_path, acl::json_node& files_array, int& saved_count,
	const std::string& folder_path, std::vector<std::string>& saved_paths)
{
	saved_count = 0;
	saved_paths.clear();

	const std::list<acl::http_mime_node*>& nodes = mime.get_nodes();
	logger_debug(DEBUG_MIME, 1, "[MIME-DEBUG] saveFiles: mime nodes count=%zu,"
		" file=%s", nodes.size(), tmp_path.c_str());

	for (const auto node : nodes) {
		logger_debug(DEBUG_MIME, 1, "[MIME-NODE] type=%d, filename=%s",
             node->get_mime_type(),
             node->get_filename() ? node->get_filename() : "(null)");

		if (node->get_mime_type() != acl::HTTP_MIME_FILE) {
			continue;
		}

		const char* filename = node->get_filename();
		if (filename == nullptr || *filename == '\0') {
			continue;
		}

		const char* basename = acl_safe_basename(filename);
		if (basename == nullptr || *basename == '\0') {
			continue;
		}

		const std::string relative_path = folder_path.empty()
			? std::string(basename) : folder_path + "/" + basename;
		const std::string dest = join_upload_path(upload_dir, relative_path);
		acl::json_node& item = files_array.add_child(false, true);

		const long long body_begin = node->get_bodyBegin();
		const long long body_end = node->get_bodyEnd();
		item.add_number("mime_begin", body_begin);
		item.add_number("mime_end", body_end);
		item.add_number("mime_size", body_end > body_begin ? body_end - body_begin : 0);

		if (is_protected_virtual_path(relative_path)) {
			item.add_text("name", basename);
			item.add_bool("saved", false);
			item.add_text("error", "system file is protected");
			continue;
		}

		logger_debug(DEBUG_MIME, 1, "[MIME-BODY] file=%s, begin=%lld, end=%lld,"
			" size=%lld", basename, body_begin, body_end,
			body_end > body_begin ? body_end - body_begin : 0);

		// Additional node state info
		logger_debug(DEBUG_MIME, 1, "[NODE-STATE] name=%s",
			node->get_name() ? node->get_name() : "(null)");

		if (body_begin < 0 || body_end <= body_begin) {
			logger_debug(DEBUG_MIME, 1, "[MIME-ERROR] MIME parsed empty body "
				"for %s: begin=%lld end=%lld", basename, body_begin, body_end);
			item.add_text("name", basename);
			item.add_number("size", 0);
			item.add_bool("saved", false);
			item.add_text("error", "mime parsed empty body");
			continue;
		}

		std::string save_err;
		long long fsize = -1;
		logger_debug(DEBUG_MIME, 1, "[MIME-SAVE] Extracting %s to %s...",
			basename, dest.c_str());
		if (!copy_mime_body_to_file(tmp_path, dest, body_begin, body_end,
				fsize, save_err)) {
			logger_error("[SAVE-ERROR] node->save() failed for %s: %s, "
	             "body_begin=%lld body_end=%lld", basename, save_err.c_str(),
	             static_cast<long long>(body_begin), static_cast<long long>(body_end));
			item.add_text("name", basename);
			item.add_bool("saved", false);
			item.add_text("error", save_err.c_str());
			continue;
		}

		logger_debug(DEBUG_MIME, 2, "[FILE-CHECK] %s size=%lld", dest.c_str(), fsize);

		if (fsize <= 0) {
			logger_error("[SAVE-FAIL] Saved file is empty or invalid: %s "
				"(%lld bytes), body_begin=%lld body_end=%lld\n",
				dest.c_str(), fsize, static_cast<long long>(body_begin),
				static_cast<long long>(body_end));
			unlink(dest.c_str());
			item.add_text("name", basename);
			item.add_number("size", fsize);
			item.add_bool("saved", false);
			item.add_text("error", "empty or invalid file");
			continue;
		}

		item.add_text("name", basename);
		item.add_text("path", relative_path.c_str());
		item.add_text("folder_path", folder_path.c_str());
		item.add_number("size", fsize);
		item.add_bool("saved", true);
		saved_count++;
		saved_paths.push_back(relative_path);
		logger_debug(DEBUG_MIME, 2, "Saved: %s (%lld bytes), body_begin=%lld "
			"body_end=%lld", dest.c_str(), fsize,
			static_cast<long long>(body_begin), static_cast<long long>(body_end));
	}
	logger_debug(DEBUG_MIME, 1, "[MIME-DEBUG] saveFiles completed: saved_count=%d,"
		" file=%s", saved_count, tmp_path.c_str());
	return true;
}

} // namespace action

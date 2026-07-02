#include "stdafx.h"
#include "actions.h"
#include "action_util.h"
#ifdef _WIN32
#include "../platform_compat.h"
#else
#include <unistd.h>
#endif
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace action {

namespace {

struct StreamUploadTarget {
	std::string folder_path;
	std::string relative_path;
	std::string dest_path;
	std::string tmp_path;
	const char* basename = nullptr;
};

std::mutex g_upload_stream_locks_mutex;
std::unordered_map<std::string, std::weak_ptr<std::mutex> > g_upload_stream_locks;

struct StreamUploadGuard {
	explicit StreamUploadGuard(const std::shared_ptr<std::mutex>& m)
		: mutex(m), lock(*mutex) {}

	std::shared_ptr<std::mutex> mutex;
	std::unique_lock<std::mutex> lock;
};

acl::json_node& make_error_json(acl::json& json, const char* message) {
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", message);
	return root;
}

acl::json_node& make_error_json(acl::json& json, const std::string& message) {
	return make_error_json(json, message.c_str());
}

acl::json_node& make_progress_json(acl::json& json, long long offset,
	long long total_size, bool complete = false) {
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_bool("complete", complete);
	root.add_number("offset", offset);
	root.add_number("total_size", total_size);
	return root;
}

void log_stream_upload_error(const char* where, const char* message) {
	logger_error("upload stream %s error: %s", where, message);
}

void log_stream_upload_error(const char* where, const StreamUploadTarget& target,
	const char* message) {
	logger_error("upload stream %s error: %s, relative_path=%s, tmp=%s, dest=%s",
		where, message, target.relative_path.c_str(), target.tmp_path.c_str(),
		target.dest_path.c_str());
}

bool parse_int64_param(const char* value, long long& out) {
	if (value == nullptr || *value == '\0') {
		return false;
	}
	char* end = nullptr;
	errno = 0;
	const long long parsed = strtoll(value, &end, 10);
	if (errno != 0 || end == value || (end != nullptr && *end != '\0')) {
		return false;
	}
	out = parsed;
	return true;
}

bool parse_positive_total_size(const request_t& req, long long& total_size,
	acl::json& json, acl::json_node*& err) {
	if (!parse_int64_param(req.getParameter("total_size"), total_size)
		|| total_size <= 0) {
		logger_error("upload stream request error: invalid total_size, total_size=%s",
			req.getParameter("total_size") != nullptr
				? req.getParameter("total_size") : "");
		err = &make_error_json(json, "total_size is required");
		return false;
	}
	err = nullptr;
	return true;
}

std::string lowercase_copy(std::string text) {
	for (char & i : text) {
		i = static_cast<char>(std::tolower(static_cast<unsigned char>(i)));
	}
	return text;
}

bool is_valid_md5_hex(const std::string& md5) {
	return md5.size() == 32 &&
		std::all_of(md5.begin(), md5.end(), [](unsigned char ch) {
			return std::isxdigit(ch);
		});
}

std::string optional_normalized_md5(const request_t& req) {
	const char* md5_param = req.getParameter("md5");
	if (md5_param == nullptr || *md5_param == '\0') {
		return {};
	}
	return lowercase_copy(md5_param);
}

bool md5_file_hex(const std::string& path, std::string& hex, std::string& err) {
	char out[33] = {};
	bool res = false;
	acl::gofiber_wait_thread([&] {
		if (acl::md5::md5_file(path.c_str(), nullptr, 0, out, sizeof(out)) < 0) {
			err = "md5 compute failed";
			res = false;
		} else {
			hex = out;
			res = true;
		}
	});
	return res;
}

bool md5_equals(const std::string& expected, const std::string& actual) {
	return lowercase_copy(expected) == lowercase_copy(actual);
}

StreamUploadGuard acquire_stream_upload_lock(const StreamUploadTarget& target) {
	std::shared_ptr<std::mutex> lock;
	{
		std::lock_guard<std::mutex> guard(g_upload_stream_locks_mutex);
		for (auto it = g_upload_stream_locks.begin(); it != g_upload_stream_locks.end();) {
			if (it->second.expired()) {
				it = g_upload_stream_locks.erase(it);
			} else {
				++it;
			}
		}

		std::weak_ptr<std::mutex>& weak = g_upload_stream_locks[target.dest_path];
		lock = weak.lock();
		if (!lock) {
			lock = std::make_shared<std::mutex>();
			weak = lock;
		}
	}
	return StreamUploadGuard(lock);
}

bool resolve_stream_upload_target(const request_t& req, acl::json& json,
	const std::string& upload_dir, StreamUploadTarget& target,
	acl::json_node*& err, int& status) {
	const char* filename_param = req.getParameter("filename");
	if (filename_param == nullptr || *filename_param == '\0') {
		log_stream_upload_error("resolve target", "filename is required");
		acl::json_node& root = make_error_json(json, "filename is required");
		status = 400;
		err = &root;
		return false;
	}

	target.basename = acl_safe_basename(filename_param);
	if (target.basename == nullptr || *target.basename == '\0') {
		logger_error("upload stream resolve target error: invalid filename, filename=%s",
			filename_param);
		acl::json_node& root = make_error_json(json, "invalid filename");
		status = 400;
		err = &root;
		return false;
	}

	std::string path_err;
	if (!normalize_relative_path(req.getParameter("folder"), target.folder_path,
		path_err, true)) {
		logger_error("upload stream resolve target error: %s, folder=%s",
			path_err.c_str(), req.getParameter("folder") != nullptr
				? req.getParameter("folder") : "");
		acl::json_node& root = make_error_json(json, path_err);
		status = 400;
		err = &root;
		return false;
	}

	if (!target.folder_path.empty()
		&& !upload_directory_exists(upload_dir, target.folder_path)) {
		logger_error("upload stream resolve target error: target folder not found,"
			" upload_dir=%s, folder=%s", upload_dir.c_str(),
			target.folder_path.c_str());
		acl::json_node& root = make_error_json(json, "target folder not found");
		status = 404;
		err = &root;
		return false;
	}

	const char* passwd = req.getParameter("folder_password");
	if (passwd == nullptr) {
		passwd = "";
	}

	bool lock_allowed = false;
	std::string locked_path;
	std::string lock_err;
	if (!folder_lock_path_allows(upload_dir, target.folder_path, passwd,
		lock_allowed, locked_path, lock_err)) {
		logger_error("upload stream resolve target error: %s, upload_dir=%s,"
			" folder=%s", lock_err.c_str(), upload_dir.c_str(),
			target.folder_path.c_str());
		acl::json_node& root = make_error_json(json, lock_err);
		status = 500;
		err = &root;
		return false;
	}
	if (!lock_allowed) {
		logger_error("upload stream resolve target error: folder is locked,"
			" upload_dir=%s, folder=%s, locked_path=%s", upload_dir.c_str(),
			target.folder_path.c_str(), locked_path.c_str());
		acl::json_node& root = make_error_json(json, "folder is locked");
		status = 403;
		err = &root;
		return false;
	}

	target.relative_path = target.folder_path.empty()
		? std::string(target.basename)
		: target.folder_path + "/" + target.basename;

	if (is_protected_virtual_path(target.relative_path)) {
		logger_error("upload stream resolve target error: protected path, path=%s",
			target.relative_path.c_str());
		acl::json_node& root = make_error_json(json, "system file is protected");
		status = 403;
		err = &root;
		return false;
	}

	if (!make_dir_recursive(upload_dir.c_str())) {
		logger_error("upload stream resolve target error: cannot access upload dir,"
			" upload_dir=%s, error=%s", upload_dir.c_str(), acl::last_serror());
		acl::json_node& root = make_error_json(json, "cannot access upload dir");
		status = 500;
		err = &root;
		return false;
	}

	target.dest_path = join_upload_path(upload_dir, target.relative_path);
	target.tmp_path = target.dest_path + ".tmp";
	status = 200;
	err = nullptr;
	return true;
}

void append_saved_file_json(acl::json& json, acl::json_node& root,
	const StreamUploadTarget& target, const long long file_size) {
	root.add_bool("ok", true);
	root.add_bool("complete", true);
	root.add_number("count", 1);
	root.add_number("offset", file_size);
	root.add_number("total_size", file_size);
	acl::json_node& files = json.create_array();
	root.add_child("files", files);
	acl::json_node& item = files.add_child(false, true);
	item.add_text("name", target.basename);
	item.add_text("path", target.relative_path.c_str());
	item.add_text("folder_path", target.folder_path.c_str());
	item.add_number("size", file_size);
	item.add_bool("saved", true);
}

bool finalize_stream_upload(const std::string& upload_dir,
	const StreamUploadTarget& target, const std::string& expected_md5,
	const long long expected_size, acl::json& json, acl::json_node& root,
	std::string& err) {
	const long long tmp_size = regular_file_size(target.tmp_path);
	if (tmp_size < 0) {
		log_stream_upload_error("finalize", target, "temp file missing");
		err = "temp file missing";
		return false;
	}
	if (tmp_size != expected_size) {
		logger_error("upload stream finalize error: temp file size mismatch,"
			" relative_path=%s, tmp=%s, actual=%lld, expected=%lld",
			target.relative_path.c_str(), target.tmp_path.c_str(), tmp_size,
			expected_size);
		err = "temp file size mismatch";
		return false;
	}

	std::string actual_md5;
	if (!md5_file_hex(target.tmp_path, actual_md5, err)) {
		logger_error("upload stream finalize error: md5 compute failed,"
			" relative_path=%s, tmp=%s, error=%s", target.relative_path.c_str(),
			target.tmp_path.c_str(), err.c_str());
		return false;
	}
	if (!md5_equals(expected_md5, actual_md5)) {
		logger_error("upload stream finalize error: md5 mismatch,"
			" relative_path=%s, tmp=%s, expected=%s, actual=%s",
			target.relative_path.c_str(), target.tmp_path.c_str(),
			expected_md5.c_str(), actual_md5.c_str());
		unlink(target.tmp_path.c_str());
		err = "md5 mismatch";
		return false;
	}

	if (rename(target.tmp_path.c_str(), target.dest_path.c_str()) != 0) {
		err = acl::last_serror();
		logger_error("upload stream finalize error: rename failed, tmp=%s,"
			" dest=%s, error=%s", target.tmp_path.c_str(),
			target.dest_path.c_str(), err.c_str());
		return false;
	}

	append_saved_file_json(json, root, target, expected_size);

	std::string sync_err;
	if (storage_backup_upload_auto_sync_enabled(upload_dir, sync_err)) {
		std::vector<std::string> saved_paths;
		saved_paths.push_back(target.relative_path);
		std::vector<std::string> delete_paths;
		(void) storage_backup_sync_paths(upload_dir, saved_paths,
			delete_paths, sync_err);
	}
	return true;
}

bool stream_body_to_file_at_offset(acl::istream& in, const long long content_length,
	const long long offset, const std::string& path, long long& written,
	std::string& err) {
	written = 0;
	err.clear();

	FILE* out = nullptr;
	if (offset == 0) {
		out = fopen(path.c_str(), "wb");
	} else {
		out = fopen(path.c_str(), "r+b");
		if (out != nullptr) {
#ifdef _WIN32
			if (_fseeki64(out, offset, SEEK_SET) != 0) {
#else
			if (fseeko(out, offset, SEEK_SET) != 0) {
#endif
				err = strerror(errno);
				logger_error("upload stream write error: seek failed, path=%s,"
					" offset=%lld, error=%s", path.c_str(), offset, err.c_str());
				fclose(out);
				return false;
			}
		}
	}
	if (out == nullptr) {
		err = strerror(errno);
		logger_error("upload stream write error: open failed, path=%s,"
			" offset=%lld, error=%s", path.c_str(), offset, err.c_str());
		return false;
	}

	char buf[65536];
	long long read_total = 0;
	while (read_total < content_length) {
		size_t want = sizeof(buf);
		const long long remain = content_length - read_total;
		if (static_cast<long long>(want) > remain) {
			want = static_cast<size_t>(remain);
		}

		const int n = in.read(buf, want);
		if (n < 0) {
			err = acl::last_serror();
			logger_error("upload stream write error: read request body failed,"
				" path=%s, read_total=%lld, expected=%lld, error=%s",
				path.c_str(), read_total, content_length, err.c_str());
			fclose(out);
			return false;
		}
		if (n == 0) {
			err = "request body incomplete";
			logger_error("upload stream write error: request body incomplete,"
				" path=%s, read_total=%lld, expected=%lld", path.c_str(),
				read_total, content_length);
			fclose(out);
			return false;
		}
		if (fwrite(buf, 1, static_cast<size_t>(n), out) != static_cast<size_t>(n)) {
			err = strerror(errno);
			logger_error("upload stream write error: fwrite failed, path=%s,"
				" read_total=%lld, bytes=%d, error=%s", path.c_str(),
				read_total, n, err.c_str());
			fclose(out);
			return false;
		}
		read_total += n;
		written += n;
	}

	if (fclose(out) != 0) {
		err = strerror(errno);
		logger_error("upload stream write error: close failed, path=%s,"
			" error=%s", path.c_str(), err.c_str());
		return false;
	}

	if (read_total != content_length) {
		err = "request body size mismatch";
		logger_error("upload stream write error: request body size mismatch,"
			" path=%s, read_total=%lld, expected=%lld", path.c_str(),
			read_total, content_length);
		return false;
	}
	return true;
}

} // namespace

bool UploadStreamAction::status(request_t& req, response_t& res,
	const std::string& upload_dir) {
	acl::json json;
	StreamUploadTarget target;
	acl::json_node* err = nullptr;
	int status = 200;
	if (!resolve_stream_upload_target(req, json, upload_dir, target, err, status)) {
		return sendJson(res, status, *err, req.isKeepAlive());
	}

	StreamUploadGuard upload_guard = acquire_stream_upload_lock(target);

	long long total_size = 0;
	if (!parse_positive_total_size(req, total_size, json, err)) {
		return sendJson(res, 400, *err, req.isKeepAlive());
	}

	const std::string expected_md5 = optional_normalized_md5(req);
	if (!expected_md5.empty() && !is_valid_md5_hex(expected_md5)) {
		logger_error("upload stream status error: invalid md5, path=%s, md5=%s",
			target.relative_path.c_str(), expected_md5.c_str());
		acl::json_node& root = make_error_json(json, "invalid md5");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	const long long dest_size = regular_file_size(target.dest_path);
	if (dest_size == total_size) {
		if (!expected_md5.empty()) {
			std::string actual_md5;
			std::string md5_err;
			if (md5_file_hex(target.dest_path, actual_md5, md5_err)
				&& md5_equals(expected_md5, actual_md5)) {
				acl::json_node& root = json.create_node();
				append_saved_file_json(json, root, target, total_size);
				unlink(target.tmp_path.c_str());
				return sendJson(res, 200, root, req.isKeepAlive());
			}
			logger_error("upload stream status error: existing destination md5"
				" mismatch or compute failed, path=%s, dest=%s, expected=%s,"
				" actual=%s, error=%s", target.relative_path.c_str(),
				target.dest_path.c_str(), expected_md5.c_str(),
				actual_md5.c_str(), md5_err.c_str());
		} else {
			acl::json_node& root = make_progress_json(json, total_size,
				total_size, true);
			return sendJson(res, 200, root, req.isKeepAlive());
		}
	}

	long long tmp_size = regular_file_size(target.tmp_path);
	if (tmp_size < 0) {
		tmp_size = 0;
	}
	if (tmp_size > total_size) {
		logger_error("upload stream status error: temp file larger than total,"
			" path=%s, tmp=%s, tmp_size=%lld, total_size=%lld",
			target.relative_path.c_str(), target.tmp_path.c_str(), tmp_size,
			total_size);
		unlink(target.tmp_path.c_str());
		tmp_size = 0;
	}

	if (tmp_size == total_size && !expected_md5.empty()) {
		acl::json_node& root = json.create_node();
		std::string finalize_err;
		if (finalize_stream_upload(upload_dir, target, expected_md5, total_size,
			json, root, finalize_err)) {
			return sendJson(res, 200, root, req.isKeepAlive());
		}
		logger_error("upload stream status error: finalize failed, path=%s,"
			" tmp=%s, error=%s", target.relative_path.c_str(),
			target.tmp_path.c_str(), finalize_err.c_str());
		unlink(target.tmp_path.c_str());
		tmp_size = 0;
	}

	acl::json_node& root = make_progress_json(json, tmp_size, total_size);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool UploadStreamAction::run(request_t& req, response_t& res,
	const std::string& upload_dir) {
	acl::json json;

	if (req.getRequestType() == acl::HTTP_REQUEST_MULTIPART_FORM) {
		log_stream_upload_error("run", "multipart request sent to stream endpoint");
		acl::json_node& root = make_error_json(json,
			"use /api/v1/upload for multipart uploads");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	const long long content_length = req.getContentLength();
	if (content_length < 0) {
		logger_error("upload stream run error: invalid content length=%lld",
			content_length);
		acl::json_node& root = make_error_json(json, "invalid request body");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	StreamUploadTarget target;
	acl::json_node* err = nullptr;
	int status = 200;
	if (!resolve_stream_upload_target(req, json, upload_dir, target, err, status)) {
		return sendJson(res, status, *err, req.isKeepAlive());
	}

	StreamUploadGuard upload_guard = acquire_stream_upload_lock(target);

	long long total_size = 0;
	if (!parse_positive_total_size(req, total_size, json, err)) {
		return sendJson(res, 400, *err, req.isKeepAlive());
	}

	long long offset = 0;
	if (req.getParameter("offset") != nullptr
		&& !parse_int64_param(req.getParameter("offset"), offset)) {
		logger_error("upload stream run error: invalid offset, path=%s,"
			" offset=%s", target.relative_path.c_str(),
			req.getParameter("offset"));
		acl::json_node& root = make_error_json(json, "invalid offset");
		return sendJson(res, 400, root, req.isKeepAlive());
	}
	if (offset < 0 || offset > total_size) {
		logger_error("upload stream run error: offset out of range, path=%s,"
			" offset=%lld, total_size=%lld", target.relative_path.c_str(),
			offset, total_size);
		acl::json_node& root = make_error_json(json, "invalid offset");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	const std::string expected_md5 = optional_normalized_md5(req);
	if (!expected_md5.empty() && !is_valid_md5_hex(expected_md5)) {
		logger_error("upload stream run error: invalid md5, path=%s, md5=%s",
			target.relative_path.c_str(), expected_md5.c_str());
		acl::json_node& root = make_error_json(json, "invalid md5");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	const long long dest_size = regular_file_size(target.dest_path);
	if (dest_size == total_size) {
		if (!expected_md5.empty()) {
			std::string actual_md5;
			std::string md5_err;
			if (md5_file_hex(target.dest_path, actual_md5, md5_err)
				&& md5_equals(expected_md5, actual_md5)) {
				acl::json_node& root = json.create_node();
				append_saved_file_json(json, root, target, total_size);
				unlink(target.tmp_path.c_str());
				return sendJson(res, 200, root, req.isKeepAlive());
			}
			logger_error("upload stream run error: existing destination md5"
				" mismatch or compute failed, path=%s, dest=%s, expected=%s,"
				" actual=%s, error=%s", target.relative_path.c_str(),
				target.dest_path.c_str(), expected_md5.c_str(),
				actual_md5.c_str(), md5_err.c_str());
		} else {
			acl::json_node& root = json.create_node();
			append_saved_file_json(json, root, target, total_size);
			unlink(target.tmp_path.c_str());
			return sendJson(res, 200, root, req.isKeepAlive());
		}
	}

	long long tmp_size = regular_file_size(target.tmp_path);
	if (tmp_size < 0) {
		tmp_size = 0;
	}
	if (tmp_size > total_size) {
		logger_error("upload stream run error: temp file larger than total,"
			" path=%s, tmp=%s, tmp_size=%lld, total_size=%lld",
			target.relative_path.c_str(), target.tmp_path.c_str(), tmp_size,
			total_size);
		unlink(target.tmp_path.c_str());
		tmp_size = 0;
	}

	if (offset == 0 && tmp_size > 0) {
		logger_error("upload stream run notice: restarting upload, path=%s,"
			" tmp=%s, old_tmp_size=%lld", target.relative_path.c_str(),
			target.tmp_path.c_str(), tmp_size);
		unlink(target.tmp_path.c_str());
		tmp_size = 0;
	} else if (offset > 0 && tmp_size != offset) {
		logger_error("upload stream run error: offset mismatch, path=%s,"
			" tmp_size=%lld, offset=%lld, total_size=%lld",
			target.relative_path.c_str(), tmp_size, offset, total_size);
		acl::json_node& root = make_error_json(json, "offset mismatch");
		root.add_number("offset", tmp_size);
		root.add_number("total_size", total_size);
		return sendJson(res, 409, root, req.isKeepAlive());
	}

	if (content_length > total_size - offset) {
		logger_error("upload stream run error: upload exceeds total_size,"
			" path=%s, offset=%lld, content_length=%lld, total_size=%lld",
			target.relative_path.c_str(), offset, content_length, total_size);
		acl::json_node& root = make_error_json(json, "upload exceeds total_size");
		return sendJson(res, 400, root, req.isKeepAlive());
	}
	const long long next_offset = offset + content_length;

	if (content_length > 0) {
		long long written = 0;
		std::string stream_err;
		acl::istream& in = req.getInputStream();
		if (!stream_body_to_file_at_offset(in, content_length, offset,
			target.tmp_path, written, stream_err)) {
			acl::json_node& root = make_error_json(json, stream_err.empty()
				? "read request body failed" : stream_err.c_str());
			return sendJson(res, 500, root, req.isKeepAlive());
		}
		tmp_size = offset + written;
	} else {
		tmp_size = offset;
	}

	if (next_offset < total_size) {
		acl::json_node& root = make_progress_json(json, tmp_size, total_size);
		return sendJson(res, 200, root, req.isKeepAlive());
	}

	if (expected_md5.empty()) {
		logger_error("upload stream run error: md5 required to finalize, path=%s,"
			" offset=%lld, content_length=%lld, total_size=%lld",
			target.relative_path.c_str(), offset, content_length, total_size);
		acl::json_node& root = make_error_json(json,
			"md5 is required to finalize upload");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	acl::json_node& root = json.create_node();
	std::string finalize_err;
	if (!finalize_stream_upload(upload_dir, target, expected_md5, total_size,
		json, root, finalize_err)) {
		logger_error("upload stream run error: finalize failed, path=%s,"
			" tmp=%s, error=%s", target.relative_path.c_str(),
			target.tmp_path.c_str(), finalize_err.c_str());
		acl::json_node& eroot = make_error_json(json, finalize_err.empty()
			? "finalize upload failed" : finalize_err.c_str());
		return sendJson(res, 500, eroot, req.isKeepAlive());
	}
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

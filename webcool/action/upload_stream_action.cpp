#include "stdafx.h"
#include "actions.h"
#include "action_util.h"
#ifdef _WIN32
#include "../platform_compat.h"
#else
#include <unistd.h>
#endif
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace action {

namespace {

struct StreamUploadTarget {
	std::string folder_path;
	std::string relative_path;
	std::string dest_path;
	std::string tmp_path;
	const char* basename = nullptr;
};

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

std::string lowercase_copy(std::string text) {
	for (size_t i = 0; i < text.size(); ++i) {
		text[i] = static_cast<char>(std::tolower(
			static_cast<unsigned char>(text[i])));
	}
	return text;
}

bool is_valid_md5_hex(const std::string& md5) {
	if (md5.size() != 32) {
		return false;
	}
	for (size_t i = 0; i < md5.size(); ++i) {
		const unsigned char ch = static_cast<unsigned char>(md5[i]);
		if (!std::isxdigit(ch)) {
			return false;
		}
	}
	return true;
}

bool md5_file_hex(const std::string& path, std::string& hex, std::string& err) {
	char out[33] = {};
	if (acl::md5::md5_file(path.c_str(), nullptr, 0, out, sizeof(out)) < 0) {
		err = "md5 compute failed";
		return false;
	}
	hex = out;
	return true;
}

bool md5_equals(const std::string& expected, const std::string& actual) {
	return lowercase_copy(expected) == lowercase_copy(actual);
}

bool resolve_stream_upload_target(request_t& req, acl::json& json,
	const std::string& upload_dir, StreamUploadTarget& target,
	acl::json_node*& err, int& status) {
	const char* filename_param = req.getParameter("filename");
	if (filename_param == nullptr || *filename_param == '\0') {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "filename is required");
		status = 400;
		err = &root;
		return false;
	}

	target.basename = acl_safe_basename(filename_param);
	if (target.basename == nullptr || *target.basename == '\0') {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "invalid filename");
		status = 400;
		err = &root;
		return false;
	}

	std::string path_err;
	if (!normalize_relative_path(req.getParameter("folder"), target.folder_path,
		path_err, true)) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", path_err.c_str());
		status = 400;
		err = &root;
		return false;
	}

	if (!target.folder_path.empty()
		&& !upload_directory_exists(upload_dir, target.folder_path)) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "target folder not found");
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
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", lock_err.c_str());
		status = 500;
		err = &root;
		return false;
	}
	if (!lock_allowed) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "folder is locked");
		status = 403;
		err = &root;
		return false;
	}

	target.relative_path = target.folder_path.empty()
		? std::string(target.basename)
		: target.folder_path + "/" + target.basename;

	if (is_protected_virtual_path(target.relative_path)) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "system file is protected");
		status = 403;
		err = &root;
		return false;
	}

	if (!make_dir_recursive(upload_dir.c_str())) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "cannot access upload dir");
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
		err = "temp file missing";
		return false;
	}
	if (tmp_size != expected_size) {
		err = "temp file size mismatch";
		return false;
	}

	std::string actual_md5;
	if (!md5_file_hex(target.tmp_path, actual_md5, err)) {
		return false;
	}
	if (!md5_equals(expected_md5, actual_md5)) {
		unlink(target.tmp_path.c_str());
		err = "md5 mismatch";
		return false;
	}

	if (::rename(target.tmp_path.c_str(), target.dest_path.c_str()) != 0) {
		err = acl::last_serror();
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
				fclose(out);
				return false;
			}
		}
	}
	if (out == nullptr) {
		err = strerror(errno);
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
			fclose(out);
			return false;
		}
		if (n == 0) {
			err = "request body incomplete";
			fclose(out);
			return false;
		}
		if (fwrite(buf, 1, static_cast<size_t>(n), out) != static_cast<size_t>(n)) {
			err = strerror(errno);
			fclose(out);
			return false;
		}
		read_total += n;
		written += n;
	}

	if (fclose(out) != 0) {
		err = strerror(errno);
		return false;
	}

	if (read_total != content_length) {
		err = "request body size mismatch";
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

	long long total_size = 0;
	if (!parse_int64_param(req.getParameter("total_size"), total_size)
		|| total_size <= 0) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "total_size is required");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	const char* md5_param = req.getParameter("md5");
	std::string expected_md5;
	if (md5_param != nullptr && *md5_param != '\0') {
		expected_md5 = lowercase_copy(md5_param);
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
		} else {
			acl::json_node& root = json.create_node();
			root.add_bool("ok", true);
			root.add_bool("complete", true);
			root.add_number("offset", total_size);
			root.add_number("total_size", total_size);
			return sendJson(res, 200, root, req.isKeepAlive());
		}
	}

	long long tmp_size = regular_file_size(target.tmp_path);
	if (tmp_size < 0) {
		tmp_size = 0;
	}
	if (tmp_size > total_size) {
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
		unlink(target.tmp_path.c_str());
		tmp_size = 0;
	}

	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_bool("complete", false);
	root.add_number("offset", tmp_size);
	root.add_number("total_size", total_size);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool UploadStreamAction::run(request_t& req, response_t& res,
	const std::string& upload_dir) {
	acl::json json;

	if (req.getRequestType() == acl::HTTP_REQUEST_MULTIPART_FORM) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "use /api/v1/upload for multipart uploads");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	const long long content_length = req.getContentLength();
	if (content_length < 0) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "invalid request body");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	StreamUploadTarget target;
	acl::json_node* err = nullptr;
	int status = 200;
	if (!resolve_stream_upload_target(req, json, upload_dir, target, err, status)) {
		return sendJson(res, status, *err, req.isKeepAlive());
	}

	long long total_size = 0;
	if (!parse_int64_param(req.getParameter("total_size"), total_size)
		|| total_size <= 0) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "total_size is required");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	long long offset = 0;
	if (req.getParameter("offset") != nullptr
		&& !parse_int64_param(req.getParameter("offset"), offset)) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "invalid offset");
		return sendJson(res, 400, root, req.isKeepAlive());
	}
	if (offset < 0 || offset > total_size) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "invalid offset");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	const char* md5_param = req.getParameter("md5");
	std::string expected_md5;
	if (md5_param != nullptr && *md5_param != '\0') {
		expected_md5 = lowercase_copy(md5_param);
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
		unlink(target.tmp_path.c_str());
		tmp_size = 0;
	}

	if (offset == 0 && tmp_size > 0) {
		unlink(target.tmp_path.c_str());
		tmp_size = 0;
	} else if (offset > 0 && tmp_size != offset) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "offset mismatch");
		root.add_number("offset", tmp_size);
		root.add_number("total_size", total_size);
		return sendJson(res, 409, root, req.isKeepAlive());
	}

	const long long next_offset = offset + content_length;
	if (next_offset > total_size) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "upload exceeds total_size");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	if (content_length > 0) {
		long long written = 0;
		std::string stream_err;
		acl::istream& in = req.getInputStream();
		if (!stream_body_to_file_at_offset(in, content_length, offset,
			target.tmp_path, written, stream_err)) {
			acl::json_node& root = json.create_node();
			root.add_bool("ok", false);
			root.add_text("error", stream_err.empty()
				? "read request body failed" : stream_err.c_str());
			return sendJson(res, 500, root, req.isKeepAlive());
		}
		tmp_size = offset + written;
	} else {
		tmp_size = offset;
	}

	if (next_offset < total_size) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_bool("complete", false);
		root.add_number("offset", tmp_size);
		root.add_number("total_size", total_size);
		return sendJson(res, 200, root, req.isKeepAlive());
	}

	if (expected_md5.empty() || !is_valid_md5_hex(expected_md5)) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "md5 is required to finalize upload");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	acl::json_node& root = json.create_node();
	std::string finalize_err;
	if (!finalize_stream_upload(upload_dir, target, expected_md5, total_size,
		json, root, finalize_err)) {
		acl::json_node& eroot = json.create_node();
		eroot.add_bool("ok", false);
		eroot.add_text("error", finalize_err.empty()
			? "finalize upload failed" : finalize_err.c_str());
		return sendJson(res, 500, eroot, req.isKeepAlive());
	}
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

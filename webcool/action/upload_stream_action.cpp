#include "stdafx.h"
#include "actions.h"
#include "action_util.h"
#ifdef _WIN32
#include "../platform_compat.h"
#else
#include <unistd.h>
#endif
#include <cerrno>
#include <cstdio>
#include <string>

namespace action {

namespace {

bool stream_body_to_file(acl::istream& in, const long long content_length,
	FILE* out, long long& written, std::string& err) {
	written = 0;
	err.clear();

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
			return false;
		}
		if (n == 0) {
			err = "request body incomplete";
			return false;
		}
		if (fwrite(buf, 1, static_cast<size_t>(n), out) != static_cast<size_t>(n)) {
			err = strerror(errno);
			return false;
		}
		read_total += n;
		written += n;
	}

	if (read_total != content_length) {
		err = "request body size mismatch";
		return false;
	}
	return true;
}

} // namespace

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
	if (content_length <= 0) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "empty request body");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	const char* filename_param = req.getParameter("filename");
	if (filename_param == nullptr || *filename_param == '\0') {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "filename is required");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	const char* basename = acl_safe_basename(filename_param);
	if (basename == nullptr || *basename == '\0') {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "invalid filename");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	std::string folder_path;
	std::string path_err;
	if (!normalize_relative_path(req.getParameter("folder"), folder_path,
		path_err, true)) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", path_err.c_str());
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	if (!folder_path.empty() && !upload_directory_exists(upload_dir, folder_path)) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "target folder not found");
		return sendJson(res, 404, root, req.isKeepAlive());
	}

	const char* passwd = req.getParameter("folder_password");
	if (passwd == nullptr) {
		passwd = "";
	}

	bool lock_allowed = false;
	std::string locked_path;
	std::string lock_err;
	if (!folder_lock_path_allows(upload_dir, folder_path, passwd,
		lock_allowed, locked_path, lock_err)) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", lock_err.c_str());
		return sendJson(res, 500, root, req.isKeepAlive());
	}
	if (!lock_allowed) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "folder is locked");
		return sendJson(res, 403, root, req.isKeepAlive());
	}

	const std::string relative_path = folder_path.empty()
		? std::string(basename) : folder_path + "/" + basename;

	if (is_protected_virtual_path(relative_path)) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "system file is protected");
		return sendJson(res, 403, root, req.isKeepAlive());
	}

	if (!make_dir_recursive(upload_dir.c_str())) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "cannot access upload dir");
		return sendJson(res, 500, root, req.isKeepAlive());
	}

	const std::string dest = join_upload_path(upload_dir, relative_path);

	FILE* out = fopen(dest.c_str(), "wb");
	if (out == nullptr) {
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "open destination file failed");
		return sendJson(res, 500, root, req.isKeepAlive());
	}

	long long written = 0;
	std::string stream_err;
	acl::istream& in = req.getInputStream();
	const bool ok = stream_body_to_file(in, content_length, out, written, stream_err);
	if (fclose(out) != 0 && ok) {
		stream_err = strerror(errno);
	}

	if (!ok) {
		unlink(dest.c_str());
		acl::json_node& root = json.create_node();
		root.add_text("error", stream_err.empty()
			? "read request body failed" : stream_err.c_str());
		root.add_bool("ok", false);
		return sendJson(res, 500, root, req.isKeepAlive());
	}

	if (written <= 0) {
		unlink(dest.c_str());
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "empty or invalid file");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_number("count", 1);
	acl::json_node& files = json.create_array();
	root.add_child("files", files);
	acl::json_node& item = files.add_child(false, true);
	item.add_text("name", basename);
	item.add_text("path", relative_path.c_str());
	item.add_text("folder_path", folder_path.c_str());
	item.add_number("size", written);
	item.add_bool("saved", true);

	std::string sync_err;
	if (storage_backup_upload_auto_sync_enabled(upload_dir, sync_err)) {
		std::vector<std::string> saved_paths;
		saved_paths.push_back(relative_path);
		std::vector<std::string> delete_paths;
		(void) storage_backup_sync_paths(upload_dir, saved_paths,
			delete_paths, sync_err);
	}

	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

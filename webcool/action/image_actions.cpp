#include "stdafx.h"
#include "actions.h"
#include "action_util.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include "../platform_compat.h"
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <list>
#include <string>

namespace action {
namespace {

#if 0
static void json_error(response_t& res, int status, const char* msg,
	bool keep_alive)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", msg ? msg : "unknown error");
	sendJson(res, status, root, keep_alive);
}
#endif

static bool ends_with_ignore_case(const std::string& text, const char* suffix)
{
	const size_t n = suffix ? strlen(suffix) : 0;
	if (text.size() < n) {
		return false;
	}
	for (size_t i = 0; i < n; ++i) {
		char a = text[text.size() - n + i];
		char b = suffix[i];
		if (a >= 'A' && a <= 'Z') {
			a = (char) (a - 'A' + 'a');
		}
		if (b >= 'A' && b <= 'Z') {
			b = (char) (b - 'A' + 'a');
		}
		if (a != b) {
			return false;
		}
	}
	return true;
}

static bool is_editable_image_name(const std::string& name)
{
	return ends_with_ignore_case(name, ".png")
		|| ends_with_ignore_case(name, ".jpg")
		|| ends_with_ignore_case(name, ".jpeg");
}

static std::string parent_path(const std::string& path)
{
	if (path.empty() || path == "/") {
		return "/";
	}
	std::string text = path;
	while (text.size() > 1 && text[text.size() - 1] == '/') {
		text.erase(text.size() - 1);
	}
	std::string::size_type pos = text.rfind('/');
	if (pos == std::string::npos || pos == 0) {
		return "/";
	}
	return text.substr(0, pos);
}

static std::string base_name(const std::string& path)
{
	std::string text = path;
	while (text.size() > 1 && text[text.size() - 1] == '/') {
		text.erase(text.size() - 1);
	}
	std::string::size_type pos = text.rfind('/');
	return pos == std::string::npos ? text : text.substr(pos + 1);
}

static bool normalize_local_path(const char* input, std::string& out,
	std::string& err)
{
	err.clear();
	std::string text = input ? input : "";
	if (text.empty() || text[0] != '/') {
		err = "absolute path is required";
		return false;
	}
	char resolved[PATH_MAX];
	if (realpath(text.c_str(), resolved) == NULL) {
		err = strerror(errno);
		return false;
	}
	out = resolved;
	return true;
}

static std::string remote_file_lock_key(const std::string& path)
{
	return std::string("remote:") + path;
}

static std::string local_file_lock_key(const std::string& path)
{
	return std::string("local:") + path;
}

static bool read_request_mime(request_t& req, const std::string& upload_dir,
	acl::http_mime& mime, std::string& tmp_path, std::string& err)
{
	long long content_length = req.getContentLength();
	if (content_length <= 0) {
		err = "empty request body";
		return false;
	}
	if (!make_dir_recursive(upload_dir.c_str())) {
		err = "cannot access upload dir";
		return false;
	}

	acl::string tmp;
	tmp.format("%s/.image_edit_tmp.%u.%d", upload_dir.c_str(),
		(unsigned) getpid(), acl::fiber::self());
	tmp_path = tmp.c_str();

	acl::ofstream fp;
	if (!fp.open_write(tmp_path.c_str())) {
		err = "open temp file failed";
		return false;
	}
	mime.set_saved_path(tmp_path.c_str());

	acl::istream& in = req.getInputStream();
	char buf[8192];
	long long read_total = 0;
	while (read_total < content_length) {
		size_t want = sizeof(buf);
		long long remain = content_length - read_total;
		if ((long long) want > remain) {
			want = (size_t) remain;
		}
		int n = in.read(buf, want);
		if (n <= 0) {
			err = "read request body failed";
			fp.close();
			::unlink(tmp_path.c_str());
			return false;
		}
		if (fp.write(buf, n) == -1) {
			err = "write temp file failed";
			fp.close();
			::unlink(tmp_path.c_str());
			return false;
		}
		read_total += n;
		(void) mime.update(buf, (size_t) n);
	}
	fp.close();
	return true;
}

static const acl::http_mime_node* first_file_node(acl::http_mime& mime)
{
	const std::list<acl::http_mime_node*>& nodes = mime.get_nodes();
	for (std::list<acl::http_mime_node*>::const_iterator it = nodes.begin();
		it != nodes.end(); ++it)
	{
		const acl::http_mime_node* node = *it;
		if (node != NULL && node->get_mime_type() == acl::HTTP_MIME_FILE) {
			return node;
		}
	}
	return NULL;
}

static bool copy_mime_body_to_file(const std::string& tmp_path,
	const acl::http_mime_node* node, const std::string& dest,
	std::string& err)
{
	if (node == NULL) {
		err = "image file part is missing";
		return false;
	}
	const long long begin = node->get_bodyBegin();
	const long long end = node->get_bodyEnd();
	if (end <= begin) {
		err = "image body is empty";
		return false;
	}

	FILE* in = fopen(tmp_path.c_str(), "rb");
	if (in == NULL) {
		err = strerror(errno);
		return false;
	}
	acl::string tmp_dest;
	tmp_dest.format("%s.editing.%u.%d", dest.c_str(),
		(unsigned) getpid(), acl::fiber::self());
	FILE* out = fopen(tmp_dest.c_str(), "wb");
	if (out == NULL) {
		err = strerror(errno);
		fclose(in);
		return false;
	}
#ifdef _WIN32
	if (_fseeki64(in, begin, SEEK_SET)) {
#else
	if (fseeko(in, begin, SEEK_SET) != 0) {
#endif
		err = strerror(errno);
		fclose(out);
		fclose(in);
		::unlink(tmp_dest.c_str());
		return false;
	}


	char buf[8192];
	long long remain = end - begin;
	while (remain > 0) {
		size_t want = sizeof(buf);
		if ((long long)  want > remain) {
			want = (size_t) remain;
		}
		size_t n = fread(buf, 1, want, in);
		if (n == 0) {
			err = ferror(in) ? strerror(errno) : "unexpected end of image body";
			fclose(out);
			fclose(in);
			::unlink(tmp_dest.c_str());
			return false;
		}
		if (fwrite(buf, 1, n, out) != n) {
			err = strerror(errno);
			fclose(out);
			fclose(in);
			::unlink(tmp_dest.c_str());
			return false;
		}
		remain -= (off_t) n;
	}
	if (fclose(out) != 0) {
		err = strerror(errno);
		fclose(in);
		::unlink(tmp_dest.c_str());
		return false;
	}
	fclose(in);
	if (::rename(tmp_dest.c_str(), dest.c_str()) != 0) {
		err = strerror(errno);
		::unlink(tmp_dest.c_str());
		return false;
	}
	return true;
}

} // namespace

bool ImageSaveAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	if (req.getRequestType() != acl::HTTP_REQUEST_MULTIPART_FORM) {
		json_error(res, 400, "request type must be multipart/form-data", req.isKeepAlive());
		return true;
	}
	acl::http_mime* mime = req.getHttpMime();
	if (mime == NULL) {
		json_error(res, 400, "getHttpMime failed", req.isKeepAlive());
		return true;
	}

	const bool local = req.getParameter("local") != NULL
		&& strcmp(req.getParameter("local"), "1") == 0;
	std::string target_path;
	std::string display_name;
	std::string err;
	if (local) {
		if (!normalize_local_path(req.getParameter("file"), target_path, err)) {
			json_error(res, 400, err.c_str(), req.isKeepAlive());
			return true;
		}
		display_name = base_name(target_path);
		bool dir_allowed = false;
		std::string locked_dir;
		if (!local_dir_lock_path_allows(upload_dir, parent_path(target_path),
			req.getParameter("local_dir_password") ? req.getParameter("local_dir_password") : "",
			dir_allowed, locked_dir, err))
		{
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!dir_allowed) {
			json_error(res, 403, "directory is locked", req.isKeepAlive());
			return true;
		}
		bool file_allowed = false;
		if (!file_lock_path_allows(upload_dir, local_file_lock_key(target_path),
			req.getParameter("file_password") ? req.getParameter("file_password") : "",
			file_allowed, err))
		{
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!file_allowed) {
			json_error(res, 403, "file is locked", req.isKeepAlive());
			return true;
		}
	} else {
		std::string relative;
		if (!normalize_relative_path(req.getParameter("file"), relative, err, false)) {
			json_error(res, 400, err.c_str(), req.isKeepAlive());
			return true;
		}
		if (is_protected_virtual_path(relative)) {
			json_error(res, 403, "system file is protected", req.isKeepAlive());
			return true;
		}
		display_name = base_name_from_relative_path(relative);
		target_path = join_upload_path(upload_dir, relative);
		bool folder_allowed = false;
		std::string locked_path;
		if (!folder_lock_path_allows(upload_dir, parent_relative_path(relative),
			req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
			folder_allowed, locked_path, err))
		{
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!folder_allowed) {
			json_error(res, 403, "folder is locked", req.isKeepAlive());
			return true;
		}
		bool file_allowed = false;
		if (!file_lock_path_allows(upload_dir, remote_file_lock_key(relative),
			req.getParameter("file_password") ? req.getParameter("file_password") : "",
			file_allowed, err))
		{
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		if (!file_allowed) {
			json_error(res, 403, "file is locked", req.isKeepAlive());
			return true;
		}
	}

	if (!is_editable_image_name(display_name)) {
		json_error(res, 400, "only png/jpg/jpeg images can be edited", req.isKeepAlive());
		return true;
	}
	struct stat st;
	if (stat(target_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		json_error(res, 404, "image file not found", req.isKeepAlive());
		return true;
	}

	std::string tmp_path;
	if (!read_request_mime(req, upload_dir, *mime, tmp_path, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	const acl::http_mime_node* node = first_file_node(*mime);
	if (!copy_mime_body_to_file(tmp_path, node, target_path, err)) {
		::unlink(tmp_path.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	::unlink(tmp_path.c_str());

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("file", local ? target_path.c_str() : req.getParameter("file"));
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

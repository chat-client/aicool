#include "stdafx.h"
#include "file_common.h"

namespace action {

bool DownloadAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string file_path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("file"), file_path, err, false)) {
		return sendText(res, 400, "invalid file name\n", req.isKeepAlive());
	}
	if (is_protected_virtual_path(file_path)) {
		return sendText(res, 403, "system file is protected\n", req.isKeepAlive());
	}
	if (!resolve_upload_regular_file_path(upload_dir, file_path, file_path)) {
		return sendText(res, 404, "file not found\n", req.isKeepAlive());
	}
	bool lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, parent_relative_path(file_path),
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		lock_allowed, locked_path, err))
	{
		return sendText(res, 500, err.c_str(), req.isKeepAlive());
	}
	if (!lock_allowed) {
		return sendText(res, 403, "folder is locked\n", req.isKeepAlive());
	}
	bool file_lock_allowed = false;
	if (!file_lock_path_allows(upload_dir, remote_file_lock_key(file_path),
		req.getParameter("file_password") ? req.getParameter("file_password") : "",
		file_lock_allowed, err))
	{
		return sendText(res, 500, err.c_str(), req.isKeepAlive());
	}
	if (!file_lock_allowed) {
		return sendText(res, 403, "file is locked\n", req.isKeepAlive());
	}

	const std::string fullpath = join_upload_path(upload_dir, file_path);
	const std::string basename = base_name_from_relative_path(file_path);

	struct stat st{};
	if (stat(fullpath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		return sendText(res, 404, "file not found\n", req.isKeepAlive());
	}

	const long long fsize = regular_file_size(fullpath);
	if (fsize < 0) {
		return sendText(res, 500, "cannot read file size\n", req.isKeepAlive());
	}
	if (fsize == 0) {
		return sendText(res, 409, "file is empty, please re-upload\n", req.isKeepAlive());
	}

	FILE* in = fopen(fullpath.c_str(), "rb");
	if (in == nullptr) {
		return sendText(res, 403, "file cannot be read\n", req.isKeepAlive());
	}

	const bool is_image = is_image_file(basename.c_str());
	const bool is_video = is_video_file(basename.c_str());
	const bool is_audio = is_audio_file(basename.c_str());
	const bool is_text = is_text_file(basename.c_str());
	const bool is_pdf = is_pdf_file(basename.c_str());
	const char* preview = req.getParameter("preview");
	const bool inline_preview = (is_image || is_video || is_audio || is_text || is_pdf)
		&& preview != nullptr && strcmp(preview, "1") == 0;
	const char* range = req.getHeader("Range");
	long long range_begin = 0;
	long long range_end = 0;
	const bool has_range = parse_range_header(range, fsize, range_begin, range_end);
	const bool want_range = range != nullptr && *range != '\0';
	if (want_range && !has_range) {
		acl::string cr;
		cr.format("bytes */%lld", fsize);
		res.setStatus(416)
			.setKeepAlive(req.isKeepAlive())
			.setHeader("Content-Range", cr.c_str())
			.setHeader("Accept-Ranges", "bytes")
			.setContentType("text/plain; charset=utf-8");
		const char* msg = "invalid range\n";
		res.setContentLength(static_cast<long long>(strlen(msg)));
		fclose(in);
		return res.write(msg, strlen(msg)) && res.write(nullptr, 0);
	}

	acl::string dispo;
	if (inline_preview) {
		dispo.format("inline; filename=\"%s\"", basename.c_str());
	} else {
		dispo.format("attachment; filename=\"%s\"", basename.c_str());
	}

	const char* ctype = "application/octet-stream";
	if (is_image) {
		ctype = image_content_type(basename.c_str());
	} else if (is_video) {
		ctype = video_content_type(basename.c_str());
	} else if (is_audio) {
		ctype = audio_content_type(basename.c_str());
	} else if (is_text) {
		ctype = text_content_type(basename.c_str());
	} else if (is_pdf) {
		ctype = document_content_type(basename.c_str());
	}

	long long send_begin = has_range ? range_begin : 0;
	long long send_end = has_range ? range_end : (fsize - 1);
	long long send_size = send_end - send_begin + 1;
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
		logger_debug(DEBUG_FILE, 1, "content range: %s", content_range.c_str());

		res.setStatus(206)
			.setKeepAlive(req.isKeepAlive())
			.setContentType(ctype)
			.setHeader("Content-Disposition", dispo.c_str())
			.setHeader("Cache-Control", "no-store, no-cache, must-revalidate")
			.setHeader("Pragma", "no-cache")
			.setHeader("Expires", "0")
			.setHeader("Accept-Ranges", "bytes")
			.setHeader("Content-Range", content_range.c_str())
			.setContentLength(send_size);
	} else {
		res.setStatus(200)
			.setKeepAlive(req.isKeepAlive())
			.setContentType(ctype)
			.setHeader("Content-Disposition", dispo.c_str())
			.setHeader("Cache-Control", "no-store, no-cache, must-revalidate")
			.setHeader("Pragma", "no-cache")
			.setHeader("Expires", "0")
			.setHeader("Accept-Ranges", "bytes")
			.setContentLength(fsize);
	}

	size_t count = 0;
	char buf[8192];
	long long remain = send_size;
	while (remain > 0) {
		size_t want = sizeof(buf);
		if (static_cast<long long>(want) > remain) {
			want = static_cast<size_t>(remain);
		}
		const size_t n = fread(buf, 1, want, in);
		if (n == 0) {
			break;
		}
		if (!res.write(buf, (size_t) n)) {
			fclose(in);
			return false;
		}
		remain -= static_cast<long long>(n);

		if (++count % 100 == 0) {
			acl::fiber::yield();
		}
	}

	fclose(in);
	return res.write(nullptr, 0);
}

} // namespace action

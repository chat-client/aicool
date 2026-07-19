#include "stdafx.h"
#include "file_common.h"
#include "../convert/convert_common.h"

namespace action {

namespace {

bool is_office_file(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	return dot != NULL && (
		strcasecmp(dot, ".doc") == 0 || strcasecmp(dot, ".docx") == 0
		|| strcasecmp(dot, ".xls") == 0 || strcasecmp(dot, ".xlsx") == 0
		|| strcasecmp(dot, ".ppt") == 0 || strcasecmp(dot, ".pptx") == 0
		|| strcasecmp(dot, ".odt") == 0 || strcasecmp(dot, ".ods") == 0
		|| strcasecmp(dot, ".odp") == 0);
}

std::string file_stem(const std::string& name) {
	const size_t slash = name.find_last_of("/\\");
	const size_t start = slash == std::string::npos ? 0 : slash + 1;
	const size_t dot = name.find_last_of('.');
	if (dot == std::string::npos || dot < start) {
		return name.substr(start);
	}
	return name.substr(start, dot - start);
}

std::string cache_key_for_file(const std::string& rel_path, const struct stat& st) {
	unsigned long long hash = 1469598103934665603ULL;
	const std::string seed = rel_path + "|" + std::to_string((long long) st.st_size)
		+ "|" + std::to_string((long long) st.st_mtime);
	for (unsigned char ch : seed) {
		hash ^= ch;
		hash *= 1099511628211ULL;
	}
	char buf[32];
	snprintf(buf, sizeof(buf), "%016llx", hash);
	return buf;
}

bool normalize_local_office_path(const char* input, std::string& out,
	std::string& err)
{
	err.clear();
#ifdef _WIN32
	const bool has_input = input != NULL && *input != '\0';
	std::string text = has_input ? input : webcool_windows_home_path();
	if (text == "/") {
		text = "\\";
	}
	const bool absolute = (text.size() >= 3
			&& ((text[0] >= 'A' && text[0] <= 'Z')
				|| (text[0] >= 'a' && text[0] <= 'z'))
			&& text[1] == ':'
			&& (text[2] == '/' || text[2] == '\\'))
		|| (text.size() >= 2
			&& (text[0] == '/' || text[0] == '\\')
			&& (text[1] == '/' || text[1] == '\\'));
	if (has_input && !absolute && text != "\\" && text != "/") {
		err = "absolute path is required";
		return false;
	}
#else
	std::string text = input ? input : "";
	if (text.empty() || text[0] != '/') {
		err = "absolute path is required";
		return false;
	}
#endif

	char resolved[PATH_MAX];
	if (realpath(text.c_str(), resolved) == NULL) {
		err = strerror(errno);
		return false;
	}
	out = resolved;
	return true;
}

std::vector<std::string> office_converter_candidates() {
	std::vector<std::string> out;
	const char* configured = getenv("WEBCOOL_SOFFICE_PATH");
	if (configured != NULL && *configured != '\0') {
		out.push_back(configured);
	}
	out.push_back("soffice");
	out.push_back("libreoffice");
#ifdef _WIN32
	out.push_back("soffice.exe");
	const char* program_files = getenv("ProgramFiles");
	if (program_files != NULL && *program_files != '\0') {
		out.push_back(std::string(program_files)
			+ "\\LibreOffice\\program\\soffice.exe");
	}
	const char* program_files_x86 = getenv("ProgramFiles(x86)");
	if (program_files_x86 != NULL && *program_files_x86 != '\0') {
		out.push_back(std::string(program_files_x86)
			+ "\\LibreOffice\\program\\soffice.exe");
	}
#else
	out.push_back("/opt/homebrew/bin/soffice");
	out.push_back("/opt/homebrew/bin/libreoffice");
	out.push_back("/usr/local/bin/soffice");
	out.push_back("/usr/local/bin/libreoffice");
	out.push_back("/usr/bin/soffice");
	out.push_back("/usr/bin/libreoffice");
	out.push_back("/snap/bin/libreoffice");
	out.push_back("/Applications/LibreOffice.app/Contents/MacOS/soffice");
#endif
	return out;
}

bool convert_office_to_pdf(const std::string& source,
	const std::string& out_dir, const std::string& profile_dir,
	const std::string& output, std::string& err)
{
	std::string last_output;
	std::string attempted;
	for (const std::string& converter : office_converter_candidates()) {
		if (!attempted.empty()) {
			attempted += ", ";
		}
		attempted += converter;
		std::string cmd = shell_quote(converter)
			+ " --headless --nologo --nofirststartwizard --nodefault --nolockcheck"
			+ " " + shell_quote(std::string("-env:UserInstallation=file://") + profile_dir)
			+ " --convert-to pdf --outdir " + shell_quote(out_dir)
			+ " " + shell_quote(source) + " 2>&1";
		const int code = run_command_capture_in_thread(cmd, last_output);
		if (code == 0 && regular_file_size(output) > 0) {
			return true;
		}
		logger_warn("office preview convert failed, converter=%s, code=%d, output=%s",
			converter.c_str(), code, last_output.c_str());
	}
	err = "office preview convert failed; tried: " + attempted;
	if (!last_output.empty()) {
		err += "; last output: " + last_output;
	}
	return false;
}

bool send_pdf_file(request_t& req, response_t& res,
	const std::string& pdf_path, const std::string& filename)
{
	const long long fsize = regular_file_size(pdf_path);
	if (fsize <= 0) {
		return sendText(res, 500, "office preview pdf is empty\n", req.isKeepAlive());
	}
	FILE* in = fopen(pdf_path.c_str(), "rb");
	if (in == nullptr) {
		return sendText(res, 403, "office preview pdf cannot be read\n", req.isKeepAlive());
	}

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

	const long long send_begin = has_range ? range_begin : 0;
	const long long send_end = has_range ? range_end : (fsize - 1);
	const long long send_size = send_end - send_begin + 1;
	if (send_size < 0) {
		fclose(in);
		return sendText(res, 500, "invalid send size\n", req.isKeepAlive());
	}
	if (send_begin > 0 && !seek_file64(in, send_begin)) {
		fclose(in);
		return sendText(res, 500, "seek office preview pdf failed\n", req.isKeepAlive());
	}

	acl::string dispo;
	dispo.format("inline; filename=\"%s\"", filename.c_str());
	if (has_range) {
		acl::string content_range;
		content_range.format("bytes %lld-%lld/%lld", send_begin, send_end, fsize);
		res.setStatus(206)
			.setKeepAlive(req.isKeepAlive())
			.setContentType("application/pdf")
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
			.setContentType("application/pdf")
			.setHeader("Content-Disposition", dispo.c_str())
			.setHeader("Cache-Control", "no-store, no-cache, must-revalidate")
			.setHeader("Pragma", "no-cache")
			.setHeader("Expires", "0")
			.setHeader("Accept-Ranges", "bytes")
			.setContentLength(fsize);
	}

	char buf[8192];
	long long remain = send_size;
	size_t count = 0;
	while (remain > 0) {
		size_t want = sizeof(buf);
		if (static_cast<long long>(want) > remain) {
			want = static_cast<size_t>(remain);
		}
		const size_t n = fread(buf, 1, want, in);
		if (n == 0) {
			break;
		}
		if (!res.write(buf, n)) {
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

} // namespace

bool OfficePreviewAction::run(request_t& req, response_t& res,
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
	const std::string basename = base_name_from_relative_path(file_path);
	if (!is_office_file(basename.c_str())) {
		return sendText(res, 415, "unsupported office file\n", req.isKeepAlive());
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
	struct stat st{};
	if (stat(fullpath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		return sendText(res, 404, "file not found\n", req.isKeepAlive());
	}
	if (st.st_size <= 0) {
		return sendText(res, 409, "file is empty, please re-upload\n", req.isKeepAlive());
	}

	const std::string cache_root = join_upload_path(upload_dir, ".office_preview_cache");
	const std::string cache_dir = cache_root + "/" + cache_key_for_file(file_path, st);
	if (!make_dir_recursive(cache_dir.c_str())) {
		return sendText(res, 500, "cannot create office preview cache\n", req.isKeepAlive());
	}
	const std::string profile_dir = cache_dir + "/profile";
	if (!make_dir_recursive(profile_dir.c_str())) {
		return sendText(res, 500, "cannot create office preview profile\n", req.isKeepAlive());
	}
	const std::string pdf_name = file_stem(basename) + ".pdf";
	const std::string pdf_path = cache_dir + "/" + pdf_name;
	std::string convert_err;
	if (regular_file_size(pdf_path) <= 0
		&& !convert_office_to_pdf(fullpath, cache_dir, profile_dir, pdf_path, convert_err))
	{
		if (convert_err.empty()) {
			convert_err = "office preview convert failed, please install LibreOffice/soffice";
		}
		convert_err += "\n";
		return sendText(res, 500, convert_err.c_str(), req.isKeepAlive());
	}
	return send_pdf_file(req, res, pdf_path, pdf_name);
}

bool LocalDiskOfficePreviewAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string path;
	std::string err;
	if (!normalize_local_office_path(req.getParameter("path"), path, err)) {
		return sendText(res, 400, err.c_str(), req.isKeepAlive());
	}

	struct stat st{};
	if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		return sendText(res, 404, "file not found\n", req.isKeepAlive());
	}
	const std::string basename = local_base_name(path);
	if (!is_office_file(basename.c_str())) {
		return sendText(res, 415, "unsupported office file\n", req.isKeepAlive());
	}

	bool dir_allowed = false;
	std::string locked_dir;
	std::string dir_lock_err;
	if (!local_dir_lock_path_allows(upload_dir, local_parent_path(path),
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
	if (st.st_size <= 0) {
		return sendText(res, 409, "file is empty, please re-upload\n", req.isKeepAlive());
	}

	const std::string cache_root = join_upload_path(upload_dir, ".office_preview_cache");
	const std::string cache_dir = cache_root + "/local-" + cache_key_for_file(path, st);
	if (!make_dir_recursive(cache_dir.c_str())) {
		return sendText(res, 500, "cannot create office preview cache\n", req.isKeepAlive());
	}
	const std::string profile_dir = cache_dir + "/profile";
	if (!make_dir_recursive(profile_dir.c_str())) {
		return sendText(res, 500, "cannot create office preview profile\n", req.isKeepAlive());
	}
	const std::string pdf_name = file_stem(basename) + ".pdf";
	const std::string pdf_path = cache_dir + "/" + pdf_name;
	std::string convert_err;
	if (regular_file_size(pdf_path) <= 0
		&& !convert_office_to_pdf(path, cache_dir, profile_dir, pdf_path, convert_err))
	{
		if (convert_err.empty()) {
			convert_err = "office preview convert failed, please install LibreOffice/soffice";
		}
		convert_err += "\n";
		return sendText(res, 500, convert_err.c_str(), req.isKeepAlive());
	}
	return send_pdf_file(req, res, pdf_path, pdf_name);
}

} // namespace action

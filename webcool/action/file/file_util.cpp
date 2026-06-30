#include "stdafx.h"
#include "file_common.h"

namespace action {

bool seek_file64(FILE* fp, long long offset)
{
#ifdef _WIN32
	return _fseeki64(fp, offset, SEEK_SET) == 0;
#else
	return fseeko(fp, (off_t) offset, SEEK_SET) == 0;
#endif
}

std::string remote_file_lock_key(const std::string& path) {
	return std::string("remote:") + path;
}


void format_upload_time(time_t ts, char* buf, size_t size) {
	if (buf == NULL || size == 0) {
		return;
	}
	if (ts <= 0) {
		buf[0] = '\0';
		return;
	}
	struct tm tmv;
	acl_localtime_r(&ts, &tmv);
	if (strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tmv) == 0) {
		buf[0] = '\0';
	}
}

bool request_bool_param(request_t& req, const char* name) {
	const char* value = req.getParameter(name);
	return value != NULL && (
		strcmp(value, "1") == 0
		|| strcasecmp(value, "true") == 0
		|| strcasecmp(value, "yes") == 0
		|| strcasecmp(value, "on") == 0);
}

bool should_skip_entry(const char* name, bool show_hidden) {
	if (name == NULL || *name == '\0') {
		return true;
	}
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
		return true;
	}
	if (!show_hidden && name[0] == '.') {
		return true;
	}
	return false;
}

bool is_protected_project_db_file(const std::string& relative_dir,
	const char* name)
{
	if (!relative_dir.empty() || name == NULL) {
		return false;
	}
	return is_protected_virtual_path(name);
}

bool is_image_file(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	return dot != NULL && (
		strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".jpg") == 0
		|| strcasecmp(dot, ".jpeg") == 0 || strcasecmp(dot, ".gif") == 0
		|| strcasecmp(dot, ".heic") == 0 || strcasecmp(dot, ".heif") == 0);
}

bool is_video_file(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	return dot != NULL && (
		strcasecmp(dot, ".mp4") == 0 || strcasecmp(dot, ".avi") == 0
		|| strcasecmp(dot, ".mkv") == 0 || strcasecmp(dot, ".rm") == 0
		|| strcasecmp(dot, ".rmvb") == 0 || strcasecmp(dot, ".mov") == 0
		|| strcasecmp(dot, ".wmv") == 0 || strcasecmp(dot, ".mpg") == 0
		|| strcasecmp(dot, ".mpeg") == 0);
}

bool is_audio_file(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	return dot != NULL && (
		strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".m4a") == 0
		|| strcasecmp(dot, ".aac") == 0 || strcasecmp(dot, ".wav") == 0
		|| strcasecmp(dot, ".ogg") == 0 || strcasecmp(dot, ".flac") == 0);
}

bool is_text_file(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	return dot != NULL && (
		strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".md") == 0
		|| strcasecmp(dot, ".log") == 0 || strcasecmp(dot, ".csv") == 0
		|| strcasecmp(dot, ".json") == 0 || strcasecmp(dot, ".xml") == 0
		|| strcasecmp(dot, ".yaml") == 0 || strcasecmp(dot, ".yml") == 0
		|| strcasecmp(dot, ".ini") == 0 || strcasecmp(dot, ".conf") == 0
		|| strcasecmp(dot, ".c") == 0 || strcasecmp(dot, ".h") == 0
		|| strcasecmp(dot, ".cpp") == 0 || strcasecmp(dot, ".hpp") == 0
		|| strcasecmp(dot, ".cc") == 0 || strcasecmp(dot, ".java") == 0
		|| strcasecmp(dot, ".py") == 0 || strcasecmp(dot, ".js") == 0
		|| strcasecmp(dot, ".ts") == 0 || strcasecmp(dot, ".sh") == 0
		|| strcasecmp(dot, ".go") == 0 || strcasecmp(dot, ".sql") == 0
		|| strcasecmp(dot, ".proto") == 0);
}

bool is_pdf_file(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	return dot != NULL && strcasecmp(dot, ".pdf") == 0;
}

const char* image_content_type(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	if (dot == NULL) {
		return "application/octet-stream";
	}
	if (strcasecmp(dot, ".png") == 0) {
		return "image/png";
	}
	if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) {
		return "image/jpeg";
	}
	if (strcasecmp(dot, ".gif") == 0) {
		return "image/gif";
	}
	if (strcasecmp(dot, ".heic") == 0) {
		return "image/heic";
	}
	if (strcasecmp(dot, ".heif") == 0) {
		return "image/heif";
	}
	return "application/octet-stream";
}

const char* video_content_type(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	if (dot == NULL) {
		return "application/octet-stream";
	}
	if (strcasecmp(dot, ".mp4") == 0) {
		return "video/mp4";
	}
	if (strcasecmp(dot, ".avi") == 0) {
		return "video/x-msvideo";
	}
	if (strcasecmp(dot, ".mkv") == 0) {
		return "video/x-matroska";
	}
	if (strcasecmp(dot, ".rmvb") == 0) {
		return "application/vnd.rn-realmedia-vbr";
	}
	if (strcasecmp(dot, ".rm") == 0) {
		return "application/vnd.rn-realmedia";
	}
	if (strcasecmp(dot, ".mov") == 0) {
		return "video/quicktime";
	}
	if (strcasecmp(dot, ".wmv") == 0) {
		return "video/x-ms-wmv";
	}
	if (strcasecmp(dot, ".mpg") == 0 || strcasecmp(dot, ".mpeg") == 0) {
		return "video/mpeg";
	}
	return "application/octet-stream";
}

const char* audio_content_type(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	if (dot == NULL) {
		return "application/octet-stream";
	}
	if (strcasecmp(dot, ".mp3") == 0) {
		return "audio/mpeg";
	}
	if (strcasecmp(dot, ".m4a") == 0) {
		return "audio/mp4";
	}
	if (strcasecmp(dot, ".aac") == 0) {
		return "audio/aac";
	}
	if (strcasecmp(dot, ".wav") == 0) {
		return "audio/wav";
	}
	if (strcasecmp(dot, ".ogg") == 0) {
		return "audio/ogg";
	}
	if (strcasecmp(dot, ".flac") == 0) {
		return "audio/flac";
	}
	return "application/octet-stream";
}

const char* text_content_type(const char* filename) {
	const char* dot = filename ? strrchr(filename, '.') : NULL;
	if (dot == NULL) {
		return "text/plain; charset=utf-8";
	}
	if (strcasecmp(dot, ".json") == 0) {
		return "application/json; charset=utf-8";
	}
	if (strcasecmp(dot, ".xml") == 0) {
		return "application/xml; charset=utf-8";
	}
	if (strcasecmp(dot, ".csv") == 0) {
		return "text/csv; charset=utf-8";
	}
	return "text/plain; charset=utf-8";
}

const char* document_content_type(const char* filename) {
	return is_pdf_file(filename) ? "application/pdf" : "application/octet-stream";
}

bool parse_range_value(const char* s, long long& out) {
	if (s == NULL || *s == '\0') {
		return false;
	}
	errno = 0;
	char* end = NULL;
	long long v = strtoll(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0' || v < 0) {
		return false;
	}
	out = v;
	return true;
}

bool parse_range_header(const char* range, long long size,
	long long& begin, long long& end)
{
	if (range == NULL || size <= 0) {
		return false;
	}
	if (strncasecmp(range, "bytes=", 6) != 0) {
		return false;
	}
	const char* expr = range + 6;
	if (*expr == '\0' || strchr(expr, ',') != NULL) {
		return false;
	}
	const char* dash = strchr(expr, '-');
	if (dash == NULL) {
		return false;
	}
	if (dash == expr) {
		long long suffix = 0;
		if (!parse_range_value(dash + 1, suffix) || suffix <= 0) {
			return false;
		}
		if (suffix > size) {
			suffix = size;
		}
		begin = size - suffix;
		end = size - 1;
		return true;
	}

	acl::string left(expr, (size_t) (dash - expr));
	long long start = 0;
	if (!parse_range_value(left.c_str(), start) || start >= size) {
		return false;
	}
	if (*(dash + 1) == '\0') {
		begin = start;
		end = size - 1;
		return true;
	}

	long long stop = 0;
	if (!parse_range_value(dash + 1, stop) || stop < start) {
		return false;
	}
	if (stop >= size) {
		stop = size - 1;
	}
	begin = start;
	end = stop;
	return true;
}

} // namespace action

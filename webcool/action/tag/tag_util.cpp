#include "stdafx.h"
#include "tag_common.h"

namespace action {

static const char* g_local_tag_file_prefix = "local:";
static const char* g_tag_lock_prefix = "tag:";

bool is_local_tag_file_name(const std::string& file_name)
{
	return file_name.compare(0, strlen(g_local_tag_file_prefix),
		g_local_tag_file_prefix) == 0;
}

std::string local_tag_storage_name(const std::string& path)
{
	return std::string(g_local_tag_file_prefix) + path;
}

std::string local_tag_path_from_storage_name(const std::string& name)
{
	return is_local_tag_file_name(name)
		? name.substr(strlen(g_local_tag_file_prefix))
		: name;
}

std::string tag_lock_key(const std::string& tag_id)
{
	return std::string(g_tag_lock_prefix) + tag_id;
}

std::string tag_db_file_for_upload_dir(const std::string& upload_dir)
{
	acl::string path;
	path.format("%s/.tag_catalog.db", upload_dir.c_str());
	return std::string(path.c_str());
}

bool normalize_existing_local_file_path(const char* input,
	std::string& out, std::string& err)
{
	err.clear();
	if (input == NULL || *input == '\0') {
		err = "missing local file";
		return false;
	}
#ifdef _WIN32
	const size_t len = strlen(input);
	const bool absolute = (len >= 3
			&& ((input[0] >= 'A' && input[0] <= 'Z')
				|| (input[0] >= 'a' && input[0] <= 'z'))
			&& input[1] == ':'
			&& (input[2] == '/' || input[2] == '\\'))
		|| (len >= 2
			&& (input[0] == '/' || input[0] == '\\')
			&& (input[1] == '/' || input[1] == '\\'));
	if (!absolute) {
		err = "absolute path is required";
		return false;
	}
#else
	if (input[0] != '/') {
		err = "absolute path is required";
		return false;
	}
#endif
	char resolved[PATH_MAX];
	if (realpath(input, resolved) == NULL) {
		err = strerror(errno);
		return false;
	}
	struct stat st;
	if (stat(resolved, &st) != 0 || !S_ISREG(st.st_mode)) {
		err = "file not found";
		return false;
	}
	out = resolved;
	return true;
}

std::string tag_local_parent_path(const std::string& path)
{
	std::string::size_type pos = path.find_last_of("/\\");
	if (pos == std::string::npos || pos == 0) {
		return "/";
	}
	return path.substr(0, pos);
}
std::string trim_copy(const char* text) {
	const char* s = text ? text : "";
	while (*s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) {
		++s;
	}

	const char* e = s + strlen(s);
	while (e > s && (*(e - 1) == ' ' || *(e - 1) == '\t'
		|| *(e - 1) == '\r' || *(e - 1) == '\n'))
	{
		--e;
	}

	return std::string(s, (size_t) (e - s));
}

bool validate_tag_name(const std::string& name, std::string& err) {
	err.clear();
	if (name.empty()) {
		err = "tag name is empty";
		return false;
	}
	if (name.size() > 60) {
		err = "tag name is too long";
		return false;
	}
	for (size_t i = 0; i < name.size(); ++i) {
		unsigned char c = (unsigned char) name[i];
		if (c < 32 || c == 127) {
			err = "tag name contains control character";
			return false;
		}
	}
	return true;
}

bool validate_tag_id(const std::string& tag_id, std::string& err) {
	err.clear();
	if (tag_id.empty()) {
		err = "tag id is empty";
		return false;
	}
	if (tag_id.size() > 120) {
		err = "tag id is too long";
		return false;
	}
	for (size_t i = 0; i < tag_id.size(); ++i) {
		unsigned char c = (unsigned char) tag_id[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '_' || c == '-')
		{
			continue;
		}
		err = "tag id contains invalid character";
		return false;
	}
	return true;
}

bool ends_with_ignore_case(const std::string& text, const char* suffix) {
	if (suffix == NULL) {
		return false;
	}
	size_t suffix_len = strlen(suffix);
	if (text.size() < suffix_len) {
		return false;
	}
	const size_t start = text.size() - suffix_len;
	for (size_t i = 0; i < suffix_len; ++i) {
		unsigned char a = (unsigned char) text[start + i];
		unsigned char b = (unsigned char) suffix[i];
		if (a >= 'A' && a <= 'Z') {
			a = (unsigned char) (a - 'A' + 'a');
		}
		if (b >= 'A' && b <= 'Z') {
			b = (unsigned char) (b - 'A' + 'a');
		}
		if (a != b) {
			return false;
		}
	}
	return true;
}

bool is_video_file_name(const std::string& name) {
	static const char* kVideoSuffixes[] = {
		".mp4", ".avi", ".mkv", ".rm", ".rmvb", ".mov", ".wmv",
		".mpg", ".mpeg"
	};
	for (size_t i = 0; i < sizeof(kVideoSuffixes) / sizeof(kVideoSuffixes[0]); ++i) {
		if (ends_with_ignore_case(name, kVideoSuffixes[i])) {
			return true;
		}
	}
	return false;
}

bool is_audio_file_name(const std::string& name) {
	static const char* kAudioSuffixes[] = {
		".mp3", ".m4a", ".aac", ".wav", ".ogg", ".flac"
	};
	for (size_t i = 0; i < sizeof(kAudioSuffixes) / sizeof(kAudioSuffixes[0]); ++i) {
		if (ends_with_ignore_case(name, kAudioSuffixes[i])) {
			return true;
		}
	}
	return false;
}

bool is_image_file_name(const std::string& name) {
	static const char* kImageSuffixes[] = {
		".png", ".jpg", ".jpeg", ".gif", ".heic", ".heif"
	};
	for (size_t i = 0; i < sizeof(kImageSuffixes) / sizeof(kImageSuffixes[0]); ++i) {
		if (ends_with_ignore_case(name, kImageSuffixes[i])) {
			return true;
		}
	}
	return false;
}

bool is_document_file_name(const std::string& name) {
	static const char* kDocumentSuffixes[] = {
		".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx",
		".odt", ".ods", ".odp", ".rtf", ".txt", ".md", ".markdown",
		".mdown", ".mkdn", ".log", ".csv", ".tsv", ".json", ".xml",
		".yaml", ".yml", ".ini", ".conf", ".html", ".htm", ".css",
		".js", ".ts", ".c", ".h", ".cc", ".cpp", ".cxx", ".hpp",
		".java", ".py", ".go", ".sh", ".sql", ".proto"
	};
	for (size_t i = 0; i < sizeof(kDocumentSuffixes) / sizeof(kDocumentSuffixes[0]); ++i) {
		if (ends_with_ignore_case(name, kDocumentSuffixes[i])) {
			return true;
		}
	}
	return false;
}
bool file_exists_in_upload_dir(const std::string& upload_dir,
	const char* relative_path)
{
	std::string normalized;
	std::string err;
	if (!normalize_relative_path(relative_path, normalized, err, false)) {
		return false;
	}
	return upload_regular_file_exists(upload_dir, normalized);
}

} // namespace action

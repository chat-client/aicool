#include "stdafx.h"
#include "convert_common.h"

namespace action {

bool is_video_name(const char* filename) {
	if (filename == nullptr) {
		return false;
	}

	const char* dot = strrchr(filename, '.');
	if (dot == nullptr || *(dot + 1) == '\0') {
		return false;
	}

	return strcasecmp(dot, ".mp4") == 0
		|| strcasecmp(dot, ".mkv") == 0
		|| strcasecmp(dot, ".avi") == 0
		|| strcasecmp(dot, ".rm") == 0
		|| strcasecmp(dot, ".rmvb") == 0
		|| strcasecmp(dot, ".mov") == 0
		|| strcasecmp(dot, ".wmv") == 0
		|| strcasecmp(dot, ".mpg") == 0
		|| strcasecmp(dot, ".mpeg") == 0;
}

bool is_local_convertible_video_name(const char* filename) {
	if (filename == nullptr) {
		return false;
	}
	const char* dot = strrchr(filename, '.');
	if (dot == nullptr || *(dot + 1) == '\0') {
		return false;
	}
	return strcasecmp(dot, ".rmvb") == 0
		|| strcasecmp(dot, ".rm") == 0
		|| strcasecmp(dot, ".avi") == 0
		|| strcasecmp(dot, ".mov") == 0
		|| strcasecmp(dot, ".wmv") == 0
		|| strcasecmp(dot, ".mpg") == 0
		|| strcasecmp(dot, ".mpeg") == 0;
}

bool is_audio_split_candidate_video_name(const char* filename) {
	if (filename == nullptr) {
		return false;
	}
	const char* dot = strrchr(filename, '.');
	if (dot == nullptr || *(dot + 1) == '\0') {
		return false;
	}
	return strcasecmp(dot, ".mp4") == 0
		|| strcasecmp(dot, ".mkv") == 0;
}

long long file_size_of(const char* path) {
	if (path == nullptr || *path == '\0') {
		return -1;
	}
	return regular_file_size(path);
}

std::string local_parent_path(const std::string& path) {
	if (path.empty() || path == "/") {
		return "/";
	}
	std::string text = path;
	while (text.size() > 1 && text[text.size() - 1] == '/') {
		text.erase(text.size() - 1);
	}
	const std::string::size_type pos = text.rfind('/');
	if (pos == std::string::npos || pos == 0) {
		return "/";
	}
	return text.substr(0, pos);
}

std::string local_join_path(const std::string& parent, const char* name) {
	if (parent == "/") {
		return std::string("/") + name;
	}
	return parent + "/" + name;
}

std::string local_base_name(const std::string& path) {
	std::string text = path;
	while (text.size() > 1 && text[text.size() - 1] == '/') {
		text.erase(text.size() - 1);
	}
	std::string::size_type pos = text.rfind('/');
	if (pos == std::string::npos) {
		return text;
	}
	return text.substr(pos + 1);
}

std::string local_file_lock_key(const std::string& path) {
	return std::string("local:") + path;
}

static std::string remote_file_lock_key(const std::string& path) {
	return std::string("remote:") + path;
}

bool ensure_video_transcode_lock_policy(const std::string& upload_dir,
	const std::string& file_lock_key, std::string& err, int& status)
{
	status = 500;
	err.clear();
	bool locked = false;
	if (!file_lock_path_has_lock(upload_dir, file_lock_key, locked, err)) {
		return false;
	}
	if (locked) {
		status = 403;
		err = "locked video cannot be transcoded";
		return false;
	}
	status = 200;
	return true;
}

bool ensure_remote_video_transcode_lock_policy(const std::string& upload_dir,
	const std::string& file_path, std::string& err, int& status)
{
	bool folder_locked = false;
	const std::string parent = parent_relative_path(file_path);
	if (!folder_lock_path_has_lock(upload_dir, parent, folder_locked, err)) {
		status = 500;
		return false;
	}
	if (folder_locked) {
		status = 403;
		err = "locked folder video cannot be transcoded";
		return false;
	}
	return ensure_video_transcode_lock_policy(upload_dir,
		remote_file_lock_key(file_path), err, status);
}

bool ensure_local_video_transcode_lock_policy(const std::string& upload_dir,
	const std::string& local_path, std::string& err, int& status)
{
	bool dir_locked = false;
	const std::string parent = local_parent_path(local_path);
	if (!local_dir_lock_path_has_lock(upload_dir, parent, dir_locked, err)) {
		status = 500;
		return false;
	}
	if (dir_locked) {
		status = 403;
		err = "locked directory video cannot be transcoded";
		return false;
	}
	return ensure_video_transcode_lock_policy(upload_dir,
		local_file_lock_key(local_path), err, status);
}

bool normalize_local_video_path(const char* input, std::string& path,
	std::string& err)
{
	path.clear();
	err.clear();
	if (input == nullptr || *input == '\0') {
		err = "missing query parameter: path";
		return false;
	}
	/*
	 * Local-disk paths use native OS syntax.  The old check only accepted
	 * POSIX paths, which made every Windows drive/UNC path fail before ffmpeg
	 * was even invoked (the properties dialog consequently never opened).
	 */
#ifdef _WIN32
	const std::string text(input);
	const bool drive_path = text.size() >= 3
		&& ((text[0] >= 'A' && text[0] <= 'Z')
			|| (text[0] >= 'a' && text[0] <= 'z'))
		&& text[1] == ':'
		&& (text[2] == '/' || text[2] == '\\');
	const bool unc_path = text.size() >= 2
		&& (text[0] == '/' || text[0] == '\\')
		&& (text[1] == '/' || text[1] == '\\');
	if (!drive_path && !unc_path) {
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
	if (realpath(input, resolved) == nullptr) {
		err = strerror(errno);
		return false;
	}
	path = resolved;
	return true;
}

std::string local_stream_state_path(const std::string& local_path) {
	return local_parent_path(local_path) + "/." + local_base_name(local_path)
		+ ".meta";
}

std::string local_stream_tmp_mp4_path(const std::string& local_path) {
	const std::string base = local_base_name(local_path);
	std::string stem = base;
	const std::string::size_type dot = base.rfind('.');
	if (dot != std::string::npos) {
		stem = base.substr(0, dot);
	}
	return local_parent_path(local_path) + "/." + stem + ".mp4";
}

long long read_local_stream_position_ms(const std::string& local_path) {
	FILE* fp = fopen(local_stream_state_path(local_path).c_str(), "r");
	if (fp == nullptr) {
		return 0;
	}
	char buf[128];
	if (fgets(buf, sizeof(buf), fp) == nullptr) {
		fclose(fp);
		return 0;
	}
	fclose(fp);
	const long long value = safe_atoll(buf, -1);
	return value > 0 ? value : 0;
}

bool write_local_stream_position_ms(const std::string& local_path,
	long long position_ms, std::string& err)
{
	err.clear();
	FILE* fp = fopen(local_stream_state_path(local_path).c_str(), "w");
	if (fp == nullptr) {
		err = strerror(errno);
		return false;
	}
	fprintf(fp, "%lld\n", position_ms > 0 ? position_ms : 0);
	if (fclose(fp) != 0) {
		err = strerror(errno);
		return false;
	}
	return true;
}

void remove_local_stream_position(const std::string& local_path)
{
	unlink(local_stream_state_path(local_path).c_str());
}

} // namespace action

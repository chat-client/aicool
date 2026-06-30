#include "stdafx.h"
#include "convert_common.h"

namespace action {

bool is_active_stream_sidecar(const std::string& path)
{
	std::lock_guard<std::mutex> guard(g_transcode_mutex);
	return g_active_stream_sidecars.find(path) != g_active_stream_sidecars.end();
}

void register_stream_sidecar(const std::string& path)
{
	std::lock_guard<std::mutex> guard(g_transcode_mutex);
	g_active_stream_sidecars.insert(path);
}

void unregister_stream_sidecar(const std::string& path)
{
	std::lock_guard<std::mutex> guard(g_transcode_mutex);
	g_active_stream_sidecars.erase(path);
}

void cleanup_local_stream_sidecars(const std::string& parent)
{
	DIR* dir = opendir(parent.c_str());
	if (dir == nullptr) {
		return;
	}
	struct dirent* entry;
	while ((entry = readdir(dir)) != nullptr) {
		const char* name = entry->d_name;
		const bool stream_tmp = strncmp(name, ".streaming_tmp.", 15) == 0;
		const bool stream_progress = strncmp(name, ".streaming_progress.", 20) == 0;
		if (!stream_tmp && !stream_progress) {
			continue;
		}
		const std::string full = local_join_path(parent, name);
		if (is_active_stream_sidecar(full)) {
			continue;
		}
		struct stat st{};
		if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
			continue;
		}
		unlink(full.c_str());
	}
	closedir(dir);
}

void cleanup_current_stream_sidecars(const std::string& tmp_path,
	const std::string& progress_path, bool keep_files)
{
	if (!keep_files) {
		unlink(tmp_path.c_str());
		unlink(progress_path.c_str());
	}
	unregister_stream_sidecar(tmp_path);
	unregister_stream_sidecar(progress_path);
}

bool remux_mp4_faststart(const std::string& ffmpeg,
	const std::string& input_path, const std::string& output_path,
	std::string& err)
{
	err.clear();
	unlink(output_path.c_str());
	std::string cmd = shell_quote(ffmpeg)
		+ " -hide_banner -loglevel error -y -i " + shell_quote(input_path)
		+ " -map 0 -c copy -movflags +faststart " + shell_quote(output_path)
		+ " 2>&1";
	std::string out;
	const int code = run_command_capture(cmd, out);
	if (code != 0 || file_size_of(output_path.c_str()) <= 0) {
		err = trim_text(out);
		if (err.empty()) {
			err = "faststart remux failed";
		}
		unlink(output_path.c_str());
		return false;
	}
	return true;
}

void update_stream_task_progress_from_file(
	const std::shared_ptr<transcode_task_t>& task,
	const std::string& progress_path, long long duration_ms)
{
	if (!task || duration_ms <= 0) {
		return;
	}
	FILE* fp = fopen(progress_path.c_str(), "r");
	if (fp == nullptr) {
		return;
	}
	char line_buf[512];
	long long current_ms = -1;
	bool ended = false;
	while (fgets(line_buf, (int) sizeof(line_buf), fp) != nullptr) {
		std::string line = trim_text(line_buf);
		long long line_ms = parse_progress_ms_line(line);
		if (line_ms >= 0) {
			current_ms = line_ms;
		} else if (line == "progress=end") {
			ended = true;
		}
	}
	fclose(fp);
	if (ended) {
		update_task_progress(task, 99.5, "写入输出文件");
		return;
	}
	if (current_ms >= 0) {
		const double percent = static_cast<double>(current_ms) * 100.0 / static_cast<double>(duration_ms);
		update_task_progress(task, percent, "边转边看中");
	}
}

bool validate_local_stream_state_request(const request_t& req,
	const std::string& upload_dir, std::string& local_path,
	std::string& err, int& status)
{
	status = 400;
	if (!normalize_local_video_path(req.getParameter("path"), local_path, err)) {
		return false;
	}
	struct stat st{};
	if (stat(local_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		err = "source video not found";
		status = 404;
		return false;
	}
	if (!is_local_convertible_video_name(local_path.c_str())) {
		err = "local video must be rmvb, rm, avi, mov, wmv, mpg, or mpeg";
		status = 400;
		return false;
	}
	std::string lock_err;
	int lock_status = 500;
	if (!ensure_local_video_transcode_lock_policy(upload_dir, local_path, lock_err, lock_status)) {
		err = lock_err;
		status = lock_status;
		return false;
	}
	return true;
}

} // namespace action

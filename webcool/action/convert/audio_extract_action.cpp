#include "stdafx.h"
#include "convert_common.h"

namespace action {

namespace {

std::string make_audio_extract_name(const std::string& upload_dir,
	const std::string& input_name)
{
	const std::string stem = replace_ext(input_name, "");
	for (int i = 1; i < 10000; ++i) {
		const std::string suffix = i == 1 ? "" : ("_" + std::to_string(i));
		const std::string candidate = stem + "_audio" + suffix + ".mp3";
		if (!path_exists(join_upload_path(upload_dir, candidate))) {
			return candidate;
		}
	}
	return stem + "_audio_" + std::to_string(g_transcode_seq.load()) + ".mp3";
}

void run_audio_extract(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input_path,
	const std::string& temp_path, const std::string& output_path)
{
	unlink(temp_path.c_str());
	const long long duration_ms = probe_duration_ms(ffmpeg, input_path);
	ACL_ARGV* args = acl_argv_alloc(24);
	acl_argv_add(args,
		ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y",
		"-i", input_path.c_str(), "-map", "0:a:0", "-vn",
		"-c:a", "libmp3lame", "-ac", "2", "-b:a", "192k",
		"-progress", "pipe:1", "-nostats",
		temp_path.c_str(), nullptr);
	const ffmpeg_process_ptr process = start_ffmpeg_process(args);
	if (!process) {
		finish_task(task, false, "音频提取失败", acl::last_serror(), -1);
		return;
	}
	const int code = wait_transcode_progress(task, *process, duration_ms,
		2.0, 94.0, "正在提取音频", 98.0, "正在写入MP3文件");
	if (is_task_cancel_requested(task) || code != 0
		|| file_size_of(temp_path.c_str()) <= 0) {
		unlink(temp_path.c_str());
		finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "音频提取失败",
			is_task_cancel_requested(task) ? "cancelled" : "ffmpeg audio extraction failed", -1);
		return;
	}
	if (rename(temp_path.c_str(), output_path.c_str()) != 0) {
		unlink(temp_path.c_str());
		finish_task(task, false, "音频提取失败", "rename audio output failed", -1);
		return;
	}
	finish_task(task, true, "音频提取完成", "", file_size_of(output_path.c_str()));
}

} // namespace

bool AudioExtractAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	const char* file = req.getParameter("file");
	std::string relative_path;
	std::string err;
	if (!normalize_relative_path(file ? file : "", relative_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!resolve_upload_regular_file_path(upload_dir, relative_path, relative_path)
		|| !is_video_name(base_name_from_relative_path(relative_path).c_str())) {
		json_error(res, 404, "source video not found", req.isKeepAlive());
		return true;
	}
	int lock_status = 500;
	if (!ensure_remote_video_transcode_lock_policy(upload_dir, relative_path, err, lock_status)) {
		json_error(res, lock_status, err.c_str(), req.isKeepAlive());
		return true;
	}
	const std::string ffmpeg = choose_ffmpeg_path();
	if (ffmpeg.empty()) {
		json_error(res, 500, "ffmpeg not found", req.isKeepAlive());
		return true;
	}
	const std::string task_file = "audio-extract:" + relative_path;
	const std::string task_key = scoped_task_key(upload_dir, task_file);
	{
		std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
		const auto running = g_running_task_by_file.find(task_key);
		if (running != g_running_task_by_file.end()) {
			const auto task = g_transcode_tasks.find(running->second);
			if (task != g_transcode_tasks.end() && !task->second->done) {
				acl::json json;
				acl::json_node& root = json.create_node();
				root.add_bool("ok", true);
				root.add_bool("started", false);
				root.add_text("task_id", task->second->id.c_str());
				root.add_text("name", task->second->output_name.c_str());
				return sendJson(res, 200, root, req.isKeepAlive());
			}
		}
	}
	const std::string output_name = make_audio_extract_name(upload_dir, relative_path);
	const std::string output_path = join_upload_path(upload_dir, output_name);
	acl::string temp_path;
	temp_path.format("%s/.audio_extract_tmp.%u.%lu.mp3",
		local_parent_path(output_path).c_str(), static_cast<unsigned>(getpid()),
		static_cast<unsigned long>(g_transcode_seq.load()));
	auto task = std::make_shared<transcode_task_t>();
	task->id = make_task_id();
	task->scope = upload_dir;
	task->file_name = task_file;
	task->output_name = output_name;
	task->message = "等待提取音频";
	{
		std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
		g_transcode_tasks[task->id] = task;
		g_running_task_by_file[task_key] = task->id;
	}
	const std::string input_path = join_upload_path(upload_dir, relative_path);
	const std::string temporary_path = temp_path.c_str();
	go[task, ffmpeg, input_path, temporary_path, output_path] {
		acl::gofiber_wait_thread([task, ffmpeg, input_path, temporary_path, output_path] {
			run_audio_extract(task, ffmpeg, input_path, temporary_path, output_path);
		});
	};
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_bool("started", true);
	root.add_text("task_id", task->id.c_str());
	root.add_text("name", output_name.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AudioExtractAction::progress(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	transcode_task_snapshot_t task;
	if (!snapshot_task_by_id(req.getParameter("task_id"), upload_dir, task)
		|| task.file_name.compare(0, 14, "audio-extract:") != 0) {
		json_error(res, 404, "audio extraction task not found", req.isKeepAlive());
		return true;
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", task.id.c_str());
	root.add_text("name", task.output_name.c_str());
	root.add_bool("done", task.done);
	root.add_bool("success", task.success);
	root.add_bool("cancel_requested", task.cancel_requested);
	root.add_number("progress", static_cast<long long>(task.progress));
	root.add_text("message", task.message.c_str());
	if (!task.error.empty()) root.add_text("error", task.error.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AudioExtractAction::cancel(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	transcode_task_snapshot_t task;
	if (!snapshot_task_by_id(req.getParameter("task_id"), upload_dir, task)
		|| task.file_name.compare(0, 14, "audio-extract:") != 0) {
		json_error(res, 404, "audio extraction task not found", req.isKeepAlive());
		return true;
	}
	bool signal_sent = false;
	if (!request_cancel_task(task.id.c_str(), upload_dir, task, signal_sent)) {
		json_error(res, 404, "audio extraction task not found", req.isKeepAlive());
		return true;
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", task.id.c_str());
	root.add_bool("done", task.done);
	root.add_bool("cancel_requested", true);
	root.add_bool("signal_sent", signal_sent);
	root.add_text("message", task.done ? "task already finished" : "cancel requested");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

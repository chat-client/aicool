#include "stdafx.h"
#include "convert_common.h"

namespace action {
namespace {

int int_param(const request_t& req, const char* name) {
	const char* value = req.getParameter(name);
	return value ? atoi(value) : 0;
}

std::string unique_output(const std::string& source, int width, int height,
	int bitrate, int preview_seconds, bool local, const std::string& upload_dir)
{
	const std::string name = local ? local_base_name(source) : source;
	const std::string stem = replace_ext(name, "");
	const std::string tag = "_" + std::to_string(width) + "x" + std::to_string(height)
		+ "_" + std::to_string(bitrate) + "k"
		+ (preview_seconds > 0 ? "_preview" + std::to_string(preview_seconds) + "s" : "");
	for (int i = 1; i < 10000; ++i) {
		const std::string suffix = i == 1 ? "" : ("_" + std::to_string(i));
		const std::string filename = stem + tag + suffix + ".mp4";
		const std::string candidate = local
			? local_join_path(local_parent_path(source), filename.c_str()) : filename;
		if (!path_exists(local ? candidate : join_upload_path(upload_dir, candidate))) return candidate;
	}
	return stem + tag + "_" + std::to_string(g_transcode_seq.load()) + ".mp4";
}

void run_enhance_in_thread(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input, const std::string& temp,
	const std::string& output, int width, int height, int bitrate,
	int denoise, bool deinterlace, int sharpen, int target_fps, int preview_seconds)
{
	unlink(temp.c_str());
	const long long duration = probe_duration_ms_in_thread(ffmpeg, input);
	const long long preview_duration = static_cast<long long>(preview_seconds) * 1000LL;
	const long long effective_duration = preview_seconds > 0
		? (duration > 0 ? std::min(duration, preview_duration) : preview_duration) : duration;
	std::string filter;
	if (deinterlace) filter += "yadif,";
	if (denoise == 1) filter += "hqdn3d=0.8:0.8:3:3,";
	if (denoise == 2) filter += "hqdn3d=1.2:1.2:4:4,";
	filter += "scale=" + std::to_string(width) + ":" + std::to_string(height)
		+ ":force_original_aspect_ratio=decrease:flags=lanczos,pad="
		+ std::to_string(width) + ":" + std::to_string(height)
		+ ":(ow-iw)/2:(oh-ih)/2";
	if (sharpen > 0) filter += ",unsharp=5:5:" + std::to_string(sharpen / 100.0);
	if (target_fps > 0) filter += ",minterpolate=fps=" + std::to_string(target_fps)
		+ ":mi_mode=mci:mc_mode=aobmc:me_mode=bidir";
	const std::string rate = std::to_string(bitrate) + "k";
	const std::string maxrate = std::to_string(bitrate * 3 / 2) + "k";
	const std::string bufsize = std::to_string(bitrate * 2) + "k";
	ACL_ARGV* args = acl_argv_alloc(40);
	acl_argv_add(args, ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y",
		"-i", input.c_str(), nullptr);
	const std::string preview_text = std::to_string(preview_seconds);
	if (preview_seconds > 0) acl_argv_add(args, "-t", preview_text.c_str(), nullptr);
	acl_argv_add(args, "-vf", filter.c_str(), "-c:v", "libx264",
		"-preset", "slow", "-b:v", rate.c_str(), "-maxrate", maxrate.c_str(),
		"-bufsize", bufsize.c_str(), "-pix_fmt", "yuv420p", "-c:a", "aac",
		"-b:a", "192k", "-movflags", "+faststart", "-progress", "pipe:1",
		"-nostats", temp.c_str(), nullptr);
	const ffmpeg_process_ptr process = start_ffmpeg_process_in_thread(args);
	if (!process) { finish_task(task, false, "画质提升失败", acl::last_serror(), -1); return; }
	const int code = wait_transcode_progress_in_thread(task, *process, effective_duration, 2, 95,
		"正在提升画质", 98, "正在写入MP4文件");
	if (code != 0 || is_task_cancel_requested(task) || file_size_of(temp.c_str()) <= 0) {
		unlink(temp.c_str());
		finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "画质提升失败",
			is_task_cancel_requested(task) ? "cancelled" : "ffmpeg enhance failed", -1);
		return;
	}
	if (rename(temp.c_str(), output.c_str()) != 0) {
		unlink(temp.c_str()); finish_task(task, false, "画质提升失败", "rename failed", -1); return;
	}
	finish_task(task, true, "画质提升完成", "", file_size_of(output.c_str()));
}

bool task_snapshot(const request_t& req, const std::string& scope, bool local,
	transcode_task_snapshot_t& task)
{
	const char* prefix = local ? "video-enhance-local:" : "video-enhance:";
	const size_t length = strlen(prefix);
	return snapshot_task_by_id(req.getParameter("task_id"), scope, task)
		&& task.file_name.compare(0, length, prefix) == 0;
}
}

bool VideoEnhanceAction::run(request_t& req, response_t& res,
	const std::string& upload_dir, bool local)
{
	const int width = int_param(req, "width");
	const int height = int_param(req, "height");
	const int bitrate = int_param(req, "bitrate_kbps");
	const int denoise = int_param(req, "denoise");
	const int sharpen = int_param(req, "sharpen");
	const int target_fps = int_param(req, "target_fps");
	const int preview_seconds = int_param(req, "preview_seconds");
	const bool deinterlace = int_param(req, "deinterlace") == 1;
	if (width < 320 || width > 3840 || height < 240 || height > 2160
		|| width % 2 || height % 2 || bitrate < 300 || bitrate > 50000
		|| denoise < 0 || denoise > 2 || sharpen < 0 || sharpen > 100
		|| (preview_seconds != 0 && preview_seconds != 10 && preview_seconds != 30 && preview_seconds != 60)
		|| (target_fps != 0 && target_fps != 30 && target_fps != 50 && target_fps != 60)) {
		json_error(res, 400, "invalid width, height, or bitrate", req.isKeepAlive()); return true;
	}
	std::string source;
	std::string err;
	if (local) {
		if (!normalize_local_video_path(req.getParameter("path"), source, err)) {
			json_error(res, 400, err.c_str(), req.isKeepAlive()); return true;
		}
		struct stat st{};
		if (stat(source.c_str(), &st) != 0 || !S_ISREG(st.st_mode)
			|| !is_video_name(local_base_name(source).c_str())) {
			json_error(res, 404, "source video not found", req.isKeepAlive()); return true;
		}
		int status = 500;
		if (!ensure_local_video_transcode_lock_policy(upload_dir, source, err, status)) {
			json_error(res, status, err.c_str(), req.isKeepAlive()); return true;
		}
	} else {
		if (!normalize_relative_path(req.getParameter("file") ? req.getParameter("file") : "", source, err, false)
			|| !resolve_upload_regular_file_path(upload_dir, source, source)
			|| !is_video_name(base_name_from_relative_path(source).c_str())) {
			json_error(res, 404, "source video not found", req.isKeepAlive()); return true;
		}
		int status = 500;
		if (!ensure_remote_video_transcode_lock_policy(upload_dir, source, err, status)) {
			json_error(res, status, err.c_str(), req.isKeepAlive()); return true;
		}
	}
	const std::string ffmpeg = choose_ffmpeg_path();
	if (ffmpeg.empty()) { json_error(res, 500, "ffmpeg not found", req.isKeepAlive()); return true; }
	const std::string prefix = local ? "video-enhance-local:" : "video-enhance:";
	const std::string task_file = prefix + source;
	const std::string key = scoped_task_key(upload_dir, task_file);
	{
		std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
		const auto running = g_running_task_by_file.find(key);
		if (running != g_running_task_by_file.end()) {
			const auto found = g_transcode_tasks.find(running->second);
			if (found != g_transcode_tasks.end() && !found->second->done) {
				acl::json json; acl::json_node& root = json.create_node(); root.add_bool("ok", true);
				root.add_text("task_id", found->second->id.c_str()); root.add_text("name", found->second->output_name.c_str());
				return sendJson(res, 200, root, req.isKeepAlive());
			}
		}
	}
	const std::string output_name = unique_output(source, width, height, bitrate, preview_seconds, local, upload_dir);
	const std::string input_path = local ? source : join_upload_path(upload_dir, source);
	const std::string output_path = local ? output_name : join_upload_path(upload_dir, output_name);
	acl::string tmp; tmp.format("%s/.video_enhance_tmp.%u.%lu.mp4", local_parent_path(output_path).c_str(),
		static_cast<unsigned>(getpid()), static_cast<unsigned long>(g_transcode_seq.load()));
	auto task = std::make_shared<transcode_task_t>(); task->id = make_task_id(); task->scope = upload_dir;
	task->file_name = task_file; task->output_name = output_name; task->message = "等待提升画质"; task->local = local;
	{ std::lock_guard<webcool::mutex> guard(g_transcode_mutex); g_transcode_tasks[task->id] = task; g_running_task_by_file[key] = task->id; }
	const std::string temp_path = tmp.c_str();
	acl::gofiber([task, ffmpeg, input_path, temp_path, output_path, width, height, bitrate, denoise, deinterlace, sharpen, target_fps, preview_seconds] {
		run_enhance_in_thread(task, ffmpeg, input_path, temp_path, output_path, width, height, bitrate,
			denoise, deinterlace, sharpen, target_fps, preview_seconds);
	});
	acl::json json; acl::json_node& root = json.create_node(); root.add_bool("ok", true);
	root.add_text("task_id", task->id.c_str()); root.add_text("name", output_name.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool VideoEnhanceAction::progress(request_t& req, response_t& res, const std::string& scope, bool local) {
	transcode_task_snapshot_t task; if (!task_snapshot(req, scope, local, task)) { json_error(res, 404, "enhance task not found", req.isKeepAlive()); return true; }
	acl::json json; acl::json_node& root = json.create_node(); root.add_bool("ok", true); root.add_text("name", task.output_name.c_str());
	root.add_bool("done", task.done); root.add_bool("success", task.success); root.add_bool("cancel_requested", task.cancel_requested);
	root.add_number("progress", static_cast<long long>(task.progress)); root.add_text("message", task.message.c_str()); if (!task.error.empty()) root.add_text("error", task.error.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool VideoEnhanceAction::cancel(request_t& req, response_t& res, const std::string& scope, bool local) {
	transcode_task_snapshot_t task; if (!task_snapshot(req, scope, local, task)) { json_error(res, 404, "enhance task not found", req.isKeepAlive()); return true; }
	bool sent = false; request_cancel_task(task.id.c_str(), scope, task, sent); acl::json json; acl::json_node& root = json.create_node();
	root.add_bool("ok", true); root.add_bool("cancel_requested", true); root.add_bool("signal_sent", sent); return sendJson(res, 200, root, req.isKeepAlive());
}
} // namespace action

#include "stdafx.h"
#include "convert_common.h"
#include <chrono>
#include <thread>

namespace action {
namespace {

std::string choose_realesrgan(std::string& models) {
	const char* env = getenv("AICOOL_REALESRGAN");
	const char* env_models = getenv("AICOOL_REALESRGAN_MODELS");
	std::vector<std::string> bins;
#ifdef _WIN32
	bins = {"realesrgan-ncnn-vulkan.exe", "tools\\windows\\realesrgan-ncnn-vulkan.exe", "..\\tools\\windows\\realesrgan-ncnn-vulkan.exe"};
#elif defined(__APPLE__)
	bins = {"/opt/soft/webcool/bin/realesrgan-ncnn-vulkan", "tools/mac/realesrgan-ncnn-vulkan", "../tools/mac/realesrgan-ncnn-vulkan"};
#else
	bins = {"/opt/soft/webcool/bin/realesrgan-ncnn-vulkan", "tools/linux/realesrgan-ncnn-vulkan", "../tools/linux/realesrgan-ncnn-vulkan"};
#endif
	std::string bin = env && *env ? env : "";
	if (bin.empty()) for (const auto& item : bins) if (access(item.c_str(), X_OK) == 0) { bin = item; break; }
	if (env_models && *env_models) models = env_models;
	else {
		std::vector<std::string> dirs = {"/opt/soft/webcool/models/realesrgan", "tools/mac/realesrgan-models", "../tools/mac/realesrgan-models", "tools/linux/realesrgan-models", "../tools/linux/realesrgan-models"};
#ifdef _WIN32
		dirs = {"models\\realesrgan", "tools\\windows\\realesrgan-models", "..\\tools\\windows\\realesrgan-models"};
#endif
		for (const auto& dir : dirs) { struct stat st{}; if (stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { models = dir; break; } }
	}
	return bin;
}

void cleanup_temp(const std::string& path) {
	std::string output;
#ifdef _WIN32
	run_command_capture("rmdir /S /Q " + shell_quote(path), output);
#else
	run_command_capture("rm -rf " + shell_quote(path), output);
#endif
}

long long count_directory_files(const std::string& path) {
	long long count = 0;
#ifdef _WIN32
	std::wstring wide;
	if (!webcool_utf8_to_wide((path + "\\*").c_str(), wide)) return 0;
	WIN32_FIND_DATAW data;
	HANDLE handle = FindFirstFileW(wide.c_str(), &data);
	if (handle == INVALID_HANDLE_VALUE) return 0;
	do { if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++count; } while (FindNextFileW(handle, &data));
	FindClose(handle);
#else
	DIR* dir = opendir(path.c_str());
	if (!dir) return 0;
	struct dirent* entry;
	while ((entry = readdir(dir)) != nullptr) if (entry->d_name[0] != '.') ++count;
	closedir(dir);
#endif
	return count;
}

std::string ai_output_name(const std::string& source, bool local,
	const std::string& upload_dir, const std::string& model, int width, int height)
{
	const std::string name = local ? local_base_name(source) : source;
	const std::string stem = replace_ext(name, "");
	const std::string tag = "_ai_" + model + "_" + std::to_string(width) + "x" + std::to_string(height);
	for (int i = 1; i < 10000; ++i) {
		const std::string suffix = i == 1 ? "" : "_" + std::to_string(i);
		const std::string item = stem + tag + suffix + ".mp4";
		const std::string path = local ? local_join_path(local_parent_path(source), item.c_str()) : join_upload_path(upload_dir, item);
		if (!path_exists(path)) return local ? path : item;
	}
	return stem + tag + "_" + std::to_string(g_transcode_seq.load()) + ".mp4";
}

bool run_stage(const std::shared_ptr<transcode_task_t>& task, ACL_ARGV* args,
	long long duration, double start, double span, const char* message,
	double end, const char* end_message)
{
	const ffmpeg_process_ptr process = start_ffmpeg_process(args);
	if (!process) return false;
	return wait_transcode_progress(task, *process, duration, start, span, message, end, end_message) == 0
		&& !is_task_cancel_requested(task);
}

void run_ai_task(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& ai, const std::string& models,
	const std::string& input, const std::string& output, const std::string& temp_root,
	const std::string& model, int scale, int width, int height, int bitrate, double fps)
{
	const std::string frames_in = temp_root + "/input";
	const std::string frames_out = temp_root + "/output";
	if (!make_dir_recursive(frames_in.c_str()) || !make_dir_recursive(frames_out.c_str())) {
		finish_task(task, false, "AI增强失败", "cannot create temporary directories", -1); return;
	}
	const long long duration = probe_duration_ms(ffmpeg, input);
	const std::string input_pattern = frames_in + "/%08d.png";
	ACL_ARGV* extract = acl_argv_alloc(20);
	acl_argv_add(extract, ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y", "-i", input.c_str(),
		"-map", "0:v:0", "-vsync", "0", "-progress", "pipe:1", "-nostats", input_pattern.c_str(), nullptr);
	if (!run_stage(task, extract, duration, 1, 10, "正在解码视频帧", 12, "准备AI推理")) {
		cleanup_temp(temp_root); finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "AI增强失败", "frame extraction failed", -1); return;
	}
	ACL_ARGV* inference = acl_argv_alloc(24);
	const std::string scale_text = std::to_string(scale);
	acl_argv_add(inference, ai.c_str(), "-i", frames_in.c_str(), "-o", frames_out.c_str(), "-n", model.c_str(),
		"-s", scale_text.c_str(), "-m", models.c_str(), "-t", "0", "-f", "png", nullptr);
	const long long total_frames = count_directory_files(frames_in);
	std::atomic<bool> inference_done(false);
	std::thread monitor([task, frames_out, total_frames, &inference_done] {
		while (!inference_done.load()) {
			if (total_frames > 0) {
				const long long completed = count_directory_files(frames_out);
				update_task_progress(task, 12.0 + std::min(72.0,
					72.0 * static_cast<double>(completed) / static_cast<double>(total_frames)),
					"AI正在恢复纹理与细节");
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
	});
	const bool inference_ok = run_stage(task, inference, 0, 12, 72,
		"AI正在恢复纹理与细节", 85, "AI推理完成");
	inference_done.store(true);
	monitor.join();
	if (!inference_ok) {
		cleanup_temp(temp_root); finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "AI增强失败", "Real-ESRGAN inference failed", -1); return;
	}
	const std::string fps_text = std::to_string(fps > 0 ? fps : 25.0);
	const std::string output_pattern = frames_out + "/%08d.png";
	const std::string filter = "scale=" + std::to_string(width) + ":" + std::to_string(height)
		+ ":force_original_aspect_ratio=decrease:flags=lanczos,pad=" + std::to_string(width) + ":"
		+ std::to_string(height) + ":(ow-iw)/2:(oh-ih)/2";
	const std::string rate = std::to_string(bitrate) + "k";
	ACL_ARGV* encode = acl_argv_alloc(40);
	acl_argv_add(encode, ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y", "-framerate", fps_text.c_str(),
		"-i", output_pattern.c_str(), "-i", input.c_str(), "-map", "0:v:0", "-map", "1:a?", "-vf", filter.c_str(),
		"-c:v", "libx264", "-preset", "slow", "-b:v", rate.c_str(), "-pix_fmt", "yuv420p", "-c:a", "aac",
		"-b:a", "192k", "-shortest", "-movflags", "+faststart", "-progress", "pipe:1", "-nostats", output.c_str(), nullptr);
	if (!run_stage(task, encode, duration, 85, 13, "正在合成视频与原音轨", 99, "正在完成AI视频")) {
		unlink(output.c_str()); cleanup_temp(temp_root); finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "AI增强失败", "video encoding failed", -1); return;
	}
	cleanup_temp(temp_root);
	finish_task(task, true, "AI超分辨率处理完成", "", file_size_of(output.c_str()));
}

bool ai_snapshot(const request_t& req, const std::string& scope, bool local, transcode_task_snapshot_t& task) {
	const std::string prefix = local ? "ai-enhance-local:" : "ai-enhance:";
	return snapshot_task_by_id(req.getParameter("task_id"), scope, task) && task.file_name.compare(0, prefix.size(), prefix) == 0;
}
}

bool AiVideoEnhanceAction::run(request_t& req, response_t& res, const std::string& upload_dir, bool local) {
	const int width = safe_atoi(req.getParameter("width"), 0), height = safe_atoi(req.getParameter("height"), 0);
	const int bitrate = safe_atoi(req.getParameter("bitrate_kbps"), 0), scale = safe_atoi(req.getParameter("scale"), 2);
	const double fps = req.getParameter("fps") ? atof(req.getParameter("fps")) : 25.0;
	const std::string model = req.getParameter("model") ? req.getParameter("model") : "realesrgan-x4plus";
	if (width < 320 || width > 3840 || height < 240 || height > 2160 || width % 2 || height % 2
		|| bitrate < 300 || bitrate > 50000 || (scale != 2 && scale != 4) || fps <= 0 || fps > 120
		|| (model != "realesrgan-x4plus" && model != "realesr-animevideov3")) {
		json_error(res, 400, "invalid AI enhancement options", req.isKeepAlive()); return true;
	}
	std::string source, err;
	if (local) {
		if (!normalize_local_video_path(req.getParameter("path"), source, err)) { json_error(res, 400, err.c_str(), req.isKeepAlive()); return true; }
		struct stat st{}; if (stat(source.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || !is_video_name(local_base_name(source).c_str())) { json_error(res, 404, "source video not found", req.isKeepAlive()); return true; }
		int status = 500; if (!ensure_local_video_transcode_lock_policy(upload_dir, source, err, status)) { json_error(res, status, err.c_str(), req.isKeepAlive()); return true; }
	} else if (!normalize_relative_path(req.getParameter("file") ? req.getParameter("file") : "", source, err, false)
		|| !resolve_upload_regular_file_path(upload_dir, source, source) || !is_video_name(base_name_from_relative_path(source).c_str())) {
		json_error(res, 404, "source video not found", req.isKeepAlive()); return true;
	} else { int status = 500; if (!ensure_remote_video_transcode_lock_policy(upload_dir, source, err, status)) { json_error(res, status, err.c_str(), req.isKeepAlive()); return true; } }
	const std::string ffmpeg = choose_ffmpeg_path(); std::string models; const std::string ai = choose_realesrgan(models);
	if (ffmpeg.empty() || ai.empty() || models.empty()) { json_error(res, 503, "Real-ESRGAN runtime or models not installed", req.isKeepAlive()); return true; }
	const std::string input = local ? source : join_upload_path(upload_dir, source);
	const std::string output_name = ai_output_name(source, local, upload_dir, model == "realesr-animevideov3" ? "anime" : "general", width, height);
	const std::string output = local ? output_name : join_upload_path(upload_dir, output_name);
	const std::string task_file = (local ? "ai-enhance-local:" : "ai-enhance:") + source;
	auto task = std::make_shared<transcode_task_t>(); task->id = make_task_id(); task->scope = upload_dir; task->file_name = task_file;
	task->output_name = output_name; task->local = local; task->message = "等待AI超分辨率处理";
	const std::string key = scoped_task_key(upload_dir, task_file);
	{ std::lock_guard<webcool::mutex> guard(g_transcode_mutex); const auto it = g_running_task_by_file.find(key); if (it != g_running_task_by_file.end()) { const auto old = g_transcode_tasks.find(it->second); if (old != g_transcode_tasks.end() && !old->second->done) { acl::json json; acl::json_node& root=json.create_node(); root.add_bool("ok",true); root.add_text("task_id",old->second->id.c_str()); root.add_text("name",old->second->output_name.c_str()); return sendJson(res,200,root,req.isKeepAlive()); } } g_transcode_tasks[task->id]=task; g_running_task_by_file[key]=task->id; }
	const std::string temp_root = local_parent_path(output) + "/.ai_enhance_tmp." + task->id;
	go[task, ffmpeg, ai, models, input, output, temp_root, model, scale, width, height, bitrate, fps] { acl::gofiber_wait_thread([task, ffmpeg, ai, models, input, output, temp_root, model, scale, width, height, bitrate, fps] { run_ai_task(task, ffmpeg, ai, models, input, output, temp_root, model, scale, width, height, bitrate, fps); }); };
	acl::json json; acl::json_node& root=json.create_node(); root.add_bool("ok",true); root.add_text("task_id",task->id.c_str()); root.add_text("name",output_name.c_str()); return sendJson(res,200,root,req.isKeepAlive());
}

bool AiVideoEnhanceAction::progress(request_t& req, response_t& res, const std::string& scope, bool local) { transcode_task_snapshot_t t; if (!ai_snapshot(req,scope,local,t)) { json_error(res,404,"AI task not found",req.isKeepAlive()); return true; } acl::json j; acl::json_node& r=j.create_node(); r.add_bool("ok",true); r.add_text("name",t.output_name.c_str()); r.add_bool("done",t.done); r.add_bool("success",t.success); r.add_bool("cancel_requested",t.cancel_requested); r.add_number("progress",(long long)t.progress); r.add_text("message",t.message.c_str()); if(!t.error.empty())r.add_text("error",t.error.c_str()); return sendJson(res,200,r,req.isKeepAlive()); }
bool AiVideoEnhanceAction::cancel(request_t& req, response_t& res, const std::string& scope, bool local) { transcode_task_snapshot_t t; if(!ai_snapshot(req,scope,local,t)){json_error(res,404,"AI task not found",req.isKeepAlive());return true;} bool sent=false;request_cancel_task(t.id.c_str(),scope,t,sent);acl::json j;acl::json_node&r=j.create_node();r.add_bool("ok",true);r.add_bool("cancel_requested",true);r.add_bool("signal_sent",sent);return sendJson(res,200,r,req.isKeepAlive()); }
} // namespace action

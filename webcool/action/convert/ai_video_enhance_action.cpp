#include "stdafx.h"
#include "convert_common.h"
#include <chrono>
#include <thread>

namespace action {
namespace {

struct coreml_runtime_t {
	std::string executable;
	std::string model;
};

coreml_runtime_t choose_coreml_runtime(const std::string& model_name) {
	coreml_runtime_t runtime;
	(void) model_name;
#if defined(__APPLE__) && defined(__aarch64__)
	const char* env_bin = getenv("AICOOL_COREML_REALESRGAN");
	const char* env_model = getenv("AICOOL_COREML_REALESRGAN_MODEL");
	const std::vector<std::string> bins = {
		"/opt/soft/webcool/bin/coreml-realesrgan", "tools/mac/coreml-realesrgan", "../tools/mac/coreml-realesrgan"
	};
	std::string file_name = "realesrgan512.mlmodelc";
	if (model_name == "coreml-x2plus") file_name = "realesrgan-x2plus.mlmodelc";
	else if (model_name == "coreml-general-x4v3") file_name = "realesr-general-x4v3.mlmodelc";
	else if (model_name == "coreml-general-x4v3-w8a8") file_name = "realesr-general-x4v3-w8a8.mlmodelc";
	else if (model_name == "coreml-x4plus-int8") file_name = "realesrgan-x4plus-int8.mlmodelc";
	const std::vector<std::string> models = {
		"/opt/soft/webcool/models/coreml/" + file_name,
		"tools/mac/coreml-models/" + file_name, "../tools/mac/coreml-models/" + file_name,
		"tools/mac/" + file_name, "../tools/mac/" + file_name
	};
	runtime.executable = env_bin && *env_bin ? env_bin : "";
	if (runtime.executable.empty()) {
		for (const auto& item : bins) if (access(item.c_str(), X_OK) == 0) { runtime.executable = item; break; }
	}
	runtime.model = env_model && *env_model ? env_model : "";
	if (runtime.model.empty()) {
		for (const auto& item : models) {
			struct stat st{};
			if (stat(item.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { runtime.model = item; break; }
		}
	}
#endif
	return runtime;
}

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
	bool use_coreml, const std::string& coreml_model, int coreml_workers,
	const std::string& compute_units,
	const std::string& input_sizing,
	int tile_batch, const std::string& overlap_mode, int temporal_step,
	const std::string& input, const std::string& output, const std::string& temp_root,
	const std::string& model, int scale, int width, int height, int bitrate, double fps,
	int tile, const std::string& threads, int gpu, const std::string& encode_preset,
	int preview_seconds)
{
	const std::string frames_in = temp_root + "/input";
	const std::string frames_out = temp_root + "/output";
	if (!make_dir_recursive(frames_in.c_str()) || !make_dir_recursive(frames_out.c_str())) {
		finish_task(task, false, "AI增强失败", "cannot create temporary directories", -1); return;
	}
	const long long duration = probe_duration_ms(ffmpeg, input);
	if (use_coreml) {
		const std::string silent_video = temp_root + "/coreml-video.mp4";
		const std::string workers_text = std::to_string(coreml_workers);
		const std::string width_text = std::to_string(width);
		const std::string height_text = std::to_string(height);
		const std::string bitrate_text = std::to_string(static_cast<long long>(bitrate) * 1000LL);
		const std::string preview_text = std::to_string(preview_seconds);
		const std::string tile_batch_text = std::to_string(tile_batch);
		const std::string temporal_step_text = std::to_string(temporal_step);
		ACL_ARGV* pipeline = acl_argv_alloc(32);
		acl_argv_add(pipeline, ai.c_str(), "--video", "--model", coreml_model.c_str(),
			"--input", input.c_str(), "--output", silent_video.c_str(), "--workers", workers_text.c_str(),
			"--width", width_text.c_str(), "--height", height_text.c_str(), "--bitrate", bitrate_text.c_str(),
			"--preview-seconds", preview_text.c_str(), "--compute-units", compute_units.c_str(),
			"--input-sizing", input_sizing.c_str(), "--tile-batch", tile_batch_text.c_str(),
			"--overlap", overlap_mode.c_str(), "--temporal-step", temporal_step_text.c_str(), nullptr);
		const long long pipeline_duration = preview_seconds > 0
			? std::min(duration, static_cast<long long>(preview_seconds) * 1000LL) : duration;
		if (!run_stage(task, pipeline, pipeline_duration, 1, 84,
			"M4正在进行硬件解码、Core ML增强与VideoToolbox编码", 85, "Core ML视频流水线完成")) {
			cleanup_temp(temp_root); finish_task(task, false,
				is_task_cancel_requested(task) ? "已取消" : "AI增强失败", "Core ML video pipeline failed", -1); return;
		}
		ACL_ARGV* mux = acl_argv_alloc(28);
		acl_argv_add(mux, ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y",
			"-i", silent_video.c_str(), "-i", input.c_str(), "-map", "0:v:0", "-map", "1:a?",
			"-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-shortest", "-movflags", "+faststart",
			"-progress", "pipe:1", "-nostats", output.c_str(), nullptr);
		if (!run_stage(task, mux, pipeline_duration, 85, 14, "正在封装原音轨", 99, "正在完成AI视频")) {
			unlink(output.c_str()); cleanup_temp(temp_root); finish_task(task, false,
				is_task_cancel_requested(task) ? "已取消" : "AI增强失败", "audio mux failed", -1); return;
		}
		cleanup_temp(temp_root);
		finish_task(task, true, "M4 Core ML流水线处理完成", "", file_size_of(output.c_str()));
		return;
	}
	const std::string input_pattern = frames_in + "/%08d.png";
	ACL_ARGV* extract = acl_argv_alloc(20);
	acl_argv_add(extract, ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y", "-i", input.c_str(),
		"-map", "0:v:0", nullptr);
	const std::string preview_text = std::to_string(preview_seconds);
	if (preview_seconds > 0) acl_argv_add(extract, "-t", preview_text.c_str(), nullptr);
	acl_argv_add(extract, "-vsync", "0", "-progress", "pipe:1", "-nostats", input_pattern.c_str(), nullptr);
	if (!run_stage(task, extract, duration, 1, 10, "正在解码视频帧", 12, "准备AI推理")) {
		cleanup_temp(temp_root); finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "AI增强失败", "frame extraction failed", -1); return;
	}
	ACL_ARGV* inference = acl_argv_alloc(24);
	// realesrgan-x4plus only ships x4 NCNN weights. Passing x2 makes the
	// runtime interpret the x4 tensor with an incompatible output scale and
	// can produce corrupted frames. AnimeVideo-v3 has scale-specific weights.
	const int inference_scale = model == "realesrgan-x4plus" ? 4 : scale;
	if (use_coreml) {
		const std::string workers_text = std::to_string(coreml_workers);
		acl_argv_add(inference, ai.c_str(), "--model", coreml_model.c_str(), "--input", frames_in.c_str(),
			"--output", frames_out.c_str(), "--workers", workers_text.c_str(), nullptr);
	} else {
		const std::string scale_text = std::to_string(inference_scale);
		const std::string tile_text = std::to_string(tile);
		acl_argv_add(inference, ai.c_str(), "-i", frames_in.c_str(), "-o", frames_out.c_str(), "-n", model.c_str(),
			"-s", scale_text.c_str(), "-m", models.c_str(), "-t", tile_text.c_str(), "-j", threads.c_str(), nullptr);
		const std::string gpu_text = std::to_string(gpu);
		if (gpu >= 0) acl_argv_add(inference, "-g", gpu_text.c_str(), nullptr);
		acl_argv_add(inference, "-f", "png", "-v", nullptr);
	}
	const long long total_frames = count_directory_files(frames_in);
	std::atomic<bool> inference_done(false);
	std::thread monitor([task, frames_out, total_frames, use_coreml, &inference_done] {
		while (!inference_done.load()) {
			if (total_frames > 0) {
				const long long completed = count_directory_files(frames_out);
				update_task_progress(task, 12.0 + std::min(72.0,
					72.0 * static_cast<double>(completed) / static_cast<double>(total_frames)),
					use_coreml ? "Core ML正在调用M4恢复纹理与细节" : "AI正在恢复纹理与细节");
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
	});
	const bool inference_ok = run_stage(task, inference, 0, 12, 72,
		use_coreml ? "Core ML正在调用M4恢复纹理与细节" : "AI正在恢复纹理与细节", 85, "AI推理完成");
	inference_done.store(true);
	monitor.join();
	if (!inference_ok) {
		cleanup_temp(temp_root); finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "AI增强失败",
			use_coreml ? "Core ML Real-ESRGAN inference failed" : "Real-ESRGAN inference failed", -1); return;
	}
	const std::string fps_text = std::to_string(fps > 0 ? fps : 25.0);
	const std::string output_pattern = frames_out + "/%08d.png";
	const std::string filter = "scale=" + std::to_string(width) + ":" + std::to_string(height)
		+ ":force_original_aspect_ratio=decrease:flags=lanczos,pad=" + std::to_string(width) + ":"
		+ std::to_string(height) + ":(ow-iw)/2:(oh-ih)/2";
	const std::string rate = std::to_string(bitrate) + "k";
	ACL_ARGV* encode = acl_argv_alloc(40);
	acl_argv_add(encode, ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y", "-framerate", fps_text.c_str(),
		"-start_number", "1", "-i", output_pattern.c_str(), "-i", input.c_str(), "-map", "0:v:0", "-map", "1:a?", "-vf", filter.c_str(),
		nullptr);
#ifdef __APPLE__
	if (use_coreml) {
		acl_argv_add(encode, "-c:v", "h264_videotoolbox", "-b:v", rate.c_str(), "-pix_fmt", "yuv420p", nullptr);
	} else
#endif
	{
		acl_argv_add(encode, "-c:v", "libx264", "-preset", encode_preset.c_str(), "-b:v", rate.c_str(), "-pix_fmt", "yuv420p", nullptr);
	}
	acl_argv_add(encode, "-c:a", "aac", "-b:a", "192k", "-shortest", "-movflags", "+faststart",
		"-progress", "pipe:1", "-nostats", output.c_str(), nullptr);
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
	const int tile = safe_atoi(req.getParameter("tile"), 0);
	const int gpu = safe_atoi(req.getParameter("gpu"), -1);
	const int preview_seconds = safe_atoi(req.getParameter("preview_seconds"), 0);
	const double fps = req.getParameter("fps") ? atof(req.getParameter("fps")) : 25.0;
	const std::string model = req.getParameter("model") ? req.getParameter("model") : "realesrgan-x4plus";
	const std::string threads = req.getParameter("threads") ? req.getParameter("threads") : "1:2:2";
	const std::string encode_preset = req.getParameter("encode_preset") ? req.getParameter("encode_preset") : "medium";
	const std::string compute_units = req.getParameter("compute_units") ? req.getParameter("compute_units") : "auto";
	const std::string input_sizing = req.getParameter("input_sizing") ? req.getParameter("input_sizing") : "target";
	const int requested_coreml_workers = safe_atoi(req.getParameter("coreml_workers"), 0);
	const int requested_tile_batch = safe_atoi(req.getParameter("tile_batch"), 0);
	const int temporal_step = safe_atoi(req.getParameter("temporal_step"), 1);
	const std::string overlap_mode = req.getParameter("overlap_mode") ? req.getParameter("overlap_mode") : "balanced";
	if (width < 320 || width > 3840 || height < 240 || height > 2160 || width % 2 || height % 2
		|| bitrate < 300 || bitrate > 50000 || (scale != 2 && scale != 4) || fps <= 0 || fps > 120
		|| (model != "realesrgan-x4plus" && model != "realesr-animevideov3"
			&& model != "coreml-x2plus" && model != "coreml-general-x4v3"
			&& model != "coreml-general-x4v3-w8a8" && model != "coreml-x4plus-int8")
		|| (tile != 0 && tile != 128 && tile != 256 && tile != 512)
		|| (threads != "1:2:2" && threads != "2:4:2" && threads != "4:4:4")
		|| gpu < -1 || gpu > 15
		|| (encode_preset != "fast" && encode_preset != "medium" && encode_preset != "slow")
		|| (compute_units != "auto" && compute_units != "gpu" && compute_units != "ane" && compute_units != "cpu")
		|| (input_sizing != "target" && input_sizing != "source")
		|| (requested_coreml_workers != 0 && requested_coreml_workers != 1 && requested_coreml_workers != 2 && requested_coreml_workers != 4)
		|| (requested_tile_batch != 0 && requested_tile_batch != 1 && requested_tile_batch != 2 && requested_tile_batch != 4)
		|| (overlap_mode != "low" && overlap_mode != "balanced" && overlap_mode != "quality")
		|| (temporal_step != 1 && temporal_step != 2 && temporal_step != 3)
		|| (preview_seconds != 0 && preview_seconds != 10 && preview_seconds != 30 && preview_seconds != 60)) {
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
	const std::string ffmpeg = choose_ffmpeg_path();
	const coreml_runtime_t coreml = choose_coreml_runtime(model);
	const bool coreml_model = model == "realesrgan-x4plus" || model == "coreml-x2plus"
		|| model == "coreml-general-x4v3" || model == "coreml-general-x4v3-w8a8"
		|| model == "coreml-x4plus-int8";
	const bool use_coreml = coreml_model && !coreml.executable.empty() && !coreml.model.empty();
	const bool requires_coreml = model == "coreml-x2plus" || model == "coreml-general-x4v3"
		|| model == "coreml-general-x4v3-w8a8" || model == "coreml-x4plus-int8";
	std::string models;
	std::string ai = use_coreml ? coreml.executable : (requires_coreml ? "" : choose_realesrgan(models));
	if (ffmpeg.empty() || ai.empty() || (!use_coreml && models.empty())) { json_error(res, 503, "selected Real-ESRGAN runtime or model is not installed", req.isKeepAlive()); return true; }
	// Large RRDB models quickly saturate ANE memory bandwidth. Extra model
	// instances can make them slower, while tiny/x2 models benefit from two.
	const bool heavy_coreml = model == "realesrgan-x4plus" || model == "coreml-x4plus-int8";
	const int coreml_workers = requested_coreml_workers > 0
		? requested_coreml_workers : (heavy_coreml ? 1 : 2);
	const int tile_batch = requested_tile_batch > 0
		? requested_tile_batch : (heavy_coreml ? 1 : 2);
	const std::string input = local ? source : join_upload_path(upload_dir, source);
	std::string output_label = "general";
	if (model == "realesr-animevideov3") output_label = "anime";
	else if (model == "coreml-x2plus") output_label = "x2plus";
	else if (model == "coreml-general-x4v3") output_label = "light-x4";
	else if (model == "coreml-general-x4v3-w8a8") output_label = "light-w8a8-x4";
	else if (model == "coreml-x4plus-int8") output_label = "int8-x4";
	if (coreml_model && compute_units != "auto") output_label += "-" + compute_units;
	if (coreml_model && input_sizing == "target") output_label += "-target";
	if (coreml_model && temporal_step > 1) output_label += "-step" + std::to_string(temporal_step);
	if (preview_seconds > 0) output_label += "_preview" + std::to_string(preview_seconds) + "s";
	const std::string output_name = ai_output_name(source, local, upload_dir, output_label, width, height);
	const std::string output = local ? output_name : join_upload_path(upload_dir, output_name);
	const std::string task_file = (local ? "ai-enhance-local:" : "ai-enhance:") + source;
	auto task = std::make_shared<transcode_task_t>(); task->id = make_task_id(); task->scope = upload_dir; task->file_name = task_file;
	task->output_name = output_name; task->local = local;
	task->message = use_coreml ? "等待M4 Core ML超分辨率处理" : "等待AI超分辨率处理";
	const std::string key = scoped_task_key(upload_dir, task_file);
	{ std::lock_guard<webcool::mutex> guard(g_transcode_mutex); const auto it = g_running_task_by_file.find(key); if (it != g_running_task_by_file.end()) { const auto old = g_transcode_tasks.find(it->second); if (old != g_transcode_tasks.end() && !old->second->done) { acl::json json; acl::json_node& root=json.create_node(); root.add_bool("ok",true); root.add_text("task_id",old->second->id.c_str()); root.add_text("name",old->second->output_name.c_str()); return sendJson(res,200,root,req.isKeepAlive()); } } g_transcode_tasks[task->id]=task; g_running_task_by_file[key]=task->id; }
	const std::string temp_root = local_parent_path(output) + "/.ai_enhance_tmp." + task->id;
	go[task, ffmpeg, ai, models, use_coreml, coreml, coreml_workers, compute_units, input_sizing, tile_batch, overlap_mode, temporal_step, input, output, temp_root, model, scale, width, height, bitrate, fps, tile, threads, gpu, encode_preset, preview_seconds] {
		acl::gofiber_wait_thread([task, ffmpeg, ai, models, use_coreml, coreml, coreml_workers, compute_units, input_sizing, tile_batch, overlap_mode, temporal_step, input, output, temp_root, model, scale, width, height, bitrate, fps, tile, threads, gpu, encode_preset, preview_seconds] {
			run_ai_task(task, ffmpeg, ai, models, use_coreml, coreml.model, coreml_workers, compute_units, input_sizing, tile_batch, overlap_mode, temporal_step, input, output, temp_root, model, scale, width, height, bitrate, fps,
				tile, threads, gpu, encode_preset, preview_seconds);
		});
	};
	acl::json json; acl::json_node& root=json.create_node(); root.add_bool("ok",true); root.add_text("task_id",task->id.c_str()); root.add_text("name",output_name.c_str()); root.add_text("backend", use_coreml ? "coreml" : "ncnn-vulkan"); root.add_text("compute_units", use_coreml ? compute_units.c_str() : "ncnn"); return sendJson(res,200,root,req.isKeepAlive());
}

bool AiVideoEnhanceAction::progress(request_t& req, response_t& res, const std::string& scope, bool local) { transcode_task_snapshot_t t; if (!ai_snapshot(req,scope,local,t)) { json_error(res,404,"AI task not found",req.isKeepAlive()); return true; } acl::json j; acl::json_node& r=j.create_node(); r.add_bool("ok",true); r.add_text("name",t.output_name.c_str()); r.add_bool("done",t.done); r.add_bool("success",t.success); r.add_bool("cancel_requested",t.cancel_requested); r.add_number("progress",(long long)t.progress); r.add_text("message",t.message.c_str()); if(!t.error.empty())r.add_text("error",t.error.c_str()); return sendJson(res,200,r,req.isKeepAlive()); }
bool AiVideoEnhanceAction::cancel(request_t& req, response_t& res, const std::string& scope, bool local) { transcode_task_snapshot_t t; if(!ai_snapshot(req,scope,local,t)){json_error(res,404,"AI task not found",req.isKeepAlive());return true;} bool sent=false;request_cancel_task(t.id.c_str(),scope,t,sent);acl::json j;acl::json_node&r=j.create_node();r.add_bool("ok",true);r.add_bool("cancel_requested",true);r.add_bool("signal_sent",sent);return sendJson(res,200,r,req.isKeepAlive()); }
} // namespace action

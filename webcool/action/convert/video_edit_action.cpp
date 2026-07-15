#include "stdafx.h"
#include "convert_common.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace action {
namespace {

long long integer_param(const request_t& req, const char* name, long long fallback = 0)
{
	const char* value = req.getParameter(name);
	if (!value || !*value) return fallback;
	char* end = nullptr;
	errno = 0;
	const long long result = std::strtoll(value, &end, 10);
	return errno == 0 && end && *end == '\0' ? result : fallback;
}

double decimal_param(const request_t& req, const char* name, double fallback)
{
	const char* value = req.getParameter(name);
	if (!value || !*value) return fallback;
	char* end = nullptr;
	errno = 0;
	const double result = std::strtod(value, &end);
	return errno == 0 && end && *end == '\0' && std::isfinite(result)
		? result : fallback;
}

std::string decimal_text(double value)
{
	std::ostringstream stream;
	stream << std::fixed << std::setprecision(4) << value;
	return stream.str();
}

std::string unique_edit_output(const std::string& source, bool local,
	const std::string& upload_dir)
{
	const std::string stem = replace_ext(source, "");
	for (int i = 1; i < 10000; ++i) {
		const std::string suffix = i == 1 ? "" : ("_" + std::to_string(i));
		const std::string candidate = stem + "_edit" + suffix + ".mp4";
		if (!path_exists(local ? candidate : join_upload_path(upload_dir, candidate))) {
			return candidate;
		}
	}
	return stem + "_edit_" + std::to_string(g_transcode_seq.load()) + ".mp4";
}

std::string audio_tempo_filter(double speed)
{
	std::vector<std::string> filters;
	double remaining = speed;
	while (remaining > 2.0 + 0.0001) {
		filters.push_back("atempo=2.0");
		remaining /= 2.0;
	}
	while (remaining < 0.5 - 0.0001) {
		filters.push_back("atempo=0.5");
		remaining /= 0.5;
	}
	filters.push_back("atempo=" + decimal_text(remaining));
	std::string result;
	for (size_t i = 0; i < filters.size(); ++i) {
		if (i) result += ",";
		result += filters[i];
	}
	return result;
}

bool has_extension(const std::string& path, const std::vector<std::string>& extensions)
{
	for (size_t i = 0; i < extensions.size(); ++i) {
		if (path.size() >= extensions[i].size()
			&& strcasecmp(path.c_str() + path.size() - extensions[i].size(), extensions[i].c_str()) == 0) return true;
	}
	return false;
}

bool resolve_auxiliary_file(const request_t& req, const std::string& parameter,
	const std::string& upload_dir, bool local, const std::vector<std::string>& extensions,
	std::string& path, std::string& err, int& status,
	const std::string& auxiliary_upload_dir = "", bool uploaded = false)
{
	const char* raw = req.getParameter(parameter.c_str());
	if (!raw || !*raw) { err = "missing auxiliary media path"; status = 400; return false; }
	if (local && uploaded) {
		std::string relative;
		if (auxiliary_upload_dir.empty()
			|| !normalize_relative_path(raw, relative, err, false)
			|| !resolve_upload_regular_file_path(auxiliary_upload_dir, relative, relative)
			|| !has_extension(relative, extensions)) {
			err = "uploaded auxiliary media file not found"; status = 404; return false;
		}
		if (!ensure_remote_video_transcode_lock_policy(auxiliary_upload_dir,
			relative, err, status)) return false;
		path = join_upload_path(auxiliary_upload_dir, relative);
		return true;
	}
	if (local) {
		if (!normalize_local_video_path(raw, path, err)) { status = 400; return false; }
		struct stat st{};
		if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || !has_extension(path, extensions)) {
			err = "auxiliary media file not found"; status = 404; return false;
		}
		return ensure_local_video_transcode_lock_policy(upload_dir, path, err, status);
	}
	std::string relative;
	if (!normalize_relative_path(raw, relative, err, false)
		|| !resolve_upload_regular_file_path(upload_dir, relative, relative)
		|| !has_extension(relative, extensions)) {
		err = "auxiliary media file not found"; status = 404; return false;
	}
	if (!ensure_remote_video_transcode_lock_policy(upload_dir, relative, err, status)) return false;
	path = join_upload_path(upload_dir, relative);
	return true;
}

std::string build_video_filter(double speed, int rotate, bool flip_h,
	bool flip_v, const std::string& crop, int output_height)
{
	std::vector<std::string> filters;
	if (std::fabs(speed - 1.0) > 0.0001) {
		filters.push_back("setpts=PTS/" + decimal_text(speed));
	}
	if (rotate == 90) filters.push_back("transpose=1");
	else if (rotate == 180) { filters.push_back("hflip"); filters.push_back("vflip"); }
	else if (rotate == 270) filters.push_back("transpose=2");
	if (flip_h) filters.push_back("hflip");
	if (flip_v) filters.push_back("vflip");
	if (crop == "16:9") filters.push_back("crop=min(iw\\,ih*16/9):min(ih\\,iw*9/16)");
	else if (crop == "9:16") filters.push_back("crop=min(iw\\,ih*9/16):min(ih\\,iw*16/9)");
	else if (crop == "1:1") filters.push_back("crop=min(iw\\,ih):min(iw\\,ih)");
	if (output_height > 0) {
		filters.push_back("scale=-2:" + std::to_string(output_height)
			+ ":force_original_aspect_ratio=decrease");
		filters.push_back("scale=trunc(iw/2)*2:trunc(ih/2)*2");
	} else if (crop != "original") {
		filters.push_back("scale=trunc(iw/2)*2:trunc(ih/2)*2");
	}
	std::string result;
	for (size_t i = 0; i < filters.size(); ++i) {
		if (i) result += ",";
		result += filters[i];
	}
	return result;
}

void run_video_edit(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input,
	const std::string& temporary, const std::string& output,
	long long start_ms, long long end_ms, double speed, int volume,
	bool muted, int rotate, bool flip_h, bool flip_v,
	const std::string& crop, int output_height, const std::string& audio_mode,
	const std::string& audio_path, long long audio_start_ms,
	const std::string& subtitle_mode, const std::string& subtitle_path,
	long long subtitle_start_ms, const std::string& subtitle_import_mode)
{
	unlink(temporary.c_str());
	const long long source_duration = probe_duration_ms(ffmpeg, input);
	long long selected_duration = end_ms > start_ms ? end_ms - start_ms
		: (source_duration > start_ms ? source_duration - start_ms : source_duration);
	if (selected_duration > 0) selected_duration = static_cast<long long>(selected_duration / speed);

	const std::string video_filter = build_video_filter(speed, rotate, flip_h,
		flip_v, crop, output_height);
	const bool fast_subtitle_import = subtitle_mode == "replace"
		&& subtitle_import_mode == "fast";
	std::string audio_filter;
	if (!muted && audio_mode != "remove") {
		if (std::fabs(speed - 1.0) > 0.0001) audio_filter = audio_tempo_filter(speed);
		if (volume != 100) {
			if (!audio_filter.empty()) audio_filter += ",";
			audio_filter += "volume=" + decimal_text(volume / 100.0);
		}
		if (audio_mode == "replace" && audio_start_ms > 0) {
			if (!audio_filter.empty()) audio_filter += ",";
			audio_filter += "adelay=" + std::to_string(audio_start_ms) + "|" + std::to_string(audio_start_ms);
		}
	}

	ACL_ARGV* args = acl_argv_alloc(48);
	acl_argv_add(args, ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y", nullptr);
	const std::string start_text = decimal_text(start_ms / 1000.0);
	if (start_ms > 0) acl_argv_add(args, "-ss", start_text.c_str(), nullptr);
	const std::string duration_text = end_ms > start_ms
		? decimal_text((end_ms - start_ms) / 1000.0) : "";
	if (!duration_text.empty()) acl_argv_add(args, "-t", duration_text.c_str(), nullptr);
	acl_argv_add(args, "-i", input.c_str(), nullptr);
	int next_input = 1;
	int audio_input = -1;
	int subtitle_input = -1;
	if (audio_mode == "replace") {
		audio_input = next_input++;
		acl_argv_add(args, "-i", audio_path.c_str(), nullptr);
	}
	const std::string subtitle_offset = decimal_text(subtitle_start_ms / 1000.0);
	if (subtitle_mode == "replace") {
		subtitle_input = next_input++;
		if (subtitle_start_ms > 0) acl_argv_add(args, "-itsoffset", subtitle_offset.c_str(), nullptr);
		acl_argv_add(args, "-i", subtitle_path.c_str(), nullptr);
	}
	acl_argv_add(args, "-map", "0:v:0", nullptr);
	const std::string audio_map = audio_input >= 0 ? std::to_string(audio_input) + ":a:0" : "0:a:0?";
	if (!muted && audio_mode != "remove") acl_argv_add(args, "-map", audio_map.c_str(), nullptr);
	const std::string subtitle_map = subtitle_input >= 0 ? std::to_string(subtitle_input) + ":s:0" : "0:s:0?";
	if (subtitle_mode != "remove") acl_argv_add(args, "-map", subtitle_map.c_str(), nullptr);
	if (!video_filter.empty()) acl_argv_add(args, "-vf", video_filter.c_str(), nullptr);
	if (muted || audio_mode == "remove") acl_argv_add(args, "-an", nullptr);
	else if (!audio_filter.empty()) acl_argv_add(args, "-af", audio_filter.c_str(), nullptr);
	if (fast_subtitle_import) {
		acl_argv_add(args, "-c:v", "copy", nullptr);
		if (!muted && audio_mode != "remove") acl_argv_add(args, "-c:a", "copy", nullptr);
	} else {
		acl_argv_add(args, "-c:v", "libx264", "-preset", "medium", "-crf", "21",
			"-pix_fmt", "yuv420p", nullptr);
		if (!muted && audio_mode != "remove") acl_argv_add(args, "-c:a", "aac", "-b:a", "192k", nullptr);
	}
	if (subtitle_mode != "remove") acl_argv_add(args, "-c:s", "mov_text", nullptr);
	const std::string output_duration_text = selected_duration > 0
		? decimal_text(selected_duration / 1000.0) : "";
	if (!output_duration_text.empty()) acl_argv_add(args, "-t", output_duration_text.c_str(), nullptr);
	acl_argv_add(args, "-movflags", "+faststart", "-progress", "pipe:1", "-nostats",
		temporary.c_str(), nullptr);

	const ffmpeg_process_ptr process = start_ffmpeg_process(args);
	if (!process) {
		finish_task(task, false, "视频剪辑启动失败", acl::last_serror(), -1);
		return;
	}
	const int code = wait_transcode_progress(task, *process, selected_duration,
		2.0, 95.0, fast_subtitle_import ? "正在快速添加字幕" : "正在导出剪辑",
		98.0, "正在写入MP4文件");
	if (code != 0 || is_task_cancel_requested(task) || file_size_of(temporary.c_str()) <= 0) {
		unlink(temporary.c_str());
		finish_task(task, false, is_task_cancel_requested(task) ? "已取消"
			: (fast_subtitle_import ? "快速添加字幕失败" : "视频剪辑失败"),
			is_task_cancel_requested(task) ? "cancelled"
			: (fast_subtitle_import ? "快速封装失败，请改用兼容转码" : "ffmpeg video edit failed"), -1);
		return;
	}
	if (rename(temporary.c_str(), output.c_str()) != 0) {
		unlink(temporary.c_str());
		finish_task(task, false, "视频剪辑失败", "rename edited video failed", -1);
		return;
	}
	long long output_size = file_size_of(output.c_str());
	int subtitle_status = 0;
	std::string vtt_path;
	std::string subtitle_err;
	if (subtitle_mode != "remove") {
		update_task_progress(task, 99.0, "正在生成浏览器字幕文件");
		subtitle_status = export_vtt_sidecar(ffmpeg, output, output,
			vtt_path, subtitle_err);
		if (subtitle_status > 0) output_size += file_size_of(vtt_path.c_str());
	}
	const char* success_message = fast_subtitle_import ? "快速添加字幕完成" : "视频剪辑完成";
	if (subtitle_status > 0) {
		success_message = fast_subtitle_import
			? "快速添加字幕完成，已生成浏览器VTT字幕"
			: "视频剪辑完成，已生成浏览器VTT字幕";
	} else if (subtitle_status < 0) {
		success_message = "视频已生成，但浏览器VTT字幕生成失败";
	}
	finish_task(task, true, success_message, "", output_size);
}

bool edit_task_snapshot(const request_t& req, const std::string& scope,
	bool local, transcode_task_snapshot_t& task)
{
	const char* prefix = local ? "video-edit-local:" : "video-edit:";
	return snapshot_task_by_id(req.getParameter("task_id"), scope, task)
		&& task.file_name.compare(0, strlen(prefix), prefix) == 0;
}

void run_subtitle_export(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input, const std::string& temporary,
	const std::string& output, long long start_ms, long long end_ms)
{
	unlink(temporary.c_str());
	const long long source_duration = probe_duration_ms(ffmpeg, input);
	const long long duration = end_ms > start_ms ? end_ms - start_ms
		: (source_duration > start_ms ? source_duration - start_ms : source_duration);
	ACL_ARGV* args = acl_argv_alloc(28);
	acl_argv_add(args, ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y", nullptr);
	const std::string start_text = decimal_text(start_ms / 1000.0);
	const std::string duration_text = end_ms > start_ms ? decimal_text((end_ms - start_ms) / 1000.0) : "";
	if (start_ms > 0) acl_argv_add(args, "-ss", start_text.c_str(), nullptr);
	acl_argv_add(args, "-i", input.c_str(), "-map", "0:s:0", "-c:s", "webvtt", nullptr);
	if (!duration_text.empty()) acl_argv_add(args, "-t", duration_text.c_str(), nullptr);
	acl_argv_add(args,
		"-progress", "pipe:1", "-nostats", temporary.c_str(), nullptr);
	const ffmpeg_process_ptr process = start_ffmpeg_process(args);
	if (!process) { finish_task(task, false, "字幕导出启动失败", acl::last_serror(), -1); return; }
	const int code = wait_transcode_progress(task, *process, duration, 3, 94,
		"正在导出字幕", 98, "正在写入VTT字幕");
	if (code != 0 || is_task_cancel_requested(task) || file_size_of(temporary.c_str()) <= 0) {
		unlink(temporary.c_str());
		finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "字幕导出失败",
			is_task_cancel_requested(task) ? "cancelled" : "ffmpeg subtitle export failed", -1);
		return;
	}
	if (rename(temporary.c_str(), output.c_str()) != 0) {
		unlink(temporary.c_str()); finish_task(task, false, "字幕导出失败", "rename failed", -1); return;
	}
	finish_task(task, true, "字幕导出完成", "", file_size_of(output.c_str()));
}

bool is_keyframe_image_name(const char* name)
{
	if (!name) return false;
	const char* prefix = "keyframe_";
	const size_t prefix_len = strlen(prefix);
	const size_t length = strlen(name);
	if (length <= prefix_len + 4 || strncmp(name, prefix, prefix_len) != 0
		|| strcasecmp(name + length - 4, ".jpg") != 0) return false;
	for (size_t i = prefix_len; i < length - 4; ++i) {
		if (name[i] < '0' || name[i] > '9') return false;
	}
	return true;
}

void cleanup_keyframe_directory(const std::string& directory, bool remove_directory)
{
	DIR* dir = opendir(directory.c_str());
	if (dir) {
		for (dirent* entry = readdir(dir); entry; entry = readdir(dir)) {
			if (is_keyframe_image_name(entry->d_name)) {
				unlink(local_join_path(directory, entry->d_name).c_str());
			}
		}
		closedir(dir);
	}
	if (remove_directory) rmdir(directory.c_str());
}

bool publish_keyframe_directory(const std::string& temporary,
	const std::string& output, long long& total_size, long long& image_count,
	std::string& err)
{
	DIR* source_dir = opendir(temporary.c_str());
	if (!source_dir) {
		err = "open temporary keyframe directory failed";
		return false;
	}
	long long available_images = 0;
	for (dirent* entry = readdir(source_dir); entry; entry = readdir(source_dir)) {
		if (is_keyframe_image_name(entry->d_name)) ++available_images;
	}
	closedir(source_dir);
	if (available_images <= 0) {
		cleanup_keyframe_directory(temporary, true);
		err = "no keyframes found in selected range";
		return false;
	}
	struct stat st{};
	if (stat(output.c_str(), &st) == 0) {
		if (!S_ISDIR(st.st_mode)) {
			err = "keyframe output path is not a directory";
			return false;
		}
	} else if (mkdir(output.c_str(), 0755) != 0) {
		err = acl::last_serror();
		return false;
	}
	cleanup_keyframe_directory(output, false);
	DIR* dir = opendir(temporary.c_str());
	if (!dir) {
		err = "open temporary keyframe directory failed";
		return false;
	}
	total_size = 0;
	image_count = 0;
	bool ok = true;
	for (dirent* entry = readdir(dir); entry; entry = readdir(dir)) {
		if (!is_keyframe_image_name(entry->d_name)) continue;
		const std::string source = local_join_path(temporary, entry->d_name);
		const std::string destination = local_join_path(output, entry->d_name);
		if (rename(source.c_str(), destination.c_str()) != 0) {
			err = "move keyframe image failed";
			ok = false;
			break;
		}
		total_size += std::max(0LL, file_size_of(destination.c_str()));
		++image_count;
	}
	closedir(dir);
	cleanup_keyframe_directory(temporary, true);
	if (!ok) return false;
	return true;
}

void cleanup_screenshot_temporary_directory(const std::string& directory)
{
	unlink(local_join_path(directory, "screenshot.jpg").c_str());
	rmdir(directory.c_str());
}

bool publish_screenshot(const std::string& temporary,
	const std::string& output, long long& image_size, std::string& image_name,
	std::string& err)
{
	const std::string source = local_join_path(temporary, "screenshot.jpg");
	image_size = file_size_of(source.c_str());
	if (image_size <= 0) {
		cleanup_screenshot_temporary_directory(temporary);
		err = "screenshot output is empty";
		return false;
	}
	struct stat st{};
	if (stat(output.c_str(), &st) == 0) {
		if (!S_ISDIR(st.st_mode)) {
			cleanup_screenshot_temporary_directory(temporary);
			err = "screenshot output path is not a directory";
			return false;
		}
	} else if (mkdir(output.c_str(), 0755) != 0) {
		cleanup_screenshot_temporary_directory(temporary);
		err = acl::last_serror();
		return false;
	}
	for (int i = 1; i < 1000000; ++i) {
		acl::string candidate;
		candidate.format("screenshot_%06d.jpg", i);
		const std::string destination = local_join_path(output, candidate.c_str());
		if (path_exists(destination)) continue;
		if (rename(source.c_str(), destination.c_str()) != 0) {
			cleanup_screenshot_temporary_directory(temporary);
			err = "move screenshot image failed";
			return false;
		}
		image_name = candidate.c_str();
		rmdir(temporary.c_str());
		return true;
	}
	cleanup_screenshot_temporary_directory(temporary);
	err = "too many screenshots in output directory";
	return false;
}

struct screenshot_options_t {
	std::string mode;
	std::string model;
	std::string compute_units;
	std::string overlap;
	std::string face_restoration;
	int scale;
	int denoise;
	int sharpen;
	int brightness;
	int restoration_strength;
	int face_fidelity;
	int face_only_center;
	int codeformer_aligned;
	int codeformer_upscale;
	int red_eye_strength;
	int red_eye_only_center;
	int quality;
	int tile;
	screenshot_options_t() : mode("original"), model("realesrgan-x4plus"),
		compute_units("auto"), overlap("balanced"), face_restoration("none"), scale(4), denoise(0),
		sharpen(35), brightness(20), restoration_strength(35), face_fidelity(90), face_only_center(1),
		codeformer_aligned(0), codeformer_upscale(0),
		red_eye_strength(80), red_eye_only_center(0), quality(95), tile(0) {}
};

struct screenshot_ai_runtime_t {
	std::string executable;
	std::string model_path;
	bool coreml;
	screenshot_ai_runtime_t() : coreml(false) {}
};

struct codeformer_runtime_t {
	std::string python;
	std::string runner;
	std::string repository;
};

std::string first_executable(const std::vector<std::string>& candidates)
{
	for (size_t i = 0; i < candidates.size(); ++i) {
		if (access(candidates[i].c_str(), X_OK) == 0) return candidates[i];
	}
	return "";
}

std::string choose_red_eye_runtime()
{
#ifdef __APPLE__
	const char* env_bin = getenv("AICOOL_RED_EYE_CORRECT");
	return env_bin && *env_bin ? env_bin : first_executable({
		"/opt/soft/webcool/bin/red-eye-correct", "tools/mac/red-eye-correct",
		"../tools/mac/red-eye-correct"});
#else
	return "";
#endif
}

screenshot_ai_runtime_t choose_screenshot_ai_runtime(const screenshot_options_t& options)
{
	screenshot_ai_runtime_t runtime;
	if (options.model.compare(0, 7, "coreml-") == 0) {
#if defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))
		const char* env_bin = getenv("AICOOL_COREML_REALESRGAN");
		const char* env_model = getenv("AICOOL_COREML_REALESRGAN_MODEL");
		runtime.executable = env_bin && *env_bin ? env_bin : first_executable({
			"/opt/soft/webcool/bin/coreml-realesrgan", "tools/mac/coreml-realesrgan",
			"../tools/mac/coreml-realesrgan"});
		std::string file_name = "realesrgan-x2plus.mlmodelc";
		if (options.model == "coreml-general-x4v3") file_name = "realesr-general-x4v3.mlmodelc";
		else if (options.model == "coreml-general-x4v3-w8a8") file_name = "realesr-general-x4v3-w8a8.mlmodelc";
		else if (options.model == "coreml-x4plus-int8") file_name = "realesrgan-x4plus-int8.mlmodelc";
		if (env_model && *env_model) runtime.model_path = env_model;
		else {
			const std::vector<std::string> models = {
				"/opt/soft/webcool/models/coreml/" + file_name,
				"tools/mac/coreml-models/" + file_name,
				"../tools/mac/coreml-models/" + file_name
			};
			for (size_t i = 0; i < models.size(); ++i) {
				struct stat st{};
				if (stat(models[i].c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
					runtime.model_path = models[i]; break;
				}
			}
		}
		runtime.coreml = true;
#endif
		return runtime;
	}
	const char* env_bin = getenv("AICOOL_REALESRGAN");
	const char* env_models = getenv("AICOOL_REALESRGAN_MODELS");
	runtime.executable = env_bin && *env_bin ? env_bin : first_executable({
#ifdef _WIN32
		"realesrgan-ncnn-vulkan.exe", "tools\\windows\\realesrgan-ncnn-vulkan.exe",
		"..\\tools\\windows\\realesrgan-ncnn-vulkan.exe"
#elif defined(__APPLE__)
		"/opt/soft/webcool/bin/realesrgan-ncnn-vulkan", "tools/mac/realesrgan-ncnn-vulkan",
		"../tools/mac/realesrgan-ncnn-vulkan"
#else
		"/opt/soft/webcool/bin/realesrgan-ncnn-vulkan", "tools/linux/realesrgan-ncnn-vulkan",
		"../tools/linux/realesrgan-ncnn-vulkan"
#endif
	});
	if (env_models && *env_models) runtime.model_path = env_models;
	else {
		const std::vector<std::string> models = {
			"/opt/soft/webcool/models/realesrgan", "tools/mac/realesrgan-models",
			"../tools/mac/realesrgan-models", "tools/linux/realesrgan-models",
			"../tools/linux/realesrgan-models"
		};
		for (size_t i = 0; i < models.size(); ++i) {
			struct stat st{};
			if (stat(models[i].c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
				runtime.model_path = models[i]; break;
			}
		}
	}
	return runtime;
}

screenshot_ai_runtime_t choose_restormer_runtime(const std::string& restoration)
{
	screenshot_ai_runtime_t runtime;
#if defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))
	const char* env_bin = getenv("AICOOL_RESTORMER");
	const char* env_model = restoration == "defocus"
		? getenv("AICOOL_RESTORMER_DEFOCUS_MODEL")
		: getenv("AICOOL_RESTORMER_MOTION_MODEL");
	runtime.executable = env_bin && *env_bin ? env_bin : first_executable({
		"/opt/soft/webcool/bin/coreml-realesrgan", "tools/mac/coreml-realesrgan",
		"../tools/mac/coreml-realesrgan"});
	if (env_model && *env_model) runtime.model_path = env_model;
	else {
		const std::string name = restoration == "defocus"
			? "restormer-defocus-deblur.mlmodelc" : "restormer-motion-deblur.mlmodelc";
		const std::vector<std::string> models = {
			"/opt/soft/webcool/models/restormer/" + name,
			"tools/mac/restormer-models/" + name,
			"../tools/mac/restormer-models/" + name
		};
		for (size_t i = 0; i < models.size(); ++i) {
			struct stat st{};
			if (stat(models[i].c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
				runtime.model_path = models[i]; break;
			}
		}
	}
	runtime.coreml = true;
#else
	(void) restoration;
#endif
	return runtime;
}

codeformer_runtime_t choose_codeformer_runtime()
{
	codeformer_runtime_t runtime;
	const char* env_python = getenv("AICOOL_CODEFORMER_PYTHON");
	const char* env_runner = getenv("AICOOL_CODEFORMER_RUNNER");
	const char* env_repo = getenv("AICOOL_CODEFORMER_REPO");
	runtime.python = env_python && *env_python ? env_python : first_executable({
		"/opt/soft/webcool/codeformer/venv/bin/python3",
		"tools/codeformer/venv/bin/python3", "../tools/codeformer/venv/bin/python3"});
	const std::vector<std::string> runners = {
		"/opt/soft/webcool/libexec/codeformer_runner.py",
		"tools/codeformer_runner.py", "../tools/codeformer_runner.py"};
	if (env_runner && *env_runner) runtime.runner = env_runner;
	else {
		for (size_t i = 0; i < runners.size(); ++i) {
			if (access(runners[i].c_str(), R_OK) == 0) { runtime.runner = runners[i]; break; }
		}
	}
	const std::vector<std::string> repositories = {
		"/opt/soft/webcool/codeformer/CodeFormer",
		"tools/codeformer/CodeFormer", "../tools/codeformer/CodeFormer"};
	if (env_repo && *env_repo) runtime.repository = env_repo;
	else {
		for (size_t i = 0; i < repositories.size(); ++i) {
			const std::string inference = local_join_path(repositories[i], "inference_codeformer.py");
			if (access(inference.c_str(), R_OK) == 0) { runtime.repository = repositories[i]; break; }
		}
	}
	return runtime;
}

void cleanup_screenshot_work_directory(const std::string& path)
{
	std::string output;
#ifdef _WIN32
	run_command_capture("rmdir /S /Q " + shell_quote(path), output);
#else
	run_command_capture("rm -rf " + shell_quote(path), output);
#endif
}

int screenshot_jpeg_qscale(int quality)
{
	return std::max(2, std::min(12, 2 + (100 - quality) / 6));
}

bool run_screenshot_stage(const std::shared_ptr<transcode_task_t>& task,
	ACL_ARGV* args, double start, double span, const char* message,
	double end, const char* end_message)
{
	const ffmpeg_process_ptr process = start_ffmpeg_process(args);
	if (!process) return false;
	return wait_transcode_progress(task, *process, 1000, start, span,
		message, end, end_message) == 0 && !is_task_cancel_requested(task);
}

void run_single_screenshot_export(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input,
	const std::string& temporary_directory, const std::string& output_directory,
	long long position_ms, const screenshot_options_t& options)
{
	cleanup_screenshot_work_directory(temporary_directory);
	if (mkdir(temporary_directory.c_str(), 0755) != 0) {
		finish_task(task, false, "截屏启动失败", acl::last_serror(), -1); return;
	}
	const std::string position = decimal_text(position_ms / 1000.0);
	const std::string qscale = std::to_string(screenshot_jpeg_qscale(options.quality));
	if (options.mode != "ai") {
		const std::string screenshot = local_join_path(temporary_directory, "screenshot.jpg");
		ACL_ARGV* capture = acl_argv_alloc(28);
		acl_argv_add(capture, ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y",
			"-ss", position.c_str(), "-i", input.c_str(), "-map", "0:v:0", "-an",
			"-frames:v", "1", nullptr);
		if (options.mode == "sharpen") {
			const std::string amount = decimal_text(options.sharpen / 50.0);
			const std::string filter = "unsharp=5:5:" + amount + ":3:3:0";
			acl_argv_add(capture, "-vf", filter.c_str(), nullptr);
		}
		acl_argv_add(capture, "-q:v", qscale.c_str(), "-progress", "pipe:1", "-nostats",
			screenshot.c_str(), nullptr);
		if (!run_screenshot_stage(task, capture, 2, 93,
			options.mode == "sharpen" ? "正在锐化并截取当前画面" : "正在截取当前画面",
			97, "正在保存截屏图片")) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "截屏失败",
				is_task_cancel_requested(task) ? "cancelled" : "ffmpeg screenshot failed", -1);
			return;
		}
	} else {
		const std::string frames_in = local_join_path(temporary_directory, "input");
		const std::string frames_out = local_join_path(temporary_directory, "output");
		if (mkdir(frames_in.c_str(), 0755) != 0 || mkdir(frames_out.c_str(), 0755) != 0) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, "AI截屏启动失败", "cannot create AI screenshot directories", -1); return;
		}
		const std::string source_png = local_join_path(frames_in, "screenshot.png");
		ACL_ARGV* capture = acl_argv_alloc(30);
		acl_argv_add(capture, ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y",
			"-ss", position.c_str(), "-i", input.c_str(), "-map", "0:v:0", "-an",
			"-frames:v", "1", nullptr);
		std::string filter;
		if (options.denoise == 1) filter = "hqdn3d=0.6:0.6:2.4:2.4";
		else if (options.denoise == 2) filter = "hqdn3d=1.0:1.0:3.5:3.5";
		if (options.sharpen > 0) {
			if (!filter.empty()) filter += ",";
			filter += "unsharp=5:5:" + decimal_text(options.sharpen / 100.0) + ":3:3:0";
		}
		if (!filter.empty()) acl_argv_add(capture, "-vf", filter.c_str(), nullptr);
		acl_argv_add(capture, "-progress", "pipe:1", "-nostats", source_png.c_str(), nullptr);
		if (!run_screenshot_stage(task, capture, 2, 23, "正在准备AI截屏画面", 25, "准备AI推理")) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "AI截屏失败",
				is_task_cancel_requested(task) ? "cancelled" : "AI screenshot preprocessing failed", -1); return;
		}
		const screenshot_ai_runtime_t runtime = choose_screenshot_ai_runtime(options);
		if (runtime.executable.empty() || runtime.model_path.empty()) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, "AI截屏失败", "selected Real-ESRGAN runtime or model is not installed", -1); return;
		}
		ACL_ARGV* inference = acl_argv_alloc(30);
		if (runtime.coreml) {
			acl_argv_add(inference, runtime.executable.c_str(), "--model", runtime.model_path.c_str(),
				"--input", frames_in.c_str(), "--output", frames_out.c_str(), "--workers", "1",
				"--compute-units", options.compute_units.c_str(), "--tile-batch", "1",
				"--overlap", options.overlap.c_str(), "--cleanup-path", temporary_directory.c_str(), nullptr);
		} else {
			const std::string scale = std::to_string(options.scale);
			const std::string tile = std::to_string(options.tile);
			acl_argv_add(inference, runtime.executable.c_str(), "-i", frames_in.c_str(),
				"-o", frames_out.c_str(), "-n", options.model.c_str(), "-s", scale.c_str(),
				"-m", runtime.model_path.c_str(), "-t", tile.c_str(), "-j", "1:2:2",
				"-f", "png", "-v", nullptr);
		}
		if (!run_screenshot_stage(task, inference, 25, 60, "AI正在恢复纹理与细节", 85, "AI超分辨率完成")) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "AI截屏失败",
				is_task_cancel_requested(task) ? "cancelled" : "Real-ESRGAN screenshot inference failed", -1); return;
		}
		const std::string enhanced_png = local_join_path(frames_out, "screenshot.png");
		const std::string screenshot = local_join_path(temporary_directory, "screenshot.jpg");
		ACL_ARGV* encode = acl_argv_alloc(22);
		acl_argv_add(encode, ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y",
			"-i", enhanced_png.c_str(), "-frames:v", "1", "-q:v", qscale.c_str(),
			"-progress", "pipe:1", "-nostats", screenshot.c_str(), nullptr);
		if (!run_screenshot_stage(task, encode, 85, 12, "正在生成AI增强截屏", 97, "正在保存截屏图片")) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "AI截屏失败",
				is_task_cancel_requested(task) ? "cancelled" : "AI screenshot encoding failed", -1); return;
		}
	}
	long long image_size = 0;
	std::string image_name;
	std::string err;
	if (!publish_screenshot(temporary_directory, output_directory, image_size, image_name, err)) {
		cleanup_screenshot_work_directory(temporary_directory);
		finish_task(task, false, options.mode == "ai" ? "AI截屏失败" : "截屏失败", err.c_str(), -1); return;
	}
	// publish_screenshot removes the root only for simple captures. AI leaves its
	// private input/output folders, so remove them after the final image is moved.
	cleanup_screenshot_work_directory(temporary_directory);
	acl::string message;
	message.format(options.mode == "ai" ? "AI截屏完成：%s" : "截屏完成：%s", image_name.c_str());
	finish_task(task, true, message.c_str(), "", image_size);
}

void run_keyframe_export(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input,
	const std::string& temporary_directory, const std::string& output_directory,
	long long start_ms, long long end_ms, bool single_screenshot,
	const screenshot_options_t& screenshot_options)
{
	if (single_screenshot) {
		run_single_screenshot_export(task, ffmpeg, input, temporary_directory,
			output_directory, start_ms, screenshot_options);
		return;
	}
	if (single_screenshot) cleanup_screenshot_temporary_directory(temporary_directory);
	else cleanup_keyframe_directory(temporary_directory, true);
	if (mkdir(temporary_directory.c_str(), 0755) != 0) {
		finish_task(task, false, single_screenshot ? "截屏启动失败" : "关键帧截屏启动失败",
			acl::last_serror(), -1);
		return;
	}
	const long long source_duration = probe_duration_ms(ffmpeg, input);
	const long long duration = end_ms > start_ms ? end_ms - start_ms
		: (source_duration > start_ms ? source_duration - start_ms : source_duration);
	const std::string start_text = decimal_text(start_ms / 1000.0);
	const std::string duration_text = !single_screenshot && end_ms > start_ms
		? decimal_text((end_ms - start_ms) / 1000.0) : "";
	const std::string output_pattern = local_join_path(temporary_directory,
		single_screenshot ? "screenshot.jpg" : "keyframe_%06d.jpg");
	ACL_ARGV* args = acl_argv_alloc(32);
	acl_argv_add(args, ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y", nullptr);
	if (start_ms > 0) acl_argv_add(args, "-ss", start_text.c_str(), nullptr);
	acl_argv_add(args, "-i", input.c_str(), nullptr);
	if (!duration_text.empty()) acl_argv_add(args, "-t", duration_text.c_str(), nullptr);
	acl_argv_add(args, "-map", "0:v:0", "-an", nullptr);
	if (single_screenshot) {
		acl_argv_add(args, "-frames:v", "1", nullptr);
	} else {
		acl_argv_add(args, "-vf", "select=eq(pict_type\\,I)", "-vsync", "vfr", nullptr);
	}
	acl_argv_add(args, "-q:v", "2", "-progress", "pipe:1", "-nostats",
		output_pattern.c_str(), nullptr);
	const ffmpeg_process_ptr process = start_ffmpeg_process(args);
	if (!process) {
		if (single_screenshot) cleanup_screenshot_temporary_directory(temporary_directory);
		else cleanup_keyframe_directory(temporary_directory, true);
		finish_task(task, false, single_screenshot ? "截屏启动失败" : "关键帧截屏启动失败",
			acl::last_serror(), -1);
		return;
	}
	const int code = wait_transcode_progress(task, *process, duration, 2, 95,
		single_screenshot ? "正在截取当前画面" : "正在截取关键帧",
		97, "正在整理截屏图片");
	if (code != 0 || is_task_cancel_requested(task)) {
		if (single_screenshot) cleanup_screenshot_temporary_directory(temporary_directory);
		else cleanup_keyframe_directory(temporary_directory, true);
		finish_task(task, false, is_task_cancel_requested(task) ? "已取消"
			: (single_screenshot ? "截屏失败" : "关键帧截屏失败"),
			is_task_cancel_requested(task) ? "cancelled"
			: (single_screenshot ? "ffmpeg screenshot failed" : "ffmpeg keyframe export failed"), -1);
		return;
	}
	long long total_size = 0;
	if (single_screenshot) {
		std::string image_name;
		std::string err;
		if (!publish_screenshot(temporary_directory, output_directory,
			total_size, image_name, err)) {
			finish_task(task, false, "截屏失败", err.c_str(), -1);
			return;
		}
		acl::string message;
		message.format("截屏完成：%s", image_name.c_str());
		finish_task(task, true, message.c_str(), "", total_size);
		return;
	}
	long long image_count = 0;
	std::string err;
	if (!publish_keyframe_directory(temporary_directory, output_directory,
		total_size, image_count, err)) {
		finish_task(task, false, "关键帧截屏失败", err.c_str(), -1);
		return;
	}
	acl::string message;
	message.format("关键帧截屏完成，共 %lld 张", image_count);
	finish_task(task, true, message.c_str(), "", total_size);
}

bool subtitle_task_snapshot(const request_t& req, const std::string& scope,
	bool local, transcode_task_snapshot_t& task)
{
	const char* prefix = local ? "subtitle-export-local:" : "subtitle-export:";
	return snapshot_task_by_id(req.getParameter("task_id"), scope, task)
		&& task.file_name.compare(0, strlen(prefix), prefix) == 0;
}

bool keyframe_task_snapshot(const request_t& req, const std::string& scope,
	bool local, transcode_task_snapshot_t& task)
{
	const char* prefix = local ? "keyframe-export-local:" : "keyframe-export:";
	return snapshot_task_by_id(req.getParameter("task_id"), scope, task)
		&& task.file_name.compare(0, strlen(prefix), prefix) == 0;
}

bool image_enhance_name(const std::string& name)
{
	return has_extension(name, {".png", ".jpg", ".jpeg", ".webp", ".bmp",
		".gif", ".heic", ".heif"});
}

std::string unique_image_enhance_output(const std::string& source, bool local,
	const std::string& upload_dir, const std::string& method, int scale,
	const std::string& face_restoration)
{
	const std::string stem = replace_ext(source, "");
	std::string label = method == "codeformer" ? "_codeformer" : (method == "brightness" ? "_brightness" : (method == "red_eye" ? "_red_eye" : (method == "deblur_ai"
		? ("_deblur_ai_x" + std::to_string(scale))
		: (method == "ai" ? ("_ai_x" + std::to_string(scale)) : "_sharpen"))));
	if (method != "codeformer" && face_restoration == "codeformer") label = "_codeformer" + label;
	for (int i = 1; i < 10000; ++i) {
		const std::string suffix = i == 1 ? "" : ("_" + std::to_string(i));
		const std::string candidate = stem + label + suffix + ".png";
		if (!path_exists(local ? candidate : join_upload_path(upload_dir, candidate))) {
			return candidate;
		}
	}
	return stem + label + "_" + std::to_string(g_transcode_seq.load()) + ".png";
}

void run_image_enhance_task(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input,
	const std::string& output, const std::string& temporary_directory,
	const std::string& method, const screenshot_options_t& options,
	const std::string& restormer_mode)
{
	cleanup_screenshot_work_directory(temporary_directory);
	if (mkdir(temporary_directory.c_str(), 0755) != 0) {
		finish_task(task, false, "图片增强启动失败", acl::last_serror(), -1); return;
	}
	const std::string staged_output = local_join_path(temporary_directory, "enhanced.png");
	if (method == "codeformer") {
		const codeformer_runtime_t codeformer = choose_codeformer_runtime();
		if (codeformer.python.empty() || codeformer.runner.empty() || codeformer.repository.empty()) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, "CodeFormer人脸重建不可用",
				"CodeFormer runtime is not installed", -1); return;
		}
		const std::string restored = local_join_path(temporary_directory, "codeformer.png");
		const std::string fidelity = decimal_text(options.face_fidelity / 100.0);
		ACL_ARGV* rebuild = acl_argv_alloc(30);
		acl_argv_add(rebuild, codeformer.python.c_str(), codeformer.runner.c_str(),
			"--repo", codeformer.repository.c_str(), "--input", input.c_str(),
			"--output", restored.c_str(), "--fidelity", fidelity.c_str(),
			"--detector", "retinaface_resnet50", nullptr);
		if (options.codeformer_aligned) acl_argv_add(rebuild, "--inpaint", nullptr);
		else if (options.face_only_center) acl_argv_add(rebuild, "--only-center-face", nullptr);
		const double codeformer_span = options.codeformer_upscale ? 72 : 93;
		if (!run_screenshot_stage(task, rebuild, 3, codeformer_span,
			options.codeformer_aligned ? "CodeFormer正在重建白色遮挡人脸" : "CodeFormer正在检测并修复人脸",
			3 + codeformer_span, options.codeformer_upscale ? "人脸重建完成，准备AI超分" : "正在保存人脸重建图片")) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "CodeFormer人脸重建失败",
				is_task_cancel_requested(task) ? "cancelled" : "CodeFormer inference failed", -1); return;
		}
		if (!options.codeformer_upscale) {
			if (rename(restored.c_str(), staged_output.c_str()) != 0) {
				cleanup_screenshot_work_directory(temporary_directory);
				finish_task(task, false, "CodeFormer人脸重建失败", "CodeFormer output image not found", -1); return;
			}
		} else {
			const std::string frames_in = local_join_path(temporary_directory, "input");
			const std::string frames_out = local_join_path(temporary_directory, "output");
			if (mkdir(frames_in.c_str(), 0755) != 0 || mkdir(frames_out.c_str(), 0755) != 0
				|| rename(restored.c_str(), local_join_path(frames_in, "image.png").c_str()) != 0) {
				cleanup_screenshot_work_directory(temporary_directory);
				finish_task(task, false, "CodeFormer后AI超分失败", "cannot prepare super-resolution input", -1); return;
			}
			const screenshot_ai_runtime_t runtime = choose_screenshot_ai_runtime(options);
			if (runtime.executable.empty() || runtime.model_path.empty()) {
				cleanup_screenshot_work_directory(temporary_directory);
				finish_task(task, false, "CodeFormer后AI超分失败", "selected Real-ESRGAN runtime or model is not installed", -1); return;
			}
			ACL_ARGV* inference = acl_argv_alloc(30);
			if (runtime.coreml) {
				acl_argv_add(inference, runtime.executable.c_str(), "--model", runtime.model_path.c_str(),
					"--input", frames_in.c_str(), "--output", frames_out.c_str(), "--workers", "1",
					"--compute-units", options.compute_units.c_str(), "--tile-batch", "1",
					"--overlap", options.overlap.c_str(), "--cleanup-path", temporary_directory.c_str(), nullptr);
			} else {
				const std::string scale = std::to_string(options.scale);
				const std::string tile = std::to_string(options.tile);
				acl_argv_add(inference, runtime.executable.c_str(), "-i", frames_in.c_str(),
					"-o", frames_out.c_str(), "-n", options.model.c_str(), "-s", scale.c_str(),
					"-m", runtime.model_path.c_str(), "-t", tile.c_str(), "-j", "1:2:2",
					"-f", "png", "-v", nullptr);
			}
			if (!run_screenshot_stage(task, inference, 75, 21, "AI正在放大CodeFormer重建结果", 96, "AI超分完成")) {
				cleanup_screenshot_work_directory(temporary_directory);
				finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "CodeFormer后AI超分失败",
					is_task_cancel_requested(task) ? "cancelled" : "Real-ESRGAN inference failed", -1); return;
			}
			if (rename(local_join_path(frames_out, "image.png").c_str(), staged_output.c_str()) != 0) {
				cleanup_screenshot_work_directory(temporary_directory);
				finish_task(task, false, "CodeFormer后AI超分失败", "AI output image not found", -1); return;
			}
		}
	} else if (method == "brightness") {
		const std::string amount = decimal_text(options.brightness / 100.0);
		const std::string filter = "eq=brightness=" + amount;
		ACL_ARGV* adjust = acl_argv_alloc(24);
		acl_argv_add(adjust, ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y",
			"-i", input.c_str(), "-map", "0:v:0", "-frames:v", "1", "-vf", filter.c_str(),
			"-progress", "pipe:1", "-nostats", staged_output.c_str(), nullptr);
		if (!run_screenshot_stage(task, adjust, 2, 94, "正在调整图片亮度", 97, "正在保存亮度调整图片")) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "图片亮度调整失败",
				is_task_cancel_requested(task) ? "cancelled" : "ffmpeg image brightness adjustment failed", -1);
			return;
		}
	} else if (method == "red_eye") {
		const std::string runtime = choose_red_eye_runtime();
		if (runtime.empty()) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, "自动去红眼不可用", "red-eye correction runtime is not installed", -1); return;
		}
		const std::string strength = std::to_string(options.red_eye_strength);
		ACL_ARGV* correct = acl_argv_alloc(16);
		acl_argv_add(correct, runtime.c_str(), "--input", input.c_str(), "--output",
			staged_output.c_str(), "--strength", strength.c_str(), nullptr);
		if (options.red_eye_only_center) acl_argv_add(correct, "--only-center-face", nullptr);
		if (!run_screenshot_stage(task, correct, 3, 93, "正在检测并校正红眼", 97, "正在保存去红眼图片")) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "自动去红眼失败",
				is_task_cancel_requested(task) ? "cancelled" : "red-eye correction failed", -1); return;
		}
	} else if (method == "sharpen") {
		const std::string amount = decimal_text(options.sharpen / 50.0);
		const std::string filter = "unsharp=5:5:" + amount + ":3:3:0";
		ACL_ARGV* sharpen = acl_argv_alloc(24);
		acl_argv_add(sharpen, ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y",
			"-i", input.c_str(), "-map", "0:v:0", "-frames:v", "1", "-vf", filter.c_str(),
			"-progress", "pipe:1", "-nostats", staged_output.c_str(), nullptr);
		if (!run_screenshot_stage(task, sharpen, 2, 94, "正在锐化图片", 97, "正在保存增强图片")) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "图片锐化失败",
				is_task_cancel_requested(task) ? "cancelled" : "ffmpeg image sharpening failed", -1);
			return;
		}
	} else {
		const std::string frames_in = local_join_path(temporary_directory, "input");
		const std::string frames_out = local_join_path(temporary_directory, "output");
		if (mkdir(frames_in.c_str(), 0755) != 0 || mkdir(frames_out.c_str(), 0755) != 0) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, "AI超分辨率启动失败", "cannot create AI image directories", -1); return;
		}
		const std::string source_png = local_join_path(frames_in, "image.png");
		ACL_ARGV* prepare = acl_argv_alloc(24);
		acl_argv_add(prepare, ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y",
			"-i", input.c_str(), "-map", "0:v:0", "-frames:v", "1", nullptr);
		std::string filter;
		if (options.denoise == 1) filter = "hqdn3d=0.6:0.6:2.4:2.4";
		else if (options.denoise == 2) filter = "hqdn3d=1.0:1.0:3.5:3.5";
		if (options.sharpen > 0) {
			if (!filter.empty()) filter += ",";
			filter += "unsharp=5:5:" + decimal_text(options.sharpen / 100.0) + ":3:3:0";
		}
		if (!filter.empty()) acl_argv_add(prepare, "-vf", filter.c_str(), nullptr);
		acl_argv_add(prepare, "-progress", "pipe:1", "-nostats", source_png.c_str(), nullptr);
		if (!run_screenshot_stage(task, prepare, 2, 20, "正在准备AI超分图片", 22, "准备AI推理")) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "AI超分辨率失败",
				is_task_cancel_requested(task) ? "cancelled" : "AI image preprocessing failed", -1); return;
		}
		std::string inference_input = frames_in;
		const bool run_restormer = method == "deblur_ai" && options.restoration_strength > 0
			&& !(options.face_restoration == "codeformer" && options.codeformer_aligned);
		if (run_restormer) {
			const std::string restored = local_join_path(temporary_directory, "restored");
			if (mkdir(restored.c_str(), 0755) != 0) {
				cleanup_screenshot_work_directory(temporary_directory);
				finish_task(task, false, "Restormer去模糊失败", "cannot create Restormer output directory", -1); return;
			}
			const screenshot_ai_runtime_t restormer = choose_restormer_runtime(restormer_mode);
			if (restormer.executable.empty() || restormer.model_path.empty()) {
				cleanup_screenshot_work_directory(temporary_directory);
				finish_task(task, false, "Restormer去模糊失败", "Restormer runtime or selected model is not installed", -1); return;
			}
			ACL_ARGV* restore = acl_argv_alloc(24);
			acl_argv_add(restore, restormer.executable.c_str(), "--model", restormer.model_path.c_str(),
				"--input", frames_in.c_str(), "--output", restored.c_str(), "--workers", "1",
				"--compute-units", options.compute_units.c_str(), "--tile-batch", "1",
				"--overlap", options.overlap.c_str(), "--cleanup-path", temporary_directory.c_str(), nullptr);
			if (!run_screenshot_stage(task, restore, 22, 28, "Restormer正在去除模糊", 50, "去模糊完成，准备保护原貌")) {
				cleanup_screenshot_work_directory(temporary_directory);
				finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "Restormer去模糊失败",
					is_task_cancel_requested(task) ? "cancelled" : "Restormer inference failed", -1); return;
			}
			// Restormer can invent facial geometry when the source is very small,
			// compressed, or does not match its motion/defocus training domain.
			// Mix its prediction with the source before super-resolution so the
			// original identity and face outline remain the dominant signal.
			if (options.restoration_strength >= 100) {
				inference_input = restored;
			} else {
				const std::string protected_frames = local_join_path(temporary_directory, "protected");
				if (mkdir(protected_frames.c_str(), 0755) != 0) {
					cleanup_screenshot_work_directory(temporary_directory);
					finish_task(task, false, "人脸保护混合失败", "cannot create protected image directory", -1); return;
				}
				const std::string protected_png = local_join_path(protected_frames, "image.png");
				const double restored_weight = options.restoration_strength / 100.0;
				const std::string blend_filter = "blend=all_expr=A*" + decimal_text(1.0 - restored_weight)
					+ "+B*" + decimal_text(restored_weight);
				const std::string restored_png = local_join_path(restored, "image.png");
				ACL_ARGV* blend = acl_argv_alloc(28);
				acl_argv_add(blend, ffmpeg.c_str(), "-hide_banner", "-loglevel", "error", "-y",
					"-i", source_png.c_str(), "-i", restored_png.c_str(), "-filter_complex",
					blend_filter.c_str(), "-frames:v", "1", "-progress", "pipe:1", "-nostats",
					protected_png.c_str(), nullptr);
				if (!run_screenshot_stage(task, blend, 50, 4, "正在保护人脸与原始轮廓", 54, "原貌保护完成")) {
					cleanup_screenshot_work_directory(temporary_directory);
					finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "人脸保护混合失败",
						is_task_cancel_requested(task) ? "cancelled" : "face-preserving blend failed", -1); return;
				}
				inference_input = protected_frames;
			}
		}
		if (options.face_restoration == "codeformer") {
			const codeformer_runtime_t codeformer = choose_codeformer_runtime();
			if (codeformer.python.empty() || codeformer.runner.empty() || codeformer.repository.empty()) {
				cleanup_screenshot_work_directory(temporary_directory);
				finish_task(task, false, "CodeFormer人脸修复不可用",
					"set AICOOL_CODEFORMER_PYTHON and AICOOL_CODEFORMER_REPO", -1); return;
			}
			const std::string face_frames = local_join_path(temporary_directory, "faces");
			if (mkdir(face_frames.c_str(), 0755) != 0) {
				cleanup_screenshot_work_directory(temporary_directory);
				finish_task(task, false, "CodeFormer人脸修复失败", "cannot create face output directory", -1); return;
			}
			const std::string face_input = local_join_path(inference_input, "image.png");
			const std::string face_output = local_join_path(face_frames, "image.png");
			const std::string fidelity = decimal_text(options.face_fidelity / 100.0);
			ACL_ARGV* restore_faces = acl_argv_alloc(30);
			acl_argv_add(restore_faces, codeformer.python.c_str(), codeformer.runner.c_str(),
				"--repo", codeformer.repository.c_str(), "--input", face_input.c_str(),
				"--output", face_output.c_str(), "--fidelity", fidelity.c_str(),
				"--detector", "retinaface_resnet50", nullptr);
			if (options.codeformer_aligned == 1)
				acl_argv_add(restore_faces, "--inpaint", nullptr);
			else if (options.face_only_center == 1)
				acl_argv_add(restore_faces, "--only-center-face", nullptr);
			const double face_start = run_restormer ? 54 : 22;
			const double face_end = run_restormer ? 72 : 52;
			if (!run_screenshot_stage(task, restore_faces, face_start, face_end - face_start,
				options.codeformer_aligned ? "CodeFormer正在重建白色遮挡人脸" : "CodeFormer正在检测并修复人脸",
				face_end, "人脸修复完成，准备AI超分")) {
				cleanup_screenshot_work_directory(temporary_directory);
				finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "CodeFormer人脸修复失败",
					is_task_cancel_requested(task) ? "cancelled" : "CodeFormer inference failed", -1); return;
			}
			inference_input = face_frames;
		}
		const screenshot_ai_runtime_t runtime = choose_screenshot_ai_runtime(options);
		if (runtime.executable.empty() || runtime.model_path.empty()) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, "AI超分辨率失败",
				"selected Real-ESRGAN runtime or model is not installed", -1); return;
		}
		ACL_ARGV* inference = acl_argv_alloc(30);
		if (runtime.coreml) {
			acl_argv_add(inference, runtime.executable.c_str(), "--model", runtime.model_path.c_str(),
				"--input", inference_input.c_str(), "--output", frames_out.c_str(), "--workers", "1",
				"--compute-units", options.compute_units.c_str(), "--tile-batch", "1",
				"--overlap", options.overlap.c_str(), "--cleanup-path", temporary_directory.c_str(), nullptr);
		} else {
			const std::string scale = std::to_string(options.scale);
			const std::string tile = std::to_string(options.tile);
			acl_argv_add(inference, runtime.executable.c_str(), "-i", inference_input.c_str(),
				"-o", frames_out.c_str(), "-n", options.model.c_str(), "-s", scale.c_str(),
				"-m", runtime.model_path.c_str(), "-t", tile.c_str(), "-j", "1:2:2",
				"-f", "png", "-v", nullptr);
		}
		double ai_start = run_restormer ? 54 : 22;
		if (options.face_restoration == "codeformer") ai_start =
			run_restormer ? 72 : 52;
		const double ai_span = 96 - ai_start;
		if (!run_screenshot_stage(task, inference, ai_start, ai_span, "AI正在恢复纹理与细节", 96, "AI超分辨率完成")) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, is_task_cancel_requested(task) ? "已取消" : "AI超分辨率失败",
				is_task_cancel_requested(task) ? "cancelled" : "Real-ESRGAN image inference failed", -1); return;
		}
		const std::string enhanced_png = local_join_path(frames_out, "image.png");
		if (rename(enhanced_png.c_str(), staged_output.c_str()) != 0) {
			cleanup_screenshot_work_directory(temporary_directory);
			finish_task(task, false, "AI超分辨率失败", "AI output image not found", -1); return;
		}
	}
	if (rename(staged_output.c_str(), output.c_str()) != 0) {
		cleanup_screenshot_work_directory(temporary_directory);
		finish_task(task, false, "图片增强失败", "move enhanced image failed", -1); return;
	}
	const long long output_size = file_size_of(output.c_str());
	cleanup_screenshot_work_directory(temporary_directory);
	acl::string message;
	const char* success_message = method == "codeformer" ? "CodeFormer人脸重建完成：%s"
		: (method == "brightness" ? "亮度调整完成：%s"
		: (method == "red_eye" ? "去红眼完成：%s"
		: (method == "deblur_ai" ? "去模糊并超分完成：%s"
		: (method == "ai" ? "AI超分辨率完成：%s" : "图片锐化完成：%s"))));
	message.format(success_message, task->output_name.c_str());
	finish_task(task, true, message.c_str(), "", output_size);
}

bool image_enhance_task_snapshot(const request_t& req, const std::string& scope,
	bool local, transcode_task_snapshot_t& task)
{
	const char* prefix = local ? "image-enhance-local:" : "image-enhance:";
	return snapshot_task_by_id(req.getParameter("task_id"), scope, task)
		&& task.file_name.compare(0, strlen(prefix), prefix) == 0;
}

} // namespace

bool VideoEditAction::run(request_t& req, response_t& res,
	const std::string& upload_dir, bool local,
	const std::string& auxiliary_upload_dir)
{
	const long long start_ms = integer_param(req, "start_ms");
	const long long end_ms = integer_param(req, "end_ms");
	const double speed = decimal_param(req, "speed", 1.0);
	const int volume = static_cast<int>(integer_param(req, "volume", 100));
	const bool muted = integer_param(req, "muted") == 1;
	const int rotate = static_cast<int>(integer_param(req, "rotate"));
	const bool flip_h = integer_param(req, "flip_h") == 1;
	const bool flip_v = integer_param(req, "flip_v") == 1;
	const std::string crop = req.getParameter("crop") ? req.getParameter("crop") : "original";
	const int output_height = static_cast<int>(integer_param(req, "output_height"));
	const std::string audio_mode = req.getParameter("audio_mode") ? req.getParameter("audio_mode") : "keep";
	const long long audio_start_ms = integer_param(req, "audio_start_ms");
	const std::string subtitle_mode = req.getParameter("subtitle_mode") ? req.getParameter("subtitle_mode") : "keep";
	const bool subtitle_uploaded = req.getParameter("subtitle_file_source")
		&& strcmp(req.getParameter("subtitle_file_source"), "upload") == 0;
	const std::string subtitle_import_mode = req.getParameter("subtitle_import_mode")
		? req.getParameter("subtitle_import_mode") : "reencode";
	const long long subtitle_start_ms = integer_param(req, "subtitle_start_ms");
	if (start_ms < 0 || (end_ms > 0 && end_ms - start_ms < 100)
		|| speed < 0.25 || speed > 4.0 || volume < 0 || volume > 200
		|| (rotate != 0 && rotate != 90 && rotate != 180 && rotate != 270)
		|| (crop != "original" && crop != "16:9" && crop != "9:16" && crop != "1:1")
		|| (output_height != 0 && output_height != 480 && output_height != 720 && output_height != 1080)
		|| (audio_mode != "keep" && audio_mode != "remove" && audio_mode != "replace")
		|| (subtitle_mode != "keep" && subtitle_mode != "remove" && subtitle_mode != "replace")
		|| (subtitle_import_mode != "fast" && subtitle_import_mode != "reencode")
		|| audio_start_ms < 0 || subtitle_start_ms < 0) {
		json_error(res, 400, "invalid video edit options", req.isKeepAlive());
		return true;
	}
	if (subtitle_import_mode == "fast" && (subtitle_mode != "replace"
		|| std::fabs(speed - 1.0) > 0.0001 || volume != 100 || muted
		|| rotate != 0 || flip_h || flip_v || crop != "original"
		|| output_height != 0 || audio_mode != "keep")) {
		json_error(res, 400,
			"快速封装不能同时应用画面、变速或音频编辑，请选择兼容转码",
			req.isKeepAlive());
		return true;
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
		if (!normalize_relative_path(req.getParameter("file") ? req.getParameter("file") : "",
			source, err, false) || !resolve_upload_regular_file_path(upload_dir, source, source)
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
	std::string audio_path;
	std::string subtitle_path;
	int auxiliary_status = 500;
	if (audio_mode == "replace" && !resolve_auxiliary_file(req, "audio_file", upload_dir,
		local, { ".mp3", ".m4a", ".aac", ".wav", ".ogg", ".flac" },
		audio_path, err, auxiliary_status)) {
		json_error(res, auxiliary_status, err.c_str(), req.isKeepAlive()); return true;
	}
	if (subtitle_mode == "replace" && !resolve_auxiliary_file(req, "subtitle_file", upload_dir,
		local, { ".srt", ".vtt", ".ass", ".ssa" },
		subtitle_path, err, auxiliary_status, auxiliary_upload_dir, subtitle_uploaded)) {
		json_error(res, auxiliary_status, err.c_str(), req.isKeepAlive()); return true;
	}
	const std::string prefix = local ? "video-edit-local:" : "video-edit:";
	const std::string task_file = prefix + source;
	const std::string key = scoped_task_key(upload_dir, task_file);
	{
		std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
		const auto running = g_running_task_by_file.find(key);
		if (running != g_running_task_by_file.end()) {
			const auto found = g_transcode_tasks.find(running->second);
			if (found != g_transcode_tasks.end() && !found->second->done) {
				acl::json json; acl::json_node& root = json.create_node();
				root.add_bool("ok", true); root.add_bool("started", false);
				root.add_text("task_id", found->second->id.c_str());
				root.add_text("name", found->second->output_name.c_str());
				return sendJson(res, 200, root, req.isKeepAlive());
			}
		}
	}

	const std::string output_name = unique_edit_output(source, local, upload_dir);
	const std::string input_path = local ? source : join_upload_path(upload_dir, source);
	const std::string output_path = local ? output_name : join_upload_path(upload_dir, output_name);
	acl::string temp;
	temp.format("%s/.video_edit_tmp.%u.%lu.mp4", local_parent_path(output_path).c_str(),
		static_cast<unsigned>(getpid()), static_cast<unsigned long>(g_transcode_seq.load()));
	auto task = std::make_shared<transcode_task_t>();
	task->id = make_task_id(); task->scope = upload_dir; task->file_name = task_file;
	task->output_name = output_name;
	task->message = subtitle_import_mode == "fast" ? "等待快速添加字幕" : "等待导出剪辑";
	task->local = local;
	{
		std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
		g_transcode_tasks[task->id] = task; g_running_task_by_file[key] = task->id;
	}
	const std::string temporary = temp.c_str();
	go[task, ffmpeg, input_path, temporary, output_path, start_ms, end_ms, speed,
		volume, muted, rotate, flip_h, flip_v, crop, output_height, audio_mode,
		audio_path, audio_start_ms, subtitle_mode, subtitle_path, subtitle_start_ms,
		subtitle_import_mode] {
		acl::gofiber_wait_thread([task, ffmpeg, input_path, temporary, output_path,
			start_ms, end_ms, speed, volume, muted, rotate, flip_h, flip_v, crop,
			output_height, audio_mode, audio_path, audio_start_ms, subtitle_mode,
			subtitle_path, subtitle_start_ms, subtitle_import_mode] {
			run_video_edit(task, ffmpeg, input_path, temporary, output_path, start_ms,
				end_ms, speed, volume, muted, rotate, flip_h, flip_v, crop, output_height,
				audio_mode, audio_path, audio_start_ms, subtitle_mode, subtitle_path,
				subtitle_start_ms, subtitle_import_mode);
		});
	};
	acl::json json; acl::json_node& root = json.create_node();
	root.add_bool("ok", true); root.add_bool("started", true);
	root.add_text("task_id", task->id.c_str()); root.add_text("name", output_name.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool VideoEditAction::progress(request_t& req, response_t& res,
	const std::string& upload_dir, bool local)
{
	transcode_task_snapshot_t task;
	if (!edit_task_snapshot(req, upload_dir, local, task)) {
		json_error(res, 404, "video edit task not found", req.isKeepAlive()); return true;
	}
	acl::json json; acl::json_node& root = json.create_node();
	root.add_bool("ok", true); root.add_text("task_id", task.id.c_str());
	root.add_text("name", task.output_name.c_str()); root.add_bool("done", task.done);
	root.add_bool("success", task.success); root.add_bool("cancel_requested", task.cancel_requested);
	root.add_number("progress", static_cast<long long>(task.progress));
	root.add_text("message", task.message.c_str());
	if (!task.error.empty()) root.add_text("error", task.error.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool VideoEditAction::cancel(request_t& req, response_t& res,
	const std::string& upload_dir, bool local)
{
	transcode_task_snapshot_t task;
	if (!edit_task_snapshot(req, upload_dir, local, task)) {
		json_error(res, 404, "video edit task not found", req.isKeepAlive()); return true;
	}
	bool signal_sent = false;
	if (!request_cancel_task(task.id.c_str(), upload_dir, task, signal_sent)) {
		json_error(res, 404, "video edit task not found", req.isKeepAlive()); return true;
	}
	acl::json json; acl::json_node& root = json.create_node();
	root.add_bool("ok", true); root.add_text("task_id", task.id.c_str());
	root.add_bool("cancel_requested", true); root.add_bool("signal_sent", signal_sent);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool VideoEditAction::exportSubtitle(request_t& req, response_t& res,
	const std::string& upload_dir, bool local)
{
	const long long start_ms = integer_param(req, "start_ms");
	const long long end_ms = integer_param(req, "end_ms");
	if (start_ms < 0 || (end_ms > 0 && end_ms - start_ms < 100)) {
		json_error(res, 400, "invalid subtitle export range", req.isKeepAlive()); return true;
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
		if (!normalize_relative_path(req.getParameter("file") ? req.getParameter("file") : "",
			source, err, false) || !resolve_upload_regular_file_path(upload_dir, source, source)
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
	const std::string source_path = local ? source : join_upload_path(upload_dir, source);
	const std::string prefix = local ? "subtitle-export-local:" : "subtitle-export:";
	// One fixed VTT sidecar is maintained per video, regardless of the selected range.
	const std::string task_file = prefix + source;
	const std::string task_key = scoped_task_key(upload_dir, task_file);
	{
		std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
		const auto running = g_running_task_by_file.find(task_key);
		if (running != g_running_task_by_file.end()) {
			const auto found = g_transcode_tasks.find(running->second);
			if (found != g_transcode_tasks.end() && !found->second->done) {
				acl::json json; acl::json_node& root = json.create_node();
				root.add_bool("ok", true); root.add_bool("started", false);
				root.add_text("task_id", found->second->id.c_str());
				root.add_text("name", found->second->output_name.c_str());
				return sendJson(res, 200, root, req.isKeepAlive());
			}
		}
	}
	std::string subtitle_source_path = source_path;
	if (!probe_has_subtitle_stream(ffmpeg, source_path)) {
		const char* subtitle_extensions[] = { ".vtt", ".srt", ".ass", ".ssa" };
		std::string sidecar_name;
		std::string sidecar_path;
		for (size_t i = 0; i < sizeof(subtitle_extensions) / sizeof(subtitle_extensions[0]); ++i) {
			const std::string candidate_name = replace_ext(source, subtitle_extensions[i]);
			const std::string candidate_path = local ? candidate_name : join_upload_path(upload_dir, candidate_name);
			if (file_size_of(candidate_path.c_str()) > 0) {
				sidecar_name = candidate_name;
				sidecar_path = candidate_path;
				break;
			}
		}
		if (sidecar_path.empty()) {
			json_error(res, 422,
				"视频没有可导出的内嵌或外挂字幕；画面中可见的文字可能是硬字幕，需要OCR识别",
				req.isKeepAlive()); return true;
		}
		int sidecar_lock_status = 500;
		const bool sidecar_allowed = local
			? ensure_local_video_transcode_lock_policy(upload_dir, sidecar_name, err, sidecar_lock_status)
			: ensure_remote_video_transcode_lock_policy(upload_dir, sidecar_name, err, sidecar_lock_status);
		if (!sidecar_allowed) {
			json_error(res, sidecar_lock_status, err.c_str(), req.isKeepAlive()); return true;
		}
		subtitle_source_path = sidecar_path;
	}
	const std::string output_name = replace_ext(source, ".vtt");
	const std::string output_path = local ? output_name : join_upload_path(upload_dir, output_name);
	acl::string temp; temp.format("%s/.subtitle_export_tmp.%u.%lu.vtt",
		local_parent_path(output_path).c_str(), static_cast<unsigned>(getpid()),
		static_cast<unsigned long>(g_transcode_seq.load()));
	auto task = std::make_shared<transcode_task_t>();
	task->id = make_task_id(); task->scope = upload_dir; task->file_name = task_file;
	task->output_name = output_name; task->message = "等待导出字幕"; task->local = local;
	{
		std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
		g_transcode_tasks[task->id] = task;
		g_running_task_by_file[task_key] = task->id;
	}
	const std::string temporary = temp.c_str();
	go[task, ffmpeg, subtitle_source_path, temporary, output_path, start_ms, end_ms] {
		acl::gofiber_wait_thread([task, ffmpeg, subtitle_source_path, temporary, output_path, start_ms, end_ms] {
			run_subtitle_export(task, ffmpeg, subtitle_source_path, temporary, output_path, start_ms, end_ms);
		});
	};
	acl::json json; acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", task->id.c_str()); root.add_text("name", output_name.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool VideoEditAction::subtitleProgress(request_t& req, response_t& res,
	const std::string& upload_dir, bool local)
{
	transcode_task_snapshot_t task;
	if (!subtitle_task_snapshot(req, upload_dir, local, task)) {
		json_error(res, 404, "subtitle export task not found", req.isKeepAlive()); return true;
	}
	acl::json json; acl::json_node& root = json.create_node();
	root.add_bool("ok", true); root.add_text("task_id", task.id.c_str());
	root.add_text("name", task.output_name.c_str()); root.add_bool("done", task.done);
	root.add_bool("success", task.success); root.add_bool("cancel_requested", task.cancel_requested);
	root.add_number("progress", static_cast<long long>(task.progress)); root.add_text("message", task.message.c_str());
	if (!task.error.empty()) root.add_text("error", task.error.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool VideoEditAction::subtitleCancel(request_t& req, response_t& res,
	const std::string& upload_dir, bool local)
{
	transcode_task_snapshot_t task;
	if (!subtitle_task_snapshot(req, upload_dir, local, task)) {
		json_error(res, 404, "subtitle export task not found", req.isKeepAlive()); return true;
	}
	bool signal_sent = false;
	if (!request_cancel_task(task.id.c_str(), upload_dir, task, signal_sent)) {
		json_error(res, 404, "subtitle export task not found", req.isKeepAlive()); return true;
	}
	acl::json json; acl::json_node& root = json.create_node(); root.add_bool("ok", true);
	root.add_bool("cancel_requested", true); root.add_bool("signal_sent", signal_sent);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool VideoEditAction::exportKeyframes(request_t& req, response_t& res,
	const std::string& upload_dir, bool local)
{
	const long long start_ms = integer_param(req, "start_ms");
	const long long end_ms = integer_param(req, "end_ms");
	const bool single_screenshot = integer_param(req, "single") != 0;
	screenshot_options_t screenshot_options;
	if (req.getParameter("enhance_mode")) screenshot_options.mode = req.getParameter("enhance_mode");
	if (req.getParameter("ai_model")) screenshot_options.model = req.getParameter("ai_model");
	if (req.getParameter("ai_compute_units")) screenshot_options.compute_units = req.getParameter("ai_compute_units");
	if (req.getParameter("ai_overlap")) screenshot_options.overlap = req.getParameter("ai_overlap");
	screenshot_options.scale = static_cast<int>(integer_param(req, "ai_scale", 4));
	screenshot_options.denoise = static_cast<int>(integer_param(req, "ai_denoise", 0));
	screenshot_options.sharpen = static_cast<int>(integer_param(req, "sharpen", 35));
	screenshot_options.quality = static_cast<int>(integer_param(req, "image_quality", 95));
	screenshot_options.tile = static_cast<int>(integer_param(req, "ai_tile", 0));
	if (start_ms < 0 || (!single_screenshot && end_ms > 0 && end_ms - start_ms < 100)) {
		json_error(res, 400, "invalid keyframe export range", req.isKeepAlive()); return true;
	}
	const bool valid_ai_model = screenshot_options.model == "realesrgan-x4plus"
		|| screenshot_options.model == "realesr-animevideov3"
		|| screenshot_options.model == "coreml-x2plus"
		|| screenshot_options.model == "coreml-general-x4v3"
		|| screenshot_options.model == "coreml-general-x4v3-w8a8"
		|| screenshot_options.model == "coreml-x4plus-int8";
	const bool coreml_scale_valid = screenshot_options.model != "coreml-x2plus"
		? screenshot_options.scale == 4 : screenshot_options.scale == 2;
	if (single_screenshot && ((screenshot_options.mode != "original"
		&& screenshot_options.mode != "sharpen" && screenshot_options.mode != "ai")
		|| !valid_ai_model || (screenshot_options.scale != 2 && screenshot_options.scale != 4)
		|| (screenshot_options.model.compare(0, 7, "coreml-") == 0 && !coreml_scale_valid)
		|| (screenshot_options.model == "realesrgan-x4plus" && screenshot_options.scale != 4)
		|| screenshot_options.denoise < 0 || screenshot_options.denoise > 2
		|| screenshot_options.sharpen < 0 || screenshot_options.sharpen > 100
		|| screenshot_options.quality < 70 || screenshot_options.quality > 100
		|| (screenshot_options.tile != 0 && screenshot_options.tile != 128
			&& screenshot_options.tile != 256 && screenshot_options.tile != 512)
		|| (screenshot_options.compute_units != "auto" && screenshot_options.compute_units != "gpu"
			&& screenshot_options.compute_units != "ane" && screenshot_options.compute_units != "cpu")
		|| (screenshot_options.overlap != "low" && screenshot_options.overlap != "balanced"
			&& screenshot_options.overlap != "quality"))) {
		json_error(res, 400, "invalid screenshot enhancement options", req.isKeepAlive()); return true;
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
		if (!normalize_relative_path(req.getParameter("file") ? req.getParameter("file") : "",
			source, err, false) || !resolve_upload_regular_file_path(upload_dir, source, source)
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
	const std::string prefix = local ? "keyframe-export-local:" : "keyframe-export:";
	const std::string task_file = prefix + source;
	const std::string task_key = scoped_task_key(upload_dir, task_file);
	{
		std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
		const auto running = g_running_task_by_file.find(task_key);
		if (running != g_running_task_by_file.end()) {
			const auto found = g_transcode_tasks.find(running->second);
			if (found != g_transcode_tasks.end() && !found->second->done) {
				acl::json json; acl::json_node& root = json.create_node();
				root.add_bool("ok", true); root.add_bool("started", false);
				root.add_text("task_id", found->second->id.c_str());
				root.add_text("name", found->second->output_name.c_str());
				return sendJson(res, 200, root, req.isKeepAlive());
			}
		}
	}
	const std::string input_path = local ? source : join_upload_path(upload_dir, source);
	const std::string output_name = replace_ext(source, "");
	const std::string output_path = local ? output_name : join_upload_path(upload_dir, output_name);
	acl::string temporary;
	temporary.format("%s/.keyframe_export_tmp.%u.%lu",
		local_parent_path(output_path).c_str(), static_cast<unsigned>(getpid()),
		static_cast<unsigned long>(g_transcode_seq.load()));
	auto task = std::make_shared<transcode_task_t>();
	task->id = make_task_id(); task->scope = upload_dir; task->file_name = task_file;
	task->output_name = output_name;
	task->message = single_screenshot
		? (screenshot_options.mode == "ai" ? "等待AI增强截屏" : "等待截取当前画面")
		: "等待截取关键帧";
	task->local = local;
	{
		std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
		g_transcode_tasks[task->id] = task;
		g_running_task_by_file[task_key] = task->id;
	}
	const std::string temporary_directory = temporary.c_str();
	go[task, ffmpeg, input_path, temporary_directory, output_path, start_ms, end_ms,
		single_screenshot, screenshot_options] {
		acl::gofiber_wait_thread([task, ffmpeg, input_path, temporary_directory,
			output_path, start_ms, end_ms, single_screenshot, screenshot_options] {
			run_keyframe_export(task, ffmpeg, input_path, temporary_directory,
				output_path, start_ms, end_ms, single_screenshot, screenshot_options);
		});
	};
	acl::json json; acl::json_node& root = json.create_node();
	root.add_bool("ok", true); root.add_bool("started", true);
	root.add_text("task_id", task->id.c_str()); root.add_text("name", output_name.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool VideoEditAction::keyframeProgress(request_t& req, response_t& res,
	const std::string& upload_dir, bool local)
{
	transcode_task_snapshot_t task;
	if (!keyframe_task_snapshot(req, upload_dir, local, task)) {
		json_error(res, 404, "keyframe export task not found", req.isKeepAlive()); return true;
	}
	acl::json json; acl::json_node& root = json.create_node();
	root.add_bool("ok", true); root.add_text("task_id", task.id.c_str());
	root.add_text("name", task.output_name.c_str()); root.add_bool("done", task.done);
	root.add_bool("success", task.success); root.add_bool("cancel_requested", task.cancel_requested);
	root.add_number("progress", static_cast<long long>(task.progress)); root.add_text("message", task.message.c_str());
	if (!task.error.empty()) root.add_text("error", task.error.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool VideoEditAction::keyframeCancel(request_t& req, response_t& res,
	const std::string& upload_dir, bool local)
{
	transcode_task_snapshot_t task;
	if (!keyframe_task_snapshot(req, upload_dir, local, task)) {
		json_error(res, 404, "keyframe export task not found", req.isKeepAlive()); return true;
	}
	bool signal_sent = false;
	if (!request_cancel_task(task.id.c_str(), upload_dir, task, signal_sent)) {
		json_error(res, 404, "keyframe export task not found", req.isKeepAlive()); return true;
	}
	acl::json json; acl::json_node& root = json.create_node(); root.add_bool("ok", true);
	root.add_bool("cancel_requested", true); root.add_bool("signal_sent", signal_sent);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool ImageEnhanceAction::run(request_t& req, response_t& res,
	const std::string& upload_dir, bool local)
{
	const std::string method = req.getParameter("method") ? req.getParameter("method") : "sharpen";
	if (method != "sharpen" && method != "ai" && method != "deblur_ai"
		&& method != "red_eye" && method != "brightness" && method != "codeformer") {
		json_error(res, 400, "invalid image enhancement method", req.isKeepAlive()); return true;
	}
	std::string source;
	std::string err;
	if (local) {
		if (!normalize_local_video_path(req.getParameter("path"), source, err)) {
			json_error(res, 400, err.c_str(), req.isKeepAlive()); return true;
		}
		struct stat st{};
		if (stat(source.c_str(), &st) != 0 || !S_ISREG(st.st_mode)
			|| !image_enhance_name(local_base_name(source))) {
			json_error(res, 404, "source image not found", req.isKeepAlive()); return true;
		}
		int status = 500;
		if (!ensure_local_video_transcode_lock_policy(upload_dir, source, err, status)) {
			json_error(res, status, err.c_str(), req.isKeepAlive()); return true;
		}
	} else {
		if (!normalize_relative_path(req.getParameter("file") ? req.getParameter("file") : "",
			source, err, false) || !resolve_upload_regular_file_path(upload_dir, source, source)
			|| !image_enhance_name(base_name_from_relative_path(source))) {
			json_error(res, 404, "source image not found", req.isKeepAlive()); return true;
		}
		int status = 500;
		if (!ensure_remote_video_transcode_lock_policy(upload_dir, source, err, status)) {
			json_error(res, status, err.c_str(), req.isKeepAlive()); return true;
		}
	}
	const std::string ffmpeg = choose_ffmpeg_path();
	if (ffmpeg.empty()) {
		json_error(res, 503, "ffmpeg not found", req.isKeepAlive()); return true;
	}
	screenshot_options_t options;
	options.sharpen = 35;
#if defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))
	options.model = "coreml-x2plus";
	options.scale = 2;
#else
	options.model = "realesrgan-x4plus";
	options.scale = 4;
#endif
	std::string restormer_mode = req.getParameter("restormer_mode")
		? req.getParameter("restormer_mode") : "motion";
	if (method == "codeformer") {
		options.face_fidelity = static_cast<int>(integer_param(req, "face_fidelity", 50));
		options.face_only_center = static_cast<int>(integer_param(req, "face_only_center", 1));
		options.codeformer_aligned = static_cast<int>(integer_param(req, "codeformer_aligned", 1));
		options.codeformer_upscale = static_cast<int>(integer_param(req, "codeformer_upscale", 0));
		if (options.face_fidelity < 0 || options.face_fidelity > 100
			|| (options.face_only_center != 0 && options.face_only_center != 1)
			|| (options.codeformer_aligned != 0 && options.codeformer_aligned != 1)
			|| (options.codeformer_upscale != 0 && options.codeformer_upscale != 1)) {
			json_error(res, 400, "invalid CodeFormer reconstruction options", req.isKeepAlive()); return true;
		}
		const codeformer_runtime_t codeformer = choose_codeformer_runtime();
		if (codeformer.python.empty() || codeformer.runner.empty() || codeformer.repository.empty()) {
			json_error(res, 503,
				"CodeFormer运行环境未安装，请执行bash tools/setup_codeformer_runtime.sh或配置AICOOL_CODEFORMER_PYTHON和AICOOL_CODEFORMER_REPO",
				req.isKeepAlive()); return true;
		}
		if (options.codeformer_aligned) {
			const std::string inpainting_model = local_join_path(
				local_join_path(local_join_path(codeformer.repository, "weights"), "CodeFormer"),
				"codeformer_inpainting.pth");
			if (access(inpainting_model.c_str(), R_OK) != 0) {
				json_error(res, 503,
					"CodeFormer遮挡修复模型未安装，请执行python3 tools/download_codeformer_models.py",
					req.isKeepAlive()); return true;
			}
		}
		if (options.codeformer_upscale) {
			const screenshot_ai_runtime_t runtime = choose_screenshot_ai_runtime(options);
			if (runtime.executable.empty() || runtime.model_path.empty()) {
				json_error(res, 503, "selected Real-ESRGAN runtime or model is not installed",
					req.isKeepAlive()); return true;
			}
		}
	}
	if (method == "brightness") {
		options.brightness = static_cast<int>(integer_param(req, "brightness", 20));
		if (options.brightness < -100 || options.brightness > 100) {
			json_error(res, 400, "invalid image brightness", req.isKeepAlive()); return true;
		}
	}
	if (method == "red_eye") {
		options.red_eye_strength = static_cast<int>(integer_param(req, "red_eye_strength", 80));
		options.red_eye_only_center = static_cast<int>(integer_param(req, "red_eye_only_center", 0));
		if (options.red_eye_strength < 0 || options.red_eye_strength > 100
			|| (options.red_eye_only_center != 0 && options.red_eye_only_center != 1)) {
			json_error(res, 400, "invalid red-eye correction options", req.isKeepAlive()); return true;
		}
		if (choose_red_eye_runtime().empty()) {
			json_error(res, 503, "自动去红眼仅支持已安装Vision运行组件的macOS服务端",
				req.isKeepAlive()); return true;
		}
	}
	if (method == "ai" || method == "deblur_ai") {
		if (req.getParameter("ai_model")) options.model = req.getParameter("ai_model");
		options.scale = static_cast<int>(integer_param(req, "ai_scale", options.scale));
		options.denoise = static_cast<int>(integer_param(req, "ai_denoise", 0));
		options.sharpen = static_cast<int>(integer_param(req, "ai_sharpen", 0));
		options.restoration_strength = static_cast<int>(integer_param(req, "restormer_strength", 35));
		options.face_restoration = req.getParameter("face_restoration")
			? req.getParameter("face_restoration") : "none";
		options.face_fidelity = static_cast<int>(integer_param(req, "face_fidelity", 90));
		options.face_only_center = static_cast<int>(integer_param(req, "face_only_center", 1));
		options.codeformer_aligned = static_cast<int>(integer_param(req, "codeformer_aligned", 0));
		options.tile = static_cast<int>(integer_param(req, "ai_tile", 0));
		if (req.getParameter("ai_compute_units")) options.compute_units = req.getParameter("ai_compute_units");
		if (req.getParameter("ai_overlap")) options.overlap = req.getParameter("ai_overlap");
		const bool valid_model = options.model == "realesrgan-x4plus"
			|| options.model == "realesr-animevideov3"
			|| options.model == "coreml-x2plus"
			|| options.model == "coreml-general-x4v3"
			|| options.model == "coreml-general-x4v3-w8a8"
			|| options.model == "coreml-x4plus-int8";
		const bool fixed_x2 = options.model == "coreml-x2plus";
		const bool fixed_x4 = options.model == "realesrgan-x4plus"
			|| (options.model.compare(0, 7, "coreml-") == 0 && !fixed_x2);
		if (!valid_model || (options.scale != 2 && options.scale != 4)
			|| (fixed_x2 && options.scale != 2) || (fixed_x4 && options.scale != 4)
			|| options.denoise < 0 || options.denoise > 2
			|| options.sharpen < 0 || options.sharpen > 50
			|| options.restoration_strength < 0 || options.restoration_strength > 100
			|| (options.face_restoration != "none" && options.face_restoration != "codeformer")
			|| options.face_fidelity < 0 || options.face_fidelity > 100
			|| (options.face_only_center != 0 && options.face_only_center != 1)
			|| (options.codeformer_aligned != 0 && options.codeformer_aligned != 1)
			|| (options.tile != 0 && options.tile != 128 && options.tile != 256 && options.tile != 512)
			|| (options.compute_units != "auto" && options.compute_units != "gpu"
				&& options.compute_units != "ane" && options.compute_units != "cpu")
			|| (options.overlap != "low" && options.overlap != "balanced"
				&& options.overlap != "quality")) {
			json_error(res, 400, "invalid AI image enhancement options", req.isKeepAlive()); return true;
		}
		if (method == "deblur_ai" && restormer_mode != "motion" && restormer_mode != "defocus") {
			json_error(res, 400, "invalid Restormer deblur mode", req.isKeepAlive()); return true;
		}
		const screenshot_ai_runtime_t runtime = choose_screenshot_ai_runtime(options);
		if (runtime.executable.empty() || runtime.model_path.empty()) {
			json_error(res, 503, "selected Real-ESRGAN runtime or model is not installed",
				req.isKeepAlive()); return true;
		}
		if (method == "deblur_ai" && options.restoration_strength > 0
			&& !(options.face_restoration == "codeformer" && options.codeformer_aligned)) {
			const screenshot_ai_runtime_t restormer = choose_restormer_runtime(restormer_mode);
			if (restormer.executable.empty() || restormer.model_path.empty()) {
				json_error(res, 503, "Restormer runtime or selected model is not installed",
					req.isKeepAlive()); return true;
			}
		}
		if (options.face_restoration == "codeformer") {
			const codeformer_runtime_t codeformer = choose_codeformer_runtime();
			if (codeformer.python.empty() || codeformer.runner.empty() || codeformer.repository.empty()) {
				json_error(res, 503,
					"CodeFormer运行环境未安装，请执行bash tools/setup_codeformer_runtime.sh或配置AICOOL_CODEFORMER_PYTHON和AICOOL_CODEFORMER_REPO",
					req.isKeepAlive()); return true;
			}
			if (options.codeformer_aligned) {
				const std::string inpainting_model = local_join_path(
					local_join_path(local_join_path(codeformer.repository, "weights"), "CodeFormer"),
					"codeformer_inpainting.pth");
				if (access(inpainting_model.c_str(), R_OK) != 0) {
					json_error(res, 503,
						"CodeFormer遮挡修复模型未安装，请执行python3 tools/download_codeformer_models.py",
						req.isKeepAlive()); return true;
				}
			}
		}
	}
	const std::string prefix = local ? "image-enhance-local:" : "image-enhance:";
	const std::string task_file = prefix + source;
	const std::string task_key = scoped_task_key(upload_dir, task_file);
	{
		std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
		const auto running = g_running_task_by_file.find(task_key);
		if (running != g_running_task_by_file.end()) {
			const auto found = g_transcode_tasks.find(running->second);
			if (found != g_transcode_tasks.end() && !found->second->done) {
				acl::json json; acl::json_node& root = json.create_node();
				root.add_bool("ok", true); root.add_bool("started", false);
				root.add_text("task_id", found->second->id.c_str());
				root.add_text("name", found->second->output_name.c_str());
				return sendJson(res, 200, root, req.isKeepAlive());
			}
		}
	}
	const std::string output_name = unique_image_enhance_output(source, local,
		upload_dir, method, options.scale, options.face_restoration);
	const std::string input_path = local ? source : join_upload_path(upload_dir, source);
	const std::string output_path = local ? output_name : join_upload_path(upload_dir, output_name);
	acl::string temporary;
	temporary.format("%s/.image_enhance_tmp.%u.%lu",
		local_parent_path(output_path).c_str(), static_cast<unsigned>(getpid()),
		static_cast<unsigned long>(g_transcode_seq.load()));
	auto task = std::make_shared<transcode_task_t>();
	task->id = make_task_id(); task->scope = upload_dir; task->file_name = task_file;
	task->output_name = output_name; task->local = local;
	task->message = method == "codeformer"
		? (options.codeformer_aligned ? "等待CodeFormer白色遮挡人脸重建" : "等待CodeFormer普通人脸修复")
		: (method == "brightness" ? "等待调整图片亮度"
		: (method == "red_eye" ? "等待自动检测并校正红眼"
		: (options.face_restoration == "codeformer" ? "等待CodeFormer人脸修复"
		: (method == "deblur_ai" ? "等待Restormer去模糊处理"
		: (method == "ai" ? "等待AI超分辨率处理" : "等待图片锐化处理")))));
	{
		std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
		g_transcode_tasks[task->id] = task;
		g_running_task_by_file[task_key] = task->id;
	}
	const std::string temporary_directory = temporary.c_str();
	go[task, ffmpeg, input_path, output_path, temporary_directory, method, options, restormer_mode] {
		acl::gofiber_wait_thread([task, ffmpeg, input_path, output_path,
			temporary_directory, method, options, restormer_mode] {
			run_image_enhance_task(task, ffmpeg, input_path, output_path,
				temporary_directory, method, options, restormer_mode);
		});
	};
	acl::json json; acl::json_node& root = json.create_node();
	root.add_bool("ok", true); root.add_bool("started", true);
	root.add_text("task_id", task->id.c_str()); root.add_text("name", output_name.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool ImageEnhanceAction::progress(request_t& req, response_t& res,
	const std::string& upload_dir, bool local)
{
	transcode_task_snapshot_t task;
	if (!image_enhance_task_snapshot(req, upload_dir, local, task)) {
		json_error(res, 404, "image enhancement task not found", req.isKeepAlive()); return true;
	}
	acl::json json; acl::json_node& root = json.create_node();
	root.add_bool("ok", true); root.add_text("task_id", task.id.c_str());
	root.add_text("name", task.output_name.c_str()); root.add_bool("done", task.done);
	root.add_bool("success", task.success); root.add_bool("cancel_requested", task.cancel_requested);
	root.add_number("progress", static_cast<long long>(task.progress));
	root.add_text("message", task.message.c_str());
	if (!task.error.empty()) root.add_text("error", task.error.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool ImageEnhanceAction::cancel(request_t& req, response_t& res,
	const std::string& upload_dir, bool local)
{
	transcode_task_snapshot_t task;
	if (!image_enhance_task_snapshot(req, upload_dir, local, task)) {
		json_error(res, 404, "image enhancement task not found", req.isKeepAlive()); return true;
	}
	bool signal_sent = false;
	if (!request_cancel_task(task.id.c_str(), upload_dir, task, signal_sent)) {
		json_error(res, 404, "image enhancement task not found", req.isKeepAlive()); return true;
	}
	acl::json json; acl::json_node& root = json.create_node(); root.add_bool("ok", true);
	root.add_bool("cancel_requested", true); root.add_bool("signal_sent", signal_sent);
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

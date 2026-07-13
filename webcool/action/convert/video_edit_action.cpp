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
	finish_task(task, true, fast_subtitle_import ? "快速添加字幕完成" : "视频剪辑完成",
		"", file_size_of(output.c_str()));
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

bool subtitle_task_snapshot(const request_t& req, const std::string& scope,
	bool local, transcode_task_snapshot_t& task)
{
	const char* prefix = local ? "subtitle-export-local:" : "subtitle-export:";
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
	const std::string stem = replace_ext(source, "");
	std::string output_name;
	for (int i = 1; i < 10000; ++i) {
		const std::string suffix = i == 1 ? "" : ("_" + std::to_string(i));
		const std::string candidate = stem + "_subtitle" + suffix + ".vtt";
		if (!path_exists(local ? candidate : join_upload_path(upload_dir, candidate))) {
			output_name = candidate; break;
		}
	}
	if (output_name.empty()) output_name = stem + "_subtitle_" + std::to_string(g_transcode_seq.load()) + ".vtt";
	const std::string output_path = local ? output_name : join_upload_path(upload_dir, output_name);
	acl::string temp; temp.format("%s/.subtitle_export_tmp.%u.%lu.vtt",
		local_parent_path(output_path).c_str(), static_cast<unsigned>(getpid()),
		static_cast<unsigned long>(g_transcode_seq.load()));
	const std::string prefix = local ? "subtitle-export-local:" : "subtitle-export:";
	const std::string task_file = prefix + source + ":" + std::to_string(start_ms) + ":" + std::to_string(end_ms);
	auto task = std::make_shared<transcode_task_t>();
	task->id = make_task_id(); task->scope = upload_dir; task->file_name = task_file;
	task->output_name = output_name; task->message = "等待导出字幕"; task->local = local;
	{
		std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
		g_transcode_tasks[task->id] = task;
		g_running_task_by_file[scoped_task_key(upload_dir, task_file)] = task->id;
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

} // namespace action

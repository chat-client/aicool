#include "stdafx.h"
#include "convert_common.h"

namespace action {

static void run_audio_only_transcode_task_in_thread(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input_file,
	const std::string& tmp_file, const std::string& output_file)
{
	if (is_task_cancel_requested(task)) {
		unlink(tmp_file.c_str());
		finish_task(task, false, "已取消", "cancelled", -1);
		return;
	}

	unlink(tmp_file.c_str());

	const long long duration_ms = probe_duration_ms_in_thread(ffmpeg, input_file);
	ACL_ARGV* args = acl_argv_alloc(24);
	acl_argv_add(args,
		ffmpeg.c_str(),
		"-hide_banner",
		"-loglevel", "error",
		"-y",
		"-i", input_file.c_str(),
		"-map", "0:a:0",
		"-vn",
		"-c:a", "aac",
		"-ac", "2",
		"-b:a", "192k",
		"-movflags", "+faststart",
		"-progress", "pipe:1",
		"-nostats",
		tmp_file.c_str(),
		nullptr);

	const ffmpeg_process_ptr stream = start_ffmpeg_process_in_thread(args);

	if (stream == nullptr) {
		unlink(tmp_file.c_str());
		finish_task(task, false, "音频转码失败", acl::last_serror(), -1);
		return;
	}

	const int code = wait_transcode_progress_in_thread(task, *stream, duration_ms,
		5.0, 91.0, "音频转码中 (转为M4A/AAC)", 96.0, "写入M4A文件");
	if (is_task_cancel_requested(task)) {
		unlink(tmp_file.c_str());
		finish_task(task, false, "已取消", "cancelled", -1);
		return;
	}
	if (code != 0 || file_size_of(tmp_file.c_str()) <= 0) {
		unlink(tmp_file.c_str());
		finish_task(task, false, "音频转码失败", "audio only ffmpeg failed", -1);
		return;
	}

	unlink(output_file.c_str());
	if (rename(tmp_file.c_str(), output_file.c_str()) != 0) {
		unlink(tmp_file.c_str());
		finish_task(task, false, "音频转码失败", "rename audio output failed", -1);
		return;
	}

	const long long out_size = file_size_of(output_file.c_str());
	if (out_size <= 0) {
		unlink(output_file.c_str());
		finish_task(task, false, "音频转码失败", "audio output is empty", -1);
		return;
	}

	finish_task(task, true, "音频转码完成，已生成M4A文件", "", out_size);
}

static void run_audio_split_transcode_task_in_thread(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input_file,
	const std::string& tmp_file, const std::string& output_file,
	const std::string& secondary_output_file)
{
	if (is_task_cancel_requested(task)) {
		unlink(tmp_file.c_str());
		finish_task(task, false, "已取消", "cancelled", -1);
		return;
	}

	if (secondary_output_file.empty()) {
		finish_task(task, false, "拆分失败", "missing audio output path", -1);
		return;
	}

	const std::string audio_tmp_file = replace_ext(tmp_file, ".m4a");
	unlink(tmp_file.c_str());
	unlink(audio_tmp_file.c_str());

	const long long duration_ms = probe_duration_ms_in_thread(ffmpeg, input_file);
	ACL_ARGV* args = acl_argv_alloc(40);
	acl_argv_add(args,
		ffmpeg.c_str(),
		"-hide_banner",
		"-loglevel", "error",
		"-y",
		"-progress", "pipe:1",
		"-nostats",
		"-i", input_file.c_str(),
		"-map", "0:v:0",
		"-map", "0:s:0?",
		"-dn",
		"-c:v", "copy",
		"-tag:v", "avc1",
		"-c:s", "mov_text",
		"-movflags", "+faststart",
		tmp_file.c_str(),
		"-map", "0:a:0",
		"-vn",
		"-c:a", "aac",
		"-ac", "2",
		"-b:a", "192k",
		"-movflags", "+faststart",
		audio_tmp_file.c_str(),
		nullptr);

	const ffmpeg_process_ptr stream = start_ffmpeg_process_in_thread(args);

	if (stream == nullptr) {
		unlink(tmp_file.c_str());
		unlink(audio_tmp_file.c_str());
		finish_task(task, false, "拆分失败", acl::last_serror(), -1);
		return;
	}

	const int code = wait_transcode_progress_in_thread(task, *stream, duration_ms,
		5.0, 91.0, "拆分中 (复用视频并转M4A/AAC音频)", 96.0,
		"写入拆分文件");
	if (is_task_cancel_requested(task)) {
		unlink(tmp_file.c_str());
		unlink(audio_tmp_file.c_str());
		finish_task(task, false, "已取消", "cancelled", -1);
		return;
	}
	if (code != 0 || file_size_of(tmp_file.c_str()) <= 0
		|| file_size_of(audio_tmp_file.c_str()) <= 0)
	{
		unlink(tmp_file.c_str());
		unlink(audio_tmp_file.c_str());
		finish_task(task, false, "拆分失败", "split ffmpeg failed", -1);
		return;
	}

	unlink(output_file.c_str());
	unlink(secondary_output_file.c_str());
	if (rename(tmp_file.c_str(), output_file.c_str()) != 0) {
		unlink(tmp_file.c_str());
		unlink(audio_tmp_file.c_str());
		finish_task(task, false, "拆分失败", "rename video output failed", -1);
		return;
	}
	if (rename(audio_tmp_file.c_str(), secondary_output_file.c_str()) != 0) {
		unlink(audio_tmp_file.c_str());
		unlink(output_file.c_str());
		finish_task(task, false, "拆分失败", "rename audio output failed", -1);
		return;
	}

	const long long video_size = file_size_of(output_file.c_str());
	const long long audio_size = file_size_of(secondary_output_file.c_str());
	if (video_size <= 0 || audio_size <= 0) {
		unlink(output_file.c_str());
		unlink(secondary_output_file.c_str());
		finish_task(task, false, "拆分失败", "split output is empty", -1);
		return;
	}

	std::string vtt_path;
	std::string subtitle_err;
	const int subtitle_status = export_vtt_sidecar_in_thread(ffmpeg, input_file, output_file,
		vtt_path, subtitle_err);
	if (subtitle_status > 0) {
		finish_task(task, true, "拆分完成，已生成独立视频、M4A音频和VTT字幕", "", video_size + audio_size);
	} else if (subtitle_status == 0) {
		finish_task(task, true, "拆分完成，已生成独立视频和M4A音频", "", video_size + audio_size);
	} else {
		finish_task(task, true, "拆分完成，已生成独立视频和M4A音频（字幕导出失败）", "", video_size + audio_size);
	}
}

static void run_audio_transcode_task_in_thread(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input_file,
	const std::string& tmp_file, const std::string& output_file,
	const std::string& secondary_output_file,
	const transcode_strategy_t& strategy)
{
	if (strategy.mode == transcode_strategy_t::audio_only) {
		run_audio_only_transcode_task_in_thread(task, ffmpeg, input_file, tmp_file,
			output_file);
		return;
	}

	run_audio_split_transcode_task_in_thread(task, ffmpeg, input_file, tmp_file,
		output_file, secondary_output_file);
}

static void run_video_transcode_task_in_thread(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input_file,
	const std::string& tmp_file, const std::string& output_file)
{
	if (is_task_cancel_requested(task)) {
		unlink(tmp_file.c_str());
		finish_task(task, false, "已取消", "cancelled", -1);
		return;
	}

	const long long duration_ms = probe_duration_ms_in_thread(ffmpeg, input_file);
	ACL_ARGV* args = acl_argv_alloc(32);
	acl_argv_add(args,
		ffmpeg.c_str(),
		"-hide_banner",
		"-loglevel", "error",
		"-y",
		"-i", input_file.c_str(),
		"-map", "0:v:0",
		"-map", "0:a:0?",
		"-map", "0:s:0?",
		"-dn",
		nullptr);

	const auto strategy_msg = "视频转码中 (转换为H.264/AAC MP4)";
	acl_argv_add(args,
		"-c:v", "libx264",
		"-preset", "veryfast",
		"-crf", "23",
		"-pix_fmt", "yuv420p",
		nullptr);

	acl_argv_add(args,
		"-movflags", "+faststart",
		"-c:a", "aac",
		"-ac", "2",
		"-b:a", "192k",
		"-c:s", "mov_text",
		"-progress", "pipe:1",
		"-nostats",
		tmp_file.c_str(),
		nullptr);

	const ffmpeg_process_ptr stream = start_ffmpeg_process_in_thread(args);

	if (stream == nullptr) {
		unlink(tmp_file.c_str());
		finish_task(task, false, "转码启动失败", acl::last_serror(), -1);
		return;
	}

	const int code = wait_transcode_progress_in_thread(task, *stream, duration_ms,
		0.1, 100.0, strategy_msg, 99.5, "写入输出文件");
	if (is_task_cancel_requested(task)) {
		unlink(tmp_file.c_str());
		finish_task(task, false, "已取消", "cancelled", -1);
		return;
	}
	if (code != 0) {
		unlink(tmp_file.c_str());
		finish_task(task, false, "转码失败", "ffmpeg failed", -1);
		return;
	}

	if (rename(tmp_file.c_str(), output_file.c_str()) != 0) {
		unlink(tmp_file.c_str());
		finish_task(task, false, "转码失败", "rename transcoded file failed", -1);
		return;
	}

	// Keep source file untouched. Output is a separate transcoded file.

	const long long out_size = file_size_of(output_file.c_str());
	if (out_size <= 0) {
		finish_task(task, false, "转码失败", "transcoded file is empty", -1);
		return;
	}

	std::string vtt_path;
	std::string subtitle_err;
	const int subtitle_status = export_vtt_sidecar_in_thread(ffmpeg, input_file, output_file,
		vtt_path, subtitle_err);

	if (subtitle_status > 0) {
		finish_task(task, true, "转码完成，已导出VTT外挂字幕", "", out_size);
	} else if (subtitle_status == 0) {
		finish_task(task, true, "转码完成（未检测到字幕流）", "", out_size);
	} else {
		finish_task(task, true, "转码完成（字幕导出失败）", "", out_size);
	}
}

void run_transcode_task_in_thread(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input_file,
	const std::string& tmp_file, const std::string& output_file,
	const std::string& secondary_output_file,
	const transcode_strategy_t& strategy)
{
	if (strategy.mode == transcode_strategy_t::full_mp4) {
		run_video_transcode_task_in_thread(task, ffmpeg, input_file, tmp_file,
			output_file);
		return;
	}

	run_audio_transcode_task_in_thread(task, ffmpeg, input_file, tmp_file,
		output_file, secondary_output_file, strategy);
}

} // namespace action

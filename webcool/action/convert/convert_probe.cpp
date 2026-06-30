#include "stdafx.h"
#include "convert_common.h"

namespace action {

bool is_browser_friendly_h264_video_line(const char* line) {
	if (line == nullptr) {
		return false;
	}

	const bool has_h264 = strstr(line, "video: h264") != nullptr;
	if (!has_h264) {
		return false;
	}

	// Require 8-bit yuv420p only; avoid matching yuv420p10le by substring.
	const bool has_yuv420p_exact = strstr(line, "yuv420p,") != nullptr
		|| strstr(line, "yuv420p(") != nullptr
		|| strstr(line, "yuv420p ") != nullptr;
	const bool has_10bit_or_high_profile = strstr(line, "yuv420p10") != nullptr
		|| strstr(line, "high 10") != nullptr
		|| strstr(line, "high 4:2:2") != nullptr
		|| strstr(line, "high 4:4:4") != nullptr;

	return has_yuv420p_exact && !has_10bit_or_high_profile;
}

bool extract_primary_video_stream_line(const char* text,
	std::string& line)
{
	line.clear();
	if (text == nullptr) {
		return false;
	}

	const char* p = text;
	while ((p = strstr(p, "stream #")) != nullptr) {
		const char* eol = strchr(p, '\n');
		if (eol == nullptr) {
			eol = p + strlen(p);
		}
		const char* video = strstr(p, "video:");
		if (video != nullptr && video < eol) {
			line.assign(p, static_cast<size_t>(eol - p));
			return true;
		}
		if (*eol == '\0') {
			break;
		}
		p = eol + 1;
	}

	return false;
}

bool browser_can_play_video_by_probe(const std::string& ffmpeg,
	const std::string& input_file, std::string& reason)
{
	reason.clear();
	struct video_probe_result_t {
		bool has_video;
		bool has_avc1_tag;
		bool has_audio;
		bool has_aac_audio;
		bool decode_error;
		bool browser_friendly_video;
		std::string primary_video_line;
		video_probe_result_t()
		: has_video(false), has_avc1_tag(false), has_audio(false)
		, has_aac_audio(false), decode_error(false)
		, browser_friendly_video(false) {}
	};

	auto probe_video_streams = [&](video_probe_result_t& probe) {
		std::string cmd = shell_quote(ffmpeg) + " -hide_banner -i "
			+ shell_quote(input_file) + " 2>&1";
		std::string out;
		run_command_capture(cmd, out);

		acl::string lower(out.c_str());
		lower.lower();
		const char* s = lower.c_str();
		probe.has_video = extract_primary_video_stream_line(s, probe.primary_video_line);
		probe.has_avc1_tag = strstr(probe.primary_video_line.c_str(), "(avc1") != nullptr
			|| strstr(probe.primary_video_line.c_str(), "avc1 /") != nullptr;
		probe.has_audio = strstr(s, "audio:") != nullptr;
		probe.has_aac_audio = strstr(s, "audio: aac") != nullptr;
		probe.decode_error = strstr(s, "invalid data found") != nullptr
			|| strstr(s, "could not find codec parameters") != nullptr
			|| strstr(s, "moov atom not found") != nullptr;
		probe.browser_friendly_video = probe.has_video
			&& is_browser_friendly_h264_video_line(probe.primary_video_line.c_str());
	};

	video_probe_result_t probe;
	probe_video_streams(probe);

	if (!probe.has_video || probe.decode_error) {
		reason = "video stream parse failed";
		return false;
	}

	if (!probe.browser_friendly_video) {
		reason = "video stream is not browser-friendly h264/yuv420p";
		return false;
	}

	if (!probe.has_avc1_tag) {
		reason = "video stream is not tagged as avc1";
		return false;
	}

	if (probe.has_audio && !probe.has_aac_audio) {
		reason = "audio codec is not aac";
		return false;
	}

	return true;
}

bool probe_transcode_strategy(const std::string& ffmpeg,
	const std::string& input_file, transcode_strategy_t& strategy,
	bool allow_audio_split)
{
	strategy.mode = transcode_strategy_t::full_mp4;
	if (!allow_audio_split || !is_audio_split_candidate_video_name(input_file.c_str())) {
		return true;
	}

	std::string cmd = shell_quote(ffmpeg) + " -hide_banner -i "
		+ shell_quote(input_file) + " 2>&1";
	std::string out;
	run_command_capture(cmd, out);
	acl::string lower(out.c_str());
	lower.lower();
	const char* s = lower.c_str();
	std::string primary_video_line;
	const bool has_video = extract_primary_video_stream_line(s, primary_video_line);
	const bool has_audio = strstr(s, "audio:") != nullptr;
	const bool has_aac = strstr(s, "audio: aac") != nullptr;
	const bool decode_error = strstr(s, "invalid data found") != nullptr
		|| strstr(s, "could not find codec parameters") != nullptr
		|| strstr(s, "moov atom not found") != nullptr;
	if (has_video && has_audio && !has_aac && !decode_error
		&& is_browser_friendly_h264_video_line(primary_video_line.c_str()))
	{
		strategy.mode = transcode_strategy_t::audio_split;
	}
	return true;
}

bool probe_has_subtitle_stream(const std::string& ffmpeg,
	const std::string& input_file)
{
	std::string cmd = shell_quote(ffmpeg) + " -hide_banner -i "
		+ shell_quote(input_file) + " 2>&1";
	std::string out;
	run_command_capture(cmd, out);
	acl::string lower(out.c_str());
	lower.lower();
	return strstr(lower.c_str(), "subtitle:") != nullptr;
}

std::string replace_ext(const std::string& name, const char* new_ext) {
	size_t slash = name.find_last_of("/\\");
	size_t dot = name.find_last_of('.');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
		return name + (new_ext ? new_ext : "");
	}
	return name.substr(0, dot) + (new_ext ? new_ext : "");
}

// Return 1 when exported, 0 when no subtitle stream, -1 when export failed.
int export_vtt_sidecar(const std::string& ffmpeg,
	const std::string& input_file, const std::string& output_file,
	std::string& vtt_file, std::string& err)
{
	vtt_file = replace_ext(output_file, ".vtt");
	err.clear();

	if (!probe_has_subtitle_stream(ffmpeg, input_file)) {
		return 0;
	}

	unlink(vtt_file.c_str());

	std::string cmd = shell_quote(ffmpeg)
		+ " -hide_banner -loglevel error -y -i " + shell_quote(input_file)
		+ " -map 0:s:0 -c:s webvtt " + shell_quote(vtt_file)
		+ " 2>&1";

	std::string out;
	int code = run_command_capture(cmd, out);
	if (code != 0) {
		err = trim_text(out);
		if (err.empty()) {
			err = "subtitle export failed";
		}
		unlink(vtt_file.c_str());
		return -1;
	}

	if (file_size_of(vtt_file.c_str()) <= 0) {
		err = "subtitle export output is empty";
		unlink(vtt_file.c_str());
		return -1;
	}

	return 1;
}

} // namespace action

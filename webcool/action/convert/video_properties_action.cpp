#include "stdafx.h"
#include "convert_common.h"
#include "../file/file_common.h"
#include <regex>

namespace action {

namespace {

struct video_properties_t {
	long long size;
	long long duration_ms;
	long long bitrate_kbps;
	long long video_bitrate_kbps;
	long long audio_bitrate_kbps;
	long long sample_rate_hz;
	int width;
	int height;
	double fps;
	std::string video_codec;
	std::string audio_codec;
	std::string audio_channels;
	video_properties_t() : size(-1), duration_ms(-1), bitrate_kbps(-1),
		video_bitrate_kbps(-1), audio_bitrate_kbps(-1), sample_rate_hz(-1),
		width(0), height(0), fps(0) {}
};

std::string first_stream_line(const std::string& text, const char* kind)
{
	const std::string marker = std::string(kind) + ":";
	size_t pos = text.find(marker);
	if (pos == std::string::npos) return "";
	const size_t end = text.find('\n', pos);
	return trim_text(text.substr(pos, end == std::string::npos ? end : end - pos));
}

std::string codec_from_stream_line(const std::string& line, const char* kind)
{
	const std::string marker = std::string(kind) + ":";
	size_t pos = line.find(marker);
	if (pos == std::string::npos) return "";
	pos += marker.size();
	while (pos < line.size() && line[pos] == ' ') ++pos;
	size_t end = line.find(',', pos);
	std::string codec = trim_text(line.substr(pos, end == std::string::npos ? end : end - pos));
	const size_t space = codec.find(' ');
	if (space != std::string::npos) codec.erase(space);
	return codec;
}

long long match_integer(const std::string& text, const std::regex& pattern)
{
	std::smatch match;
	if (!std::regex_search(text, match, pattern) || match.size() < 2) return -1;
	return static_cast<long long>(strtoll(match[1].str().c_str(), nullptr, 10));
}

void probe_properties(const std::string& ffmpeg, const std::string& path,
	video_properties_t& result)
{
	result.size = file_size_of(path.c_str());
	std::string output;
	run_command_capture(shell_quote(ffmpeg) + " -hide_banner -i "
		+ shell_quote(path) + " 2>&1", output);
	result.duration_ms = parse_duration_ms_from_text(output);
	result.bitrate_kbps = match_integer(output,
		std::regex("Duration:[^\\n]*bitrate: ([0-9]+) kb/s"));
	const std::string video = first_stream_line(output, "Video");
	const std::string audio = first_stream_line(output, "Audio");
	result.video_codec = codec_from_stream_line(video, "Video");
	result.audio_codec = codec_from_stream_line(audio, "Audio");
	std::smatch match;
	if (std::regex_search(video, match, std::regex("([0-9]{2,5})x([0-9]{2,5})"))) {
		result.width = atoi(match[1].str().c_str());
		result.height = atoi(match[2].str().c_str());
	}
	if (std::regex_search(video, match, std::regex("([0-9]+(?:\\.[0-9]+)?) fps"))) {
		result.fps = atof(match[1].str().c_str());
	}
	result.video_bitrate_kbps = match_integer(video, std::regex("([0-9]+) kb/s"));
	result.sample_rate_hz = match_integer(audio, std::regex("([0-9]+) Hz"));
	result.audio_bitrate_kbps = match_integer(audio, std::regex("([0-9]+) kb/s"));
	if (!audio.empty()) {
		std::vector<std::string> parts;
		size_t begin = 0;
		while (begin < audio.size()) {
			const size_t comma = audio.find(',', begin);
			parts.push_back(trim_text(audio.substr(begin,
				comma == std::string::npos ? comma : comma - begin)));
			if (comma == std::string::npos) break;
			begin = comma + 1;
		}
		for (size_t i = 1; i < parts.size(); ++i) {
			if (parts[i].find("Hz") == std::string::npos
				&& parts[i].find("kb/s") == std::string::npos
				&& parts[i].find("fltp") == std::string::npos
				&& parts[i].find("s16") == std::string::npos
				&& parts[i].find("s32") == std::string::npos) {
				result.audio_channels = parts[i];
				break;
			}
		}
	}
}

bool send_properties(response_t& res, bool keep_alive, const std::string& name,
	const video_properties_t& value)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("name", name.c_str());
	root.add_bool("has_video", !value.video_codec.empty());
	if (value.size >= 0) root.add_number("size", value.size);
	if (value.duration_ms >= 0) root.add_number("duration_ms", value.duration_ms);
	if (value.bitrate_kbps >= 0) root.add_number("bitrate_kbps", value.bitrate_kbps);
	if (!value.video_codec.empty()) root.add_text("video_codec", value.video_codec.c_str());
	if (value.width > 0) root.add_number("width", value.width);
	if (value.height > 0) root.add_number("height", value.height);
	if (value.fps > 0) root.add_double("fps", value.fps);
	if (value.video_bitrate_kbps >= 0) root.add_number("video_bitrate_kbps", value.video_bitrate_kbps);
	root.add_bool("has_audio", !value.audio_codec.empty());
	if (!value.audio_codec.empty()) root.add_text("audio_codec", value.audio_codec.c_str());
	if (value.sample_rate_hz >= 0) root.add_number("sample_rate_hz", value.sample_rate_hz);
	if (!value.audio_channels.empty()) root.add_text("audio_channels", value.audio_channels.c_str());
	if (value.audio_bitrate_kbps >= 0) root.add_number("audio_bitrate_kbps", value.audio_bitrate_kbps);
	return sendJson(res, 200, root, keep_alive);
}

} // namespace

bool VideoPropertiesAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string file;
	std::string err;
	if (!normalize_relative_path(req.getParameter("file") ? req.getParameter("file") : "",
		file, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!resolve_upload_regular_file_path(upload_dir, file, file)) {
		json_error(res, 404, "source media not found", req.isKeepAlive());
		return true;
	}
	const std::string basename = base_name_from_relative_path(file);
	if (!is_video_name(basename.c_str()) && !is_audio_file(basename.c_str())) {
		json_error(res, 400, "file is not supported video or audio", req.isKeepAlive());
		return true;
	}
	const std::string ffmpeg = choose_ffmpeg_path();
	if (ffmpeg.empty()) {
		json_error(res, 500, "ffmpeg not found", req.isKeepAlive());
		return true;
	}
	video_properties_t properties;
	probe_properties(ffmpeg, join_upload_path(upload_dir, file), properties);
	return send_properties(res, req.isKeepAlive(), file, properties);
}

bool VideoPropertiesAction::runLocal(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	(void) upload_dir;
	std::string path;
	std::string err;
	if (!normalize_local_video_path(req.getParameter("path"), path, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	struct stat st{};
	const std::string basename = local_base_name(path);
	if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		json_error(res, 404, "source media not found", req.isKeepAlive());
		return true;
	}
	if (!is_video_name(basename.c_str()) && !is_audio_file(basename.c_str())) {
		json_error(res, 400, "file is not supported video or audio", req.isKeepAlive());
		return true;
	}
	const std::string ffmpeg = choose_ffmpeg_path();
	if (ffmpeg.empty()) {
		json_error(res, 500, "ffmpeg not found", req.isKeepAlive());
		return true;
	}
	video_properties_t properties;
	probe_properties(ffmpeg, path, properties);
	return send_properties(res, req.isKeepAlive(), path, properties);
}

} // namespace action

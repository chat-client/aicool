#include "stdafx.h"
#include "convert_common.h"

namespace action {

std::string replace_ext_with_mp4(const std::string& name) {
	size_t slash = name.find_last_of("/\\");
	size_t dot = name.find_last_of('.');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
		return name + ".mp4";
	}
	return name.substr(0, dot) + ".mp4";
}

bool path_exists(const std::string& path) {
	return access(path.c_str(), F_OK) == 0;
}

std::string make_unique_transcoded_name(const std::string& upload_dir,
	const std::string& input_name)
{
	std::string base_mp4 = replace_ext_with_mp4(input_name);
	size_t slash = base_mp4.find_last_of("/\\");
	size_t dot = base_mp4.find_last_of('.');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
		dot = base_mp4.size();
	}

	const std::string stem = base_mp4.substr(0, dot);
	const std::string ext = ".mp4";

	std::string candidate = stem + "_web" + ext;
	std::string full = upload_dir + "/" + candidate;
	if (!path_exists(full)) {
		return candidate;
	}

	for (int i = 2; i < 10000; ++i) {
		candidate = stem;
		candidate += "_web_";
		candidate += std::to_string(i);
		candidate += ext;
		full = upload_dir;
		full += "/";
		full += candidate;
		if (!path_exists(full)) {
			return candidate;
		}
	}

	char buf[64];
	snprintf(buf, sizeof(buf), "_web_%lu",
		static_cast<unsigned long>(g_transcode_seq.load()));
	return stem + std::string(buf) + ext;
}

std::string make_unique_audio_only_name(const std::string& upload_dir,
	const std::string& input_name)
{
	const std::string stem = replace_ext(input_name, "");
	std::string candidate = stem + "_web_audio.m4a";
	if (!path_exists(join_upload_path(upload_dir, candidate))) {
		return candidate;
	}

	for (int i = 2; i < 10000; ++i) {
		candidate = stem + "_web_audio_" + std::to_string(i) + ".m4a";
		if (!path_exists(join_upload_path(upload_dir, candidate))) {
			return candidate;
		}
	}

	char buf[64];
	snprintf(buf, sizeof(buf), "_web_audio_%lu.m4a",
		static_cast<unsigned long>(g_transcode_seq.load()));
	return stem + std::string(buf);
}

void make_unique_split_output_names(const std::string& upload_dir,
	const std::string& input_name, std::string& video_name,
	std::string& audio_name)
{
	video_name.clear();
	audio_name.clear();
	const std::string stem = replace_ext(input_name, "");
	for (int i = 1; i < 10000; ++i) {
		const std::string suffix = i == 1 ? "" : ("_" + std::to_string(i));
		std::string candidate_video = stem;
		candidate_video += "_web_video";
		candidate_video += suffix;
		candidate_video += ".mp4";

		std::string candidate_audio = stem;
		candidate_audio += "_web_audio";
		candidate_audio += suffix;
		candidate_audio += ".m4a";

		if (!path_exists(join_upload_path(upload_dir, candidate_video))
			&& !path_exists(join_upload_path(upload_dir, candidate_audio)))
		{
			video_name = candidate_video;
			audio_name = candidate_audio;
			return;
		}
	}
	char buf[64];
	snprintf(buf, sizeof(buf), "_%lu",
		static_cast<unsigned long>(g_transcode_seq.load()));
	video_name = stem + "_web_video" + std::string(buf) + ".mp4";
	audio_name = stem + "_web_audio" + std::string(buf) + ".m4a";
}

std::string make_unique_local_transcoded_path(const std::string& input_path)
{
	std::string base_mp4 = replace_ext_with_mp4(input_path);
	if (!path_exists(base_mp4)) {
		return base_mp4;
	}

	size_t slash = base_mp4.find_last_of("/\\");
	size_t dot = base_mp4.find_last_of('.');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
		dot = base_mp4.size();
	}
	const std::string stem = base_mp4.substr(0, dot);
	const std::string ext = ".mp4";
	for (int i = 2; i < 10000; ++i) {
		std::string candidate = stem;
		candidate += "_web_";
		candidate += std::to_string(i);
		candidate += ext;
		if (!path_exists(candidate)) {
			return candidate;
		}
	}
	char buf[64];
	snprintf(buf, sizeof(buf), "_web_%lu",
		static_cast<unsigned long>(g_transcode_seq.load()));
	return stem + std::string(buf) + ext;
}

bool send_existing_local_mp4(const std::string& path, response_t& res)
{
	FILE* fp = fopen(path.c_str(), "rb");
	if (fp == nullptr) {
		return sendText(res, 403, "file cannot be read\n", false);
	}
	const long long fsize = file_size_of(path.c_str());
	if (fsize <= 0) {
		fclose(fp);
		return sendText(res, 404, "converted mp4 not found\n", false);
	}
	res.setStatus(200)
		.setKeepAlive(false)
		.setContentType("video/mp4")
		.setHeader("Content-Disposition", "inline")
		.setHeader("Accept-Ranges", "bytes")
		.setContentLength(fsize);

	std::vector<char> buf(8192);
	bool ok = true;
	while (!feof(fp)) {
		const size_t n = fread(buf.data(), 1, buf.size(), fp);
		if (n > 0 && !res.write(buf.data(), n)) {
			ok = false;
			break;
		}
		if (n < buf.size()) {
			if (ferror(fp)) {
				ok = false;
			}
			break;
		}
	}
	fclose(fp);
	return ok ? res.write(nullptr, 0) : false;
}

} // namespace action

#include "stdafx.h"
#include "actions.h"
#include "action_util.h"
#ifdef _WIN32
#include "../platform_compat.h"
#else
#include <strings.h>
#endif

namespace action {

static bool is_video_name(const char* filename) {
	const char* dot = strrchr(filename, '.');
	if (dot == nullptr || *(dot + 1) == '\0') {
		return false;
	}

	return strcasecmp(dot, ".mp4") == 0
		|| strcasecmp(dot, ".mkv") == 0
		|| strcasecmp(dot, ".avi") == 0
		|| strcasecmp(dot, ".rmvb") == 0
		|| strcasecmp(dot, ".mov") == 0
		|| strcasecmp(dot, ".wmv") == 0
		|| strcasecmp(dot, ".mpg") == 0
		|| strcasecmp(dot, ".mpeg") == 0;
}

static long long file_size_of(const char* path) {
	if (*path == '\0') {
		return -1;
	}
	return regular_file_size(path);
}

static std::string shell_quote(const std::string& s) {
	std::string out;
	out.reserve(s.size() + 8);
#ifdef _WIN32
	out.push_back('"');
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '"') {
			out += "\\\"";
		} else {
			out.push_back(s[i]);
		}
	}
	out.push_back('"');
#else
	out.push_back('\'');
	for (const char i : s) {
		if (i == '\'') {
			out += "'\\''";
		} else {
			out.push_back(i);
		}
	}
	out.push_back('\'');
#endif
	return out;
}

static int run_command_capture_direct(const std::string& command, std::string& output) {
	output.clear();
#ifdef _WIN32
	SECURITY_ATTRIBUTES sa;
	memset(&sa, 0, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE read_pipe = nullptr;
	HANDLE write_pipe = nullptr;
	if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
		return -1;
	}
	SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

	std::string full_command = std::string("cmd.exe /C ") + command;
	std::wstring wcmd;
	if (!webcool_utf8_to_wide(full_command.c_str(), wcmd)) {
		CloseHandle(read_pipe);
		CloseHandle(write_pipe);
		return -1;
	}

	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	memset(&si, 0, sizeof(si));
	memset(&pi, 0, sizeof(pi));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = write_pipe;
	si.hStdError = write_pipe;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

	BOOL ok = CreateProcessW(nullptr, &wcmd[0], nullptr, NULL, TRUE,
		CREATE_NO_WINDOW, nullptr, NULL, &si, &pi);
	CloseHandle(write_pipe);
	if (!ok) {
		CloseHandle(read_pipe);
		return -1;
	}

	char buf[4096];
	DWORD n = 0;
	while (ReadFile(read_pipe, buf, sizeof(buf), &n, nullptr) && n > 0) {
		output.append(buf, buf + n);
	}
	CloseHandle(read_pipe);
	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD exit_code = 1;
	GetExitCodeProcess(pi.hProcess, &exit_code);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return (int) exit_code;
#else
	FILE* fp = popen(command.c_str(), "r");
	if (fp == nullptr) {
		return -1;
	}

	char buf[4096];
	while (fgets(buf, sizeof(buf), fp) != nullptr) {
		output.append(buf);
	}

	int code = pclose(fp);
	if (code == -1) {
		return -1;
	}

	return WEXITSTATUS(code);
#endif
}

static int run_command_capture_in_thread(const std::string& command, std::string& output) {
	int res = 0;
	acl::gofiber_wait_thread([&command, &output, &res] {
		res = run_command_capture_direct(command, output);
	});
	return res;
}

struct probe_result_t {
	bool has_audio;
	bool browser_audio_supported;
	std::string audio_codec;

	probe_result_t() : has_audio(false), browser_audio_supported(true) {}
};

static probe_result_t probe_audio_info_in_thread(const std::string& ffmpeg,
	const std::string& input_file)
{
	probe_result_t ret;
	const std::string cmd = shell_quote(ffmpeg)
		+ " -hide_banner -loglevel info -t 1 -i "
		+ shell_quote(input_file) + " 2>&1";

	std::string out;
	run_command_capture_in_thread(cmd, out);

	std::string lower = out;
	for (char & i : lower) {
		if (i >= 'A' && i <= 'Z') {
			i = static_cast<char>(i - 'A' + 'a');
		}
	}

	size_t pos = lower.find("audio:");
	if (pos == std::string::npos) {
		ret.has_audio = false;
		ret.browser_audio_supported = true;
		return ret;
	}

	ret.has_audio = true;
	pos += 6;
	while (pos < lower.size() && (lower[pos] == ' ' || lower[pos] == '\t')) {
		++pos;
	}

	const size_t start = pos;
	while (pos < lower.size()) {
		const char c = lower[pos];
		if (c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			break;
		}
		++pos;
	}

	if (pos > start) {
		ret.audio_codec = lower.substr(start, pos - start);
	}

	if (ret.audio_codec.empty()
		|| ret.audio_codec == "aac"
		|| ret.audio_codec == "mp3"
		|| ret.audio_codec == "opus"
		|| ret.audio_codec == "vorbis")
	{
		ret.browser_audio_supported = true;
	} else {
		ret.browser_audio_supported = false;
	}

	return ret;
}

#if 0
static void json_error(response_t& res, const int status, const char* msg,
	const bool keep_alive)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", msg);
	sendJson(res, status, root, keep_alive);
}
#endif

bool VideoProbeAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	const char* file = req.getParameter("file");
	if (file == nullptr || *file == '\0') {
		json_error(res, 400, "missing query parameter: file", req.isKeepAlive());
		return true;
	}

	std::string file_path;
	std::string err;
	if (!normalize_relative_path(file, file_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!resolve_upload_regular_file_path(upload_dir, file_path, file_path)) {
		json_error(res, 404, "source video not found", req.isKeepAlive());
		return true;
	}

	const std::string base_name = base_name_from_relative_path(file_path);
	if (!is_video_name(base_name.c_str())) {
		json_error(res, 400, "file is not a supported video", req.isKeepAlive());
		return true;
	}

	const std::string in_path = join_upload_path(upload_dir, file_path);
	if (file_size_of(in_path.c_str()) <= 0) {
		json_error(res, 404, "source video not found", req.isKeepAlive());
		return true;
	}

	const std::string ffmpeg = choose_ffmpeg_path();
	if (ffmpeg.empty()) {
		json_error(res, 500, "ffmpeg not found in tools directory", req.isKeepAlive());
		return true;
	}

	probe_result_t probe = probe_audio_info_in_thread(ffmpeg, in_path);

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_bool("has_audio", probe.has_audio);
	root.add_bool("browser_audio_supported", probe.browser_audio_supported);
	if (!probe.audio_codec.empty()) {
		root.add_text("audio_codec", probe.audio_codec.c_str());
	}
	root.add_text("name", base_name.c_str());
	root.add_text("file", file_path.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

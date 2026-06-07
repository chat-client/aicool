#include "actions.h"
#include "action_util.h"
#ifdef _WIN32
#include "../platform_compat.h"
#else
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace action {

struct transcode_task_t {
	std::string id;
	std::string scope;
	std::string file_name;
	std::string output_name;
	std::string secondary_output_name;
	std::string error;
	std::string message;
	double progress;
	long process_pid;
	bool done;
	bool success;
	bool cancel_requested;
	bool local;
	long long size;
	transcode_task_t()
	: progress(0), process_pid(-1), done(false), success(false)
	, cancel_requested(false), local(false), size(-1) {}
};

struct transcode_task_snapshot_t {
	std::string id;
	std::string scope;
	std::string file_name;
	std::string output_name;
	std::string secondary_output_name;
	std::string error;
	std::string message;
	double progress;
	long process_pid;
	bool done;
	bool success;
	bool cancel_requested;
	bool local;
	long long size;
	transcode_task_snapshot_t()
	: progress(0), process_pid(-1), done(false), success(false)
	, cancel_requested(false), local(false), size(-1) {}
};

static std::mutex g_transcode_mutex;
static std::map<std::string, std::shared_ptr<transcode_task_t> > g_transcode_tasks;
static std::map<std::string, std::string> g_running_task_by_file;
static std::set<std::string> g_active_stream_sidecars;
static std::atomic<unsigned long> g_transcode_seq(1);

static bool is_video_name(const char* filename) {
	if (filename == NULL) {
		return false;
	}

	const char* dot = strrchr(filename, '.');
	if (dot == NULL || *(dot + 1) == '\0') {
		return false;
	}

	return strcasecmp(dot, ".mp4") == 0
		|| strcasecmp(dot, ".mkv") == 0
		|| strcasecmp(dot, ".avi") == 0
		|| strcasecmp(dot, ".rm") == 0
		|| strcasecmp(dot, ".rmvb") == 0
		|| strcasecmp(dot, ".mov") == 0
		|| strcasecmp(dot, ".wmv") == 0;
}

static bool is_local_convertible_video_name(const char* filename) {
	if (filename == NULL) {
		return false;
	}
	const char* dot = strrchr(filename, '.');
	if (dot == NULL || *(dot + 1) == '\0') {
		return false;
	}
	return strcasecmp(dot, ".rmvb") == 0
		|| strcasecmp(dot, ".rm") == 0
		|| strcasecmp(dot, ".avi") == 0;
}

static bool is_audio_split_candidate_video_name(const char* filename) {
	if (filename == NULL) {
		return false;
	}
	const char* dot = strrchr(filename, '.');
	if (dot == NULL || *(dot + 1) == '\0') {
		return false;
	}
	return strcasecmp(dot, ".mp4") == 0
		|| strcasecmp(dot, ".mkv") == 0;
}

static long long file_size_of(const char* path) {
	if (path == NULL || *path == '\0') {
		return -1;
	}
	return regular_file_size(path);
}

static std::string local_parent_path(const std::string& path) {
	if (path.empty() || path == "/") {
		return "/";
	}
	std::string text = path;
	while (text.size() > 1 && text[text.size() - 1] == '/') {
		text.erase(text.size() - 1);
	}
	std::string::size_type pos = text.rfind('/');
	if (pos == std::string::npos || pos == 0) {
		return "/";
	}
	return text.substr(0, pos);
}

static std::string local_join_path(const std::string& parent, const char* name) {
	if (parent == "/") {
		return std::string("/") + name;
	}
	return parent + "/" + name;
}

static std::string local_base_name(const std::string& path) {
	std::string text = path;
	while (text.size() > 1 && text[text.size() - 1] == '/') {
		text.erase(text.size() - 1);
	}
	std::string::size_type pos = text.rfind('/');
	if (pos == std::string::npos) {
		return text;
	}
	return text.substr(pos + 1);
}

static std::string local_file_lock_key(const std::string& path) {
	return std::string("local:") + path;
}

static bool normalize_local_video_path(const char* input, std::string& path,
	std::string& err)
{
	path.clear();
	err.clear();
	if (input == NULL || *input == '\0') {
		err = "missing query parameter: path";
		return false;
	}
	if (input[0] != '/') {
		err = "absolute path is required";
		return false;
	}
	char resolved[PATH_MAX];
	if (realpath(input, resolved) == NULL) {
		err = strerror(errno);
		return false;
	}
	path = resolved;
	return true;
}

static std::string local_stream_state_path(const std::string& local_path) {
	return local_parent_path(local_path) + "/." + local_base_name(local_path)
		+ ".meta";
}

static std::string local_stream_tmp_mp4_path(const std::string& local_path) {
	const std::string base = local_base_name(local_path);
	std::string stem = base;
	const std::string::size_type dot = base.rfind('.');
	if (dot != std::string::npos) {
		stem = base.substr(0, dot);
	}
	return local_parent_path(local_path) + "/." + stem + ".mp4";
}

static long long read_local_stream_position_ms(const std::string& local_path) {
	FILE* fp = fopen(local_stream_state_path(local_path).c_str(), "r");
	if (fp == NULL) {
		return 0;
	}
	char buf[128];
	if (fgets(buf, (int) sizeof(buf), fp) == NULL) {
		fclose(fp);
		return 0;
	}
	fclose(fp);
	long long value = atoll(buf);
	return value > 0 ? value : 0;
}

static bool write_local_stream_position_ms(const std::string& local_path,
	long long position_ms, std::string& err)
{
	err.clear();
	FILE* fp = fopen(local_stream_state_path(local_path).c_str(), "w");
	if (fp == NULL) {
		err = strerror(errno);
		return false;
	}
	fprintf(fp, "%lld\n", position_ms > 0 ? position_ms : 0);
	if (fclose(fp) != 0) {
		err = strerror(errno);
		return false;
	}
	return true;
}

static void remove_local_stream_position(const std::string& local_path)
{
	::unlink(local_stream_state_path(local_path).c_str());
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
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '\'') {
			out += "'\\''";
		} else {
			out.push_back(s[i]);
		}
	}
	out.push_back('\'');
#endif
	return out;
}

static int run_command_capture(const std::string& command, std::string& output) {
	output.clear();
#ifdef _WIN32
	SECURITY_ATTRIBUTES sa;
	memset(&sa, 0, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE read_pipe = NULL;
	HANDLE write_pipe = NULL;
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

	BOOL ok = CreateProcessW(NULL, &wcmd[0], NULL, NULL, TRUE,
		CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	CloseHandle(write_pipe);
	if (!ok) {
		CloseHandle(read_pipe);
		return -1;
	}

	char buf[4096];
	DWORD n = 0;
	while (ReadFile(read_pipe, buf, sizeof(buf), &n, NULL) && n > 0) {
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
	if (fp == NULL) {
		return -1;
	}

	char buf[4096];
	while (fgets(buf, (int) sizeof(buf), fp) != NULL) {
		output.append(buf);
	}

	int code = pclose(fp);
	if (code == -1) {
		return -1;
	}

	return WEXITSTATUS(code);
#endif
}

static std::string trim_text(const std::string& s) {
	size_t b = 0;
	while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) {
		++b;
	}
	size_t e = s.size();
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) {
		--e;
	}
	return s.substr(b, e - b);
}

static long long parse_duration_ms_from_text(const std::string& text) {
	const char* p = strstr(text.c_str(), "Duration: ");
	if (p == NULL) {
		return -1;
	}
	p += 10;
	int hh = 0, mm = 0;
	double ss = 0;
	if (sscanf(p, "%d:%d:%lf", &hh, &mm, &ss) != 3) {
		return -1;
	}
	return (long long) (((hh * 3600.0) + (mm * 60.0) + ss) * 1000.0);
}

static long long probe_duration_ms(const std::string& ffmpeg,
	const std::string& input_file)
{
	std::string cmd = shell_quote(ffmpeg) + " -hide_banner -i "
		+ shell_quote(input_file) + " 2>&1";
	std::string out;
	run_command_capture(cmd, out);
	return parse_duration_ms_from_text(out);
}

static long long parse_progress_ms_line(const std::string& line) {
	if (line.compare(0, 12, "out_time_us=") == 0) {
		long long us = atoll(line.c_str() + 12);
		return us / 1000;
	}
	if (line.compare(0, 12, "out_time_ms=") == 0) {
		long long value = atoll(line.c_str() + 12);
		if (value > 1000000) {
			return value / 1000;
		}
		return value;
	}
	if (line.compare(0, 9, "out_time=") == 0) {
		return parse_duration_ms_from_text(std::string("Duration: ") + std::string(line.c_str() + 9));
	}
	return -1;
}

static bool is_browser_friendly_h264_video_line(const char* line) {
	if (line == NULL) {
		return false;
	}

	const bool has_h264 = strstr(line, "video: h264") != NULL;
	if (!has_h264) {
		return false;
	}

	// Require 8-bit yuv420p only; avoid matching yuv420p10le by substring.
	const bool has_yuv420p_exact = strstr(line, "yuv420p,") != NULL
		|| strstr(line, "yuv420p(") != NULL
		|| strstr(line, "yuv420p ") != NULL;
	const bool has_10bit_or_high_profile = strstr(line, "yuv420p10") != NULL
		|| strstr(line, "high 10") != NULL
		|| strstr(line, "high 4:2:2") != NULL
		|| strstr(line, "high 4:4:4") != NULL;

	return has_yuv420p_exact && !has_10bit_or_high_profile;
}

static bool extract_primary_video_stream_line(const char* text,
	std::string& line)
{
	line.clear();
	if (text == NULL) {
		return false;
	}

	const char* p = text;
	while ((p = strstr(p, "stream #")) != NULL) {
		const char* eol = strchr(p, '\n');
		if (eol == NULL) {
			eol = p + strlen(p);
		}
		const char* video = strstr(p, "video:");
		if (video != NULL && video < eol) {
			line.assign(p, (size_t) (eol - p));
			return true;
		}
		if (*eol == '\0') {
			break;
		}
		p = eol + 1;
	}

	return false;
}

static bool browser_can_play_video_by_probe(const std::string& ffmpeg,
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
		probe.has_avc1_tag = strstr(probe.primary_video_line.c_str(), "(avc1") != NULL
			|| strstr(probe.primary_video_line.c_str(), "avc1 /") != NULL;
		probe.has_audio = strstr(s, "audio:") != NULL;
		probe.has_aac_audio = strstr(s, "audio: aac") != NULL;
		probe.decode_error = strstr(s, "invalid data found") != NULL
			|| strstr(s, "could not find codec parameters") != NULL
			|| strstr(s, "moov atom not found") != NULL;
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

struct transcode_strategy_t {
	enum mode_t {
		full_mp4,
		audio_only,
		audio_split,
	} mode;
	transcode_strategy_t() : mode(full_mp4) {}
};

static bool probe_transcode_strategy(const std::string& ffmpeg,
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
	const bool has_audio = strstr(s, "audio:") != NULL;
	const bool has_aac = strstr(s, "audio: aac") != NULL;
	const bool decode_error = strstr(s, "invalid data found") != NULL
		|| strstr(s, "could not find codec parameters") != NULL
		|| strstr(s, "moov atom not found") != NULL;
	if (has_video && has_audio && !has_aac && !decode_error
		&& is_browser_friendly_h264_video_line(primary_video_line.c_str()))
	{
		strategy.mode = transcode_strategy_t::audio_split;
	}
	return true;
}

static bool probe_has_subtitle_stream(const std::string& ffmpeg,
	const std::string& input_file)
{
	std::string cmd = shell_quote(ffmpeg) + " -hide_banner -i "
		+ shell_quote(input_file) + " 2>&1";
	std::string out;
	run_command_capture(cmd, out);
	acl::string lower(out.c_str());
	lower.lower();
	return strstr(lower.c_str(), "subtitle:") != NULL;
}

static std::string replace_ext(const std::string& name, const char* new_ext) {
	size_t slash = name.find_last_of("/\\");
	size_t dot = name.find_last_of('.');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
		return name + (new_ext ? new_ext : "");
	}
	return name.substr(0, dot) + (new_ext ? new_ext : "");
}

// Return 1 when exported, 0 when no subtitle stream, -1 when export failed.
static int export_vtt_sidecar(const std::string& ffmpeg,
	const std::string& input_file, const std::string& output_file,
	std::string& vtt_file, std::string& err)
{
	vtt_file = replace_ext(output_file, ".vtt");
	err.clear();

	if (!probe_has_subtitle_stream(ffmpeg, input_file)) {
		return 0;
	}

	::unlink(vtt_file.c_str());

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
		::unlink(vtt_file.c_str());
		return -1;
	}

	if (file_size_of(vtt_file.c_str()) <= 0) {
		err = "subtitle export output is empty";
		::unlink(vtt_file.c_str());
		return -1;
	}

	return 1;
}

static std::string make_task_id() {
	char buf[64];
	snprintf(buf, sizeof(buf), "tx-%u-%lu", (unsigned) getpid(),
		(unsigned long) g_transcode_seq.fetch_add(1));
	return std::string(buf);
}

static void update_task_progress(const std::shared_ptr<transcode_task_t>& task,
	double percent, const char* msg)
{
	std::lock_guard<std::mutex> guard(g_transcode_mutex);
	if (percent < 0) {
		percent = 0;
	}
	if (percent > 0 && percent < 1) {
		percent = 1;
	}
	if (percent > 100) {
		percent = 100;
	}
	task->progress = percent;
	if (msg) {
		task->message = msg;
	}
}

static void set_task_process_pid(const std::shared_ptr<transcode_task_t>& task,
	long process_pid)
{
	std::lock_guard<std::mutex> guard(g_transcode_mutex);
	task->process_pid = process_pid;
}

static bool is_task_cancel_requested(const std::shared_ptr<transcode_task_t>& task) {
	std::lock_guard<std::mutex> guard(g_transcode_mutex);
	return task->cancel_requested;
}

static std::string scoped_task_key(const std::string& scope,
	const std::string& file_name)
{
	return scope + "\n" + file_name;
}

static void finish_task(const std::shared_ptr<transcode_task_t>& task,
	bool success, const char* msg, const char* err, long long size)
{
	std::lock_guard<std::mutex> guard(g_transcode_mutex);
	task->done = true;
	task->success = success;
	task->progress = success ? 100.0 : task->progress;
	task->size = size;
	task->process_pid = -1;
	task->message = msg ? msg : "";
	task->error = err ? err : "";
	std::map<std::string, std::string>::iterator it =
		g_running_task_by_file.find(scoped_task_key(task->scope, task->file_name));
	if (it != g_running_task_by_file.end() && it->second == task->id) {
		g_running_task_by_file.erase(it);
	}
}

struct ffmpeg_process_t {
#ifdef _WIN32
	HANDLE process;
	HANDLE thread;
	HANDLE read_pipe;
	DWORD pid;
	std::string pending;
	ffmpeg_process_t() : process(NULL), thread(NULL), read_pipe(NULL), pid(0) {}
#else
	ACL_VSTREAM* stream;
	ffmpeg_process_t() : stream(NULL) {}
#endif
};

#ifdef _WIN32
static std::string quote_windows_arg(const char* arg)
{
	std::string text = arg ? arg : "";
	bool needs_quotes = text.empty();
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == ' ' || text[i] == '\t' || text[i] == '"') {
			needs_quotes = true;
			break;
		}
	}
	if (!needs_quotes) {
		return text;
	}

	std::string out;
	out.push_back('"');
	size_t backslashes = 0;
	for (size_t i = 0; i < text.size(); ++i) {
		const char c = text[i];
		if (c == '\\') {
			++backslashes;
			continue;
		}
		if (c == '"') {
			out.append(backslashes * 2 + 1, '\\');
			out.push_back('"');
		} else {
			out.append(backslashes, '\\');
			out.push_back(c);
		}
		backslashes = 0;
	}
	out.append(backslashes * 2, '\\');
	out.push_back('"');
	return out;
}

static bool read_ffmpeg_line(ffmpeg_process_t* proc, char* buf, size_t size)
{
	if (proc == NULL || proc->read_pipe == NULL || buf == NULL || size == 0) {
		return false;
	}

	while (true) {
		size_t pos = proc->pending.find('\n');
		if (pos != std::string::npos) {
			std::string line = proc->pending.substr(0, pos);
			proc->pending.erase(0, pos + 1);
			if (!line.empty() && line[line.size() - 1] == '\r') {
				line.erase(line.size() - 1);
			}
			const size_t n = line.size() < size - 1 ? line.size() : size - 1;
			memcpy(buf, line.data(), n);
			buf[n] = '\0';
			return true;
		}

		char tmp[4096];
		DWORD n = 0;
		if (!ReadFile(proc->read_pipe, tmp, sizeof(tmp), &n, NULL) || n == 0) {
			if (!proc->pending.empty()) {
				std::string line;
				line.swap(proc->pending);
				if (!line.empty() && line[line.size() - 1] == '\r') {
					line.erase(line.size() - 1);
				}
				const size_t len = line.size() < size - 1 ? line.size() : size - 1;
				memcpy(buf, line.data(), len);
				buf[len] = '\0';
				return true;
			}
			return false;
		}
		proc->pending.append(tmp, tmp + n);
	}
}
#endif

static int wait_transcode_progress(const std::shared_ptr<transcode_task_t>& task,
	ffmpeg_process_t* proc, long long duration_ms,
	double start_percent, double progress_span,
	const char* progress_msg, double end_percent,
	const char* end_msg)
{
#ifdef _WIN32
	set_task_process_pid(task, proc ? (long) proc->pid : -1);
#else
	set_task_process_pid(task, proc && proc->stream ? (long) proc->stream->pid : -1);
#endif
	update_task_progress(task, start_percent, progress_msg);

	char buf[4096];
#ifdef _WIN32
	while (read_ffmpeg_line(proc, buf, sizeof(buf))) {
#else
	int ret;
	while ((ret = acl_vstream_gets_nonl(proc->stream, buf, sizeof(buf) - 1)) != ACL_VSTREAM_EOF) {
		buf[ret] = '\0';
#endif
		std::string line(buf);
		long long current_ms = parse_progress_ms_line(line);
		if (current_ms >= 0 && duration_ms > 0) {
			double percent = start_percent
				+ ((double) current_ms * progress_span / (double) duration_ms);
			update_task_progress(task, percent, progress_msg);
		} else if (line == "progress=end") {
			update_task_progress(task, end_percent, end_msg);
		}

		if (is_task_cancel_requested(task)) {
#ifdef _WIN32
			if (proc != NULL && proc->process != NULL) {
				TerminateProcess(proc->process, 1);
			}
#else
			if (proc->stream->pid > 0) {
				kill((pid_t) proc->stream->pid, SIGTERM);
			}
#endif
		}
	}

#ifdef _WIN32
	DWORD exit_code = 1;
	if (proc != NULL && proc->process != NULL) {
		WaitForSingleObject(proc->process, INFINITE);
		GetExitCodeProcess(proc->process, &exit_code);
	}
	if (proc != NULL) {
		if (proc->read_pipe != NULL) {
			CloseHandle(proc->read_pipe);
		}
		if (proc->thread != NULL) {
			CloseHandle(proc->thread);
		}
		if (proc->process != NULL) {
			CloseHandle(proc->process);
		}
		delete proc;
	}
	return (int) exit_code;
#else
	int code = acl_vstream_pclose(proc->stream);
	delete proc;
	return code;
#endif
}

static ffmpeg_process_t* start_ffmpeg_process(ACL_ARGV* args)
{
#ifdef _WIN32
	SECURITY_ATTRIBUTES sa;
	memset(&sa, 0, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE read_pipe = NULL;
	HANDLE write_pipe = NULL;
	if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
		acl_argv_free(args);
		return NULL;
	}
	SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

	std::string command;
	for (int i = 0; args->argv[i] != NULL; ++i) {
		if (!command.empty()) {
			command.push_back(' ');
		}
		command += quote_windows_arg(args->argv[i]);
	}

	std::wstring wcmd;
	if (!webcool_utf8_to_wide(command.c_str(), wcmd)) {
		CloseHandle(read_pipe);
		CloseHandle(write_pipe);
		acl_argv_free(args);
		return NULL;
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

	BOOL ok = CreateProcessW(NULL, &wcmd[0], NULL, NULL, TRUE,
		CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	CloseHandle(write_pipe);
	acl_argv_free(args);
	if (!ok) {
		CloseHandle(read_pipe);
		return NULL;
	}

	ffmpeg_process_t* proc = new ffmpeg_process_t;
	proc->process = pi.hProcess;
	proc->thread = pi.hThread;
	proc->read_pipe = read_pipe;
	proc->pid = pi.dwProcessId;
	return proc;
#else
	ACL_VSTREAM* stream = acl_vstream_popen(O_RDWR,
		ACL_VSTREAM_POPEN_ARGV, args->argv,
		ACL_VSTREAM_POPEN_END);
	acl_argv_free(args);
	if (stream == NULL) {
		return NULL;
	}
	ffmpeg_process_t* proc = new ffmpeg_process_t;
	proc->stream = stream;
	return proc;
#endif
}

static void run_audio_only_transcode_task(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input_file,
	const std::string& tmp_file, const std::string& output_file)
{
	if (is_task_cancel_requested(task)) {
		::unlink(tmp_file.c_str());
		finish_task(task, false, "已取消", "cancelled", -1);
		return;
	}

	::unlink(tmp_file.c_str());

	long long duration_ms = probe_duration_ms(ffmpeg, input_file);
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
		NULL);

	ffmpeg_process_t* stream = start_ffmpeg_process(args);

	if (stream == NULL) {
		::unlink(tmp_file.c_str());
		finish_task(task, false, "音频转码失败", acl::last_serror(), -1);
		return;
	}

	int code = wait_transcode_progress(task, stream, duration_ms,
		5.0, 91.0, "音频转码中 (转为M4A/AAC)", 96.0, "写入M4A文件");
	if (is_task_cancel_requested(task)) {
		::unlink(tmp_file.c_str());
		finish_task(task, false, "已取消", "cancelled", -1);
		return;
	}
	if (code != 0 || file_size_of(tmp_file.c_str()) <= 0) {
		::unlink(tmp_file.c_str());
		finish_task(task, false, "音频转码失败", "audio only ffmpeg failed", -1);
		return;
	}

	::unlink(output_file.c_str());
	if (::rename(tmp_file.c_str(), output_file.c_str()) != 0) {
		::unlink(tmp_file.c_str());
		finish_task(task, false, "音频转码失败", "rename audio output failed", -1);
		return;
	}

	long long out_size = file_size_of(output_file.c_str());
	if (out_size <= 0) {
		::unlink(output_file.c_str());
		finish_task(task, false, "音频转码失败", "audio output is empty", -1);
		return;
	}

	finish_task(task, true, "音频转码完成，已生成M4A文件", "", out_size);
}

static void run_audio_split_transcode_task(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input_file,
	const std::string& tmp_file, const std::string& output_file,
	const std::string& secondary_output_file)
{
	if (is_task_cancel_requested(task)) {
		::unlink(tmp_file.c_str());
		finish_task(task, false, "已取消", "cancelled", -1);
		return;
	}

	if (secondary_output_file.empty()) {
		finish_task(task, false, "拆分失败", "missing audio output path", -1);
		return;
	}

	const std::string audio_tmp_file = replace_ext(tmp_file, ".m4a");
	::unlink(tmp_file.c_str());
	::unlink(audio_tmp_file.c_str());

	long long duration_ms = probe_duration_ms(ffmpeg, input_file);
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
		NULL);

	ffmpeg_process_t* stream = start_ffmpeg_process(args);

	if (stream == NULL) {
		::unlink(tmp_file.c_str());
		::unlink(audio_tmp_file.c_str());
		finish_task(task, false, "拆分失败", acl::last_serror(), -1);
		return;
	}

	int code = wait_transcode_progress(task, stream, duration_ms,
		5.0, 91.0, "拆分中 (复用视频并转M4A/AAC音频)", 96.0,
		"写入拆分文件");
	if (is_task_cancel_requested(task)) {
		::unlink(tmp_file.c_str());
		::unlink(audio_tmp_file.c_str());
		finish_task(task, false, "已取消", "cancelled", -1);
		return;
	}
	if (code != 0 || file_size_of(tmp_file.c_str()) <= 0
		|| file_size_of(audio_tmp_file.c_str()) <= 0)
	{
		::unlink(tmp_file.c_str());
		::unlink(audio_tmp_file.c_str());
		finish_task(task, false, "拆分失败", "split ffmpeg failed", -1);
		return;
	}

	::unlink(output_file.c_str());
	::unlink(secondary_output_file.c_str());
	if (::rename(tmp_file.c_str(), output_file.c_str()) != 0) {
		::unlink(tmp_file.c_str());
		::unlink(audio_tmp_file.c_str());
		finish_task(task, false, "拆分失败", "rename video output failed", -1);
		return;
	}
	if (::rename(audio_tmp_file.c_str(), secondary_output_file.c_str()) != 0) {
		::unlink(audio_tmp_file.c_str());
		::unlink(output_file.c_str());
		finish_task(task, false, "拆分失败", "rename audio output failed", -1);
		return;
	}

	long long video_size = file_size_of(output_file.c_str());
	long long audio_size = file_size_of(secondary_output_file.c_str());
	if (video_size <= 0 || audio_size <= 0) {
		::unlink(output_file.c_str());
		::unlink(secondary_output_file.c_str());
		finish_task(task, false, "拆分失败", "split output is empty", -1);
		return;
	}

	std::string vtt_path;
	std::string subtitle_err;
	int subtitle_status = export_vtt_sidecar(ffmpeg, input_file, output_file,
		vtt_path, subtitle_err);
	if (subtitle_status > 0) {
		finish_task(task, true, "拆分完成，已生成独立视频、M4A音频和VTT字幕", "", video_size + audio_size);
	} else if (subtitle_status == 0) {
		finish_task(task, true, "拆分完成，已生成独立视频和M4A音频", "", video_size + audio_size);
	} else {
		finish_task(task, true, "拆分完成，已生成独立视频和M4A音频（字幕导出失败）", "", video_size + audio_size);
	}
	return;
}

static void run_audio_transcode_task(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input_file,
	const std::string& tmp_file, const std::string& output_file,
	const std::string& secondary_output_file,
	const transcode_strategy_t& strategy)
{
	if (strategy.mode == transcode_strategy_t::audio_only) {
		printf(">>>>>audio only\n");
		run_audio_only_transcode_task(task, ffmpeg, input_file, tmp_file,
			output_file);
		return;
	}

	printf(">>>audio split\n");
	run_audio_split_transcode_task(task, ffmpeg, input_file, tmp_file,
		output_file, secondary_output_file);
}

static void run_video_transcode_task(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input_file,
	const std::string& tmp_file, const std::string& output_file)
{
	if (is_task_cancel_requested(task)) {
		::unlink(tmp_file.c_str());
		finish_task(task, false, "已取消", "cancelled", -1);
		return;
	}

	long long duration_ms = probe_duration_ms(ffmpeg, input_file);
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
		NULL);

	const char* strategy_msg = "视频转码中 (转换为H.264/AAC MP4)";
	acl_argv_add(args,
		"-c:v", "libx264",
		"-preset", "veryfast",
		"-crf", "23",
		"-pix_fmt", "yuv420p",
		NULL);

	acl_argv_add(args,
		"-movflags", "+faststart",
		"-c:a", "aac",
		"-ac", "2",
		"-b:a", "192k",
		"-c:s", "mov_text",
		"-progress", "pipe:1",
		"-nostats",
		tmp_file.c_str(),
		NULL);

	ffmpeg_process_t* stream = start_ffmpeg_process(args);

	if (stream == NULL) {
		::unlink(tmp_file.c_str());
		finish_task(task, false, "转码启动失败", acl::last_serror(), -1);
		return;
	}

	int code = wait_transcode_progress(task, stream, duration_ms,
		0.1, 100.0, strategy_msg, 99.5, "写入输出文件");
	if (is_task_cancel_requested(task)) {
		::unlink(tmp_file.c_str());
		finish_task(task, false, "已取消", "cancelled", -1);
		return;
	}
	if (code != 0) {
		::unlink(tmp_file.c_str());
		finish_task(task, false, "转码失败", "ffmpeg failed", -1);
		return;
	}

	if (::rename(tmp_file.c_str(), output_file.c_str()) != 0) {
		::unlink(tmp_file.c_str());
		finish_task(task, false, "转码失败", "rename transcoded file failed", -1);
		return;
	}

	// Keep source file untouched. Output is a separate transcoded file.

	long long out_size = file_size_of(output_file.c_str());
	if (out_size <= 0) {
		finish_task(task, false, "转码失败", "transcoded file is empty", -1);
		return;
	}

	std::string vtt_path;
	std::string subtitle_err;
	int subtitle_status = export_vtt_sidecar(ffmpeg, input_file, output_file,
		vtt_path, subtitle_err);

	if (subtitle_status > 0) {
		finish_task(task, true, "转码完成，已导出VTT外挂字幕", "", out_size);
	} else if (subtitle_status == 0) {
		finish_task(task, true, "转码完成（未检测到字幕流）", "", out_size);
	} else {
		finish_task(task, true, "转码完成（字幕导出失败）", "", out_size);
	}
}

static void run_transcode_task(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input_file,
	const std::string& tmp_file, const std::string& output_file,
	const std::string& secondary_output_file,
	const transcode_strategy_t& strategy)
{
	if (strategy.mode == transcode_strategy_t::full_mp4) {
		run_video_transcode_task(task, ffmpeg, input_file, tmp_file,
			output_file);
		return;
	}

	run_audio_transcode_task(task, ffmpeg, input_file, tmp_file,
		output_file, secondary_output_file, strategy);
}

static bool snapshot_task_by_id(const char* task_id, const std::string& scope,
	transcode_task_snapshot_t& snapshot)
{
	if (task_id == NULL || *task_id == '\0') {
		return false;
	}
	std::lock_guard<std::mutex> guard(g_transcode_mutex);
	std::map<std::string, std::shared_ptr<transcode_task_t> >::iterator it =
		g_transcode_tasks.find(task_id);
	if (it == g_transcode_tasks.end() || !it->second) {
		return false;
	}
	if (it->second->scope != scope) {
		return false;
	}
	snapshot.id = it->second->id;
	snapshot.scope = it->second->scope;
	snapshot.file_name = it->second->file_name;
	snapshot.output_name = it->second->output_name;
	snapshot.secondary_output_name = it->second->secondary_output_name;
	snapshot.error = it->second->error;
	snapshot.message = it->second->message;
	snapshot.progress = it->second->progress;
	snapshot.process_pid = it->second->process_pid;
	snapshot.done = it->second->done;
	snapshot.success = it->second->success;
	snapshot.cancel_requested = it->second->cancel_requested;
	snapshot.local = it->second->local;
	snapshot.size = it->second->size;
	return true;
}

static void snapshot_running_tasks(const std::string& scope,
	std::vector<transcode_task_snapshot_t>& out) {
	out.clear();
	std::lock_guard<std::mutex> guard(g_transcode_mutex);
	for (std::map<std::string, std::shared_ptr<transcode_task_t> >::iterator it =
		g_transcode_tasks.begin(); it != g_transcode_tasks.end(); ++it)
	{
		if (!it->second || it->second->done || it->second->scope != scope) {
			continue;
		}
		transcode_task_snapshot_t snapshot;
		snapshot.id = it->second->id;
		snapshot.scope = it->second->scope;
		snapshot.file_name = it->second->file_name;
		snapshot.output_name = it->second->output_name;
		snapshot.secondary_output_name = it->second->secondary_output_name;
		snapshot.error = it->second->error;
		snapshot.message = it->second->message;
		snapshot.progress = it->second->progress;
		snapshot.process_pid = it->second->process_pid;
		snapshot.done = it->second->done;
		snapshot.success = it->second->success;
		snapshot.cancel_requested = it->second->cancel_requested;
		snapshot.local = it->second->local;
		snapshot.size = it->second->size;
		out.push_back(snapshot);
	}
}

static bool request_cancel_task(const char* task_id, const std::string& scope,
	transcode_task_snapshot_t& snapshot, bool& signal_sent)
{
	signal_sent = false;
	if (task_id == NULL || *task_id == '\0') {
		return false;
	}

	long pid = -1;
	{
		std::lock_guard<std::mutex> guard(g_transcode_mutex);
		std::map<std::string, std::shared_ptr<transcode_task_t> >::iterator it =
			g_transcode_tasks.find(task_id);
		if (it == g_transcode_tasks.end() || !it->second) {
			return false;
		}
		if (it->second->scope != scope) {
			return false;
		}
		it->second->cancel_requested = true;
		it->second->message = "取消中";
		pid = it->second->process_pid;
		snapshot.id = it->second->id;
		snapshot.scope = it->second->scope;
		snapshot.file_name = it->second->file_name;
		snapshot.output_name = it->second->output_name;
		snapshot.secondary_output_name = it->second->secondary_output_name;
		snapshot.error = it->second->error;
		snapshot.message = it->second->message;
		snapshot.progress = it->second->progress;
		snapshot.process_pid = it->second->process_pid;
		snapshot.done = it->second->done;
		snapshot.success = it->second->success;
		snapshot.cancel_requested = it->second->cancel_requested;
		snapshot.local = it->second->local;
		snapshot.size = it->second->size;
	}

#ifndef _WIN32
	if (!snapshot.done && pid > 0) {
		signal_sent = kill((pid_t) pid, SIGTERM) == 0;
	}
#endif
	return true;
}

static std::string replace_ext_with_mp4(const std::string& name) {
	size_t slash = name.find_last_of("/\\");
	size_t dot = name.find_last_of('.');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
		return name + ".mp4";
	}
	return name.substr(0, dot) + ".mp4";
}

static bool path_exists(const std::string& path) {
	return access(path.c_str(), F_OK) == 0;
}

static std::string make_unique_transcoded_name(const std::string& upload_dir,
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
		candidate = stem + "_web_" + std::to_string(i) + ext;
		full = upload_dir + "/" + candidate;
		if (!path_exists(full)) {
			return candidate;
		}
	}

	char buf[64];
	snprintf(buf, sizeof(buf), "_web_%lu", (unsigned long) g_transcode_seq.load());
	return stem + std::string(buf) + ext;
}

static std::string make_unique_audio_only_name(const std::string& upload_dir,
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
		(unsigned long) g_transcode_seq.load());
	return stem + std::string(buf);
}

static void make_unique_split_output_names(const std::string& upload_dir,
	const std::string& input_name, std::string& video_name,
	std::string& audio_name)
{
	video_name.clear();
	audio_name.clear();
	const std::string stem = replace_ext(input_name, "");
	for (int i = 1; i < 10000; ++i) {
		const std::string suffix = i == 1 ? "" : ("_" + std::to_string(i));
		const std::string candidate_video = stem + "_web_video" + suffix + ".mp4";
		const std::string candidate_audio = stem + "_web_audio" + suffix + ".m4a";
		if (!path_exists(join_upload_path(upload_dir, candidate_video))
			&& !path_exists(join_upload_path(upload_dir, candidate_audio)))
		{
			video_name = candidate_video;
			audio_name = candidate_audio;
			return;
		}
	}
	char buf[64];
	snprintf(buf, sizeof(buf), "_%lu", (unsigned long) g_transcode_seq.load());
	video_name = stem + "_web_video" + std::string(buf) + ".mp4";
	audio_name = stem + "_web_audio" + std::string(buf) + ".m4a";
}

static std::string make_unique_local_transcoded_path(const std::string& input_path)
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
		const std::string candidate = stem + "_web_" + std::to_string(i) + ext;
		if (!path_exists(candidate)) {
			return candidate;
		}
	}
	char buf[64];
	snprintf(buf, sizeof(buf), "_web_%lu", (unsigned long) g_transcode_seq.load());
	return stem + std::string(buf) + ext;
}

static bool send_existing_local_mp4(const std::string& path, response_t& res)
{
	FILE* fp = fopen(path.c_str(), "rb");
	if (fp == NULL) {
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

	char buf[64 * 1024];
	bool ok = true;
	while (!feof(fp)) {
		const size_t n = fread(buf, 1, sizeof(buf), fp);
		if (n > 0 && !res.write(buf, n)) {
			ok = false;
			break;
		}
		if (n < sizeof(buf)) {
			if (ferror(fp)) {
				ok = false;
			}
			break;
		}
	}
	fclose(fp);
	return ok ? res.write(NULL, 0) : false;
}

static bool is_active_stream_sidecar(const std::string& path)
{
	std::lock_guard<std::mutex> guard(g_transcode_mutex);
	return g_active_stream_sidecars.find(path) != g_active_stream_sidecars.end();
}

static void register_stream_sidecar(const std::string& path)
{
	std::lock_guard<std::mutex> guard(g_transcode_mutex);
	g_active_stream_sidecars.insert(path);
}

static void unregister_stream_sidecar(const std::string& path)
{
	std::lock_guard<std::mutex> guard(g_transcode_mutex);
	g_active_stream_sidecars.erase(path);
}

static void cleanup_local_stream_sidecars(const std::string& parent)
{
	DIR* dir = opendir(parent.c_str());
	if (dir == NULL) {
		return;
	}
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		const char* name = entry->d_name;
		const bool stream_tmp = strncmp(name, ".streaming_tmp.", 15) == 0;
		const bool stream_progress = strncmp(name, ".streaming_progress.", 20) == 0;
		if (!stream_tmp && !stream_progress) {
			continue;
		}
		const std::string full = local_join_path(parent, name);
		if (is_active_stream_sidecar(full)) {
			continue;
		}
		struct stat st;
		if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
			continue;
		}
		::unlink(full.c_str());
	}
	closedir(dir);
}

static void cleanup_current_stream_sidecars(const std::string& tmp_path,
	const std::string& progress_path, bool keep_files)
{
	if (!keep_files) {
		::unlink(tmp_path.c_str());
		::unlink(progress_path.c_str());
	}
	unregister_stream_sidecar(tmp_path);
	unregister_stream_sidecar(progress_path);
}

static bool remux_mp4_faststart(const std::string& ffmpeg,
	const std::string& input_path, const std::string& output_path,
	std::string& err)
{
	err.clear();
	::unlink(output_path.c_str());
	std::string cmd = shell_quote(ffmpeg)
		+ " -hide_banner -loglevel error -y -i " + shell_quote(input_path)
		+ " -map 0 -c copy -movflags +faststart " + shell_quote(output_path)
		+ " 2>&1";
	std::string out;
	const int code = run_command_capture(cmd, out);
	if (code != 0 || file_size_of(output_path.c_str()) <= 0) {
		err = trim_text(out);
		if (err.empty()) {
			err = "faststart remux failed";
		}
		::unlink(output_path.c_str());
		return false;
	}
	return true;
}

static void update_stream_task_progress_from_file(
	const std::shared_ptr<transcode_task_t>& task,
	const std::string& progress_path, long long duration_ms)
{
	if (!task || duration_ms <= 0) {
		return;
	}
	FILE* fp = fopen(progress_path.c_str(), "r");
	if (fp == NULL) {
		return;
	}
	char line_buf[512];
	long long current_ms = -1;
	bool ended = false;
	while (fgets(line_buf, (int) sizeof(line_buf), fp) != NULL) {
		std::string line = trim_text(line_buf);
		long long line_ms = parse_progress_ms_line(line);
		if (line_ms >= 0) {
			current_ms = line_ms;
		} else if (line == "progress=end") {
			ended = true;
		}
	}
	fclose(fp);
	if (ended) {
		update_task_progress(task, 99.5, "写入输出文件");
		return;
	}
	if (current_ms >= 0) {
		const double percent = (double) current_ms * 100.0 / (double) duration_ms;
		update_task_progress(task, percent, "边转边看中");
	}
}

static void json_error(response_t& res, int status, const char* msg,
	bool keep_alive)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", msg);
	sendJson(res, status, root, keep_alive);
}

static bool validate_local_stream_state_request(request_t& req,
	const std::string& upload_dir, std::string& local_path,
	std::string& err, int& status)
{
	status = 400;
	if (!normalize_local_video_path(req.getParameter("path"), local_path, err)) {
		return false;
	}
	struct stat st;
	if (stat(local_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		err = "source video not found";
		status = 404;
		return false;
	}
	if (!is_local_convertible_video_name(local_path.c_str())) {
		err = "local video must be rmvb, rm, or avi";
		status = 400;
		return false;
	}
	bool dir_allowed = false;
	std::string locked_dir;
	std::string lock_err;
	if (!local_dir_lock_path_allows(upload_dir, local_parent_path(local_path),
		req.getParameter("local_dir_password") ? req.getParameter("local_dir_password") : "",
		dir_allowed, locked_dir, lock_err))
	{
		err = lock_err;
		status = 500;
		return false;
	}
	if (!dir_allowed) {
		err = "directory is locked";
		status = 403;
		return false;
	}
	bool file_allowed = false;
	if (!file_lock_path_allows(upload_dir, local_file_lock_key(local_path),
		req.getParameter("file_password") ? req.getParameter("file_password") : "",
		file_allowed, lock_err))
	{
		err = lock_err;
		status = 500;
		return false;
	}
	if (!file_allowed) {
		err = "file is locked";
		status = 403;
		return false;
	}
	return true;
}

bool VideoConvertAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	const char* file = req.getParameter("file");
	if (file == NULL || *file == '\0') {
		json_error(res, 400, "missing query parameter: file", req.isKeepAlive());
		return true;
	}

	std::string file_path;
	std::string path_err;
	if (!normalize_relative_path(file, file_path, path_err, false)) {
		json_error(res, 400, path_err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!resolve_upload_regular_file_path(upload_dir, file_path, file_path)) {
		json_error(res, 404, "source video not found", req.isKeepAlive());
		return true;
	}

	const std::string basename = base_name_from_relative_path(file_path);
	if (!is_video_name(basename.c_str())) {
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

	std::string probe_reason;
	if (browser_can_play_video_by_probe(ffmpeg, in_path.c_str(), probe_reason)) {
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_bool("started", false);
		root.add_bool("completed", true);
		root.add_bool("playable", true);
		root.add_text("name", file_path.c_str());
		root.add_text("message", "video already playable");
		return sendJson(res, 200, root, req.isKeepAlive());
	}

	{
		const std::string task_key = scoped_task_key(upload_dir, file_path);
		std::lock_guard<std::mutex> guard(g_transcode_mutex);
		std::map<std::string, std::string>::iterator it =
			g_running_task_by_file.find(task_key);
		if (it != g_running_task_by_file.end()) {
			std::map<std::string, std::shared_ptr<transcode_task_t> >::iterator task_it =
				g_transcode_tasks.find(it->second);
			if (task_it != g_transcode_tasks.end() && !task_it->second->done) {
				acl::json json;
				acl::json_node& root = json.create_node();
				root.add_bool("ok", true);
				root.add_bool("started", false);
				root.add_bool("running", true);
				root.add_text("task_id", task_it->second->id.c_str());
				root.add_text("name", task_it->second->output_name.c_str());
				root.add_number("progress", (long long) task_it->second->progress);
				root.add_bool("cancel_requested", task_it->second->cancel_requested);
				root.add_text("message", task_it->second->message.c_str());
				return sendJson(res, 200, root, req.isKeepAlive());
			}
		}
	}

	transcode_strategy_t strategy;
	probe_transcode_strategy(ffmpeg, in_path, strategy, true);
	const char* requested_mode = req.getParameter("mode");
	if (requested_mode != NULL && strcmp(requested_mode, "audio_only") == 0) {
		strategy.mode = transcode_strategy_t::audio_only;
	}
	if (requested_mode != NULL && strcmp(requested_mode, "audio_split") == 0) {
		strategy.mode = transcode_strategy_t::audio_split;
	}
	if (requested_mode != NULL && strcmp(requested_mode, "full_mp4") == 0) {
		strategy.mode = transcode_strategy_t::full_mp4;
	}

	std::string output_name;
	std::string secondary_output_name;
	if (strategy.mode == transcode_strategy_t::audio_only) {
		output_name = make_unique_audio_only_name(upload_dir, file_path);
	} else if (strategy.mode == transcode_strategy_t::audio_split) {
		make_unique_split_output_names(upload_dir, file_path, output_name,
			secondary_output_name);
	} else {
		output_name = make_unique_transcoded_name(upload_dir, file_path);
	}
	const std::string out_path = join_upload_path(upload_dir, output_name);
	const std::string out_dir = local_parent_path(out_path);
	const std::string secondary_out_path = secondary_output_name.empty()
		? std::string() : join_upload_path(upload_dir, secondary_output_name);

	acl::string tmp_path;
	if (strategy.mode == transcode_strategy_t::audio_only) {
		tmp_path.format("%s/.transcoding_tmp.%u.%lu.m4a", out_dir.c_str(),
			(unsigned) getpid(), (unsigned long) g_transcode_seq.load());
	} else {
		tmp_path.format("%s/.transcoding_tmp.%u.%lu.mp4", out_dir.c_str(),
			(unsigned) getpid(), (unsigned long) g_transcode_seq.load());
	}

	std::shared_ptr<transcode_task_t> task(new transcode_task_t);
	task->id = make_task_id();
	task->scope = upload_dir;
	task->file_name = file_path;
	task->output_name = output_name;
	task->secondary_output_name = secondary_output_name;
	task->message = "等待后台转码";

	{
		std::lock_guard<std::mutex> guard(g_transcode_mutex);
		g_transcode_tasks[task->id] = task;
		g_running_task_by_file[scoped_task_key(task->scope, task->file_name)] = task->id;
	}

	const std::string input_path(in_path);
	const std::string temp_path(tmp_path.c_str());
	const std::string output_path(out_path);
	const std::string secondary_output_path(secondary_out_path);

	// Run conversion in a dedicated fiber and execute heavy work in thread.
	go[task, ffmpeg, input_path, temp_path, output_path, secondary_output_path, strategy] {
		acl::gofiber_wait_thread([task, ffmpeg, input_path, temp_path, output_path, secondary_output_path, strategy] {
			run_transcode_task(task, ffmpeg, input_path, temp_path, output_path,
				secondary_output_path, strategy);
		});
	};

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_bool("started", true);
	root.add_bool("running", true);
	root.add_bool("playable", false);
	root.add_text("task_id", task->id.c_str());
	root.add_text("name", output_name.c_str());
	if (!secondary_output_name.empty()) {
		root.add_text("secondary_name", secondary_output_name.c_str());
	}
	root.add_bool("cancel_requested", false);
	root.add_number("progress", 0);
	root.add_text("message", strategy.mode == transcode_strategy_t::audio_only
		? "audio only task started"
		: (strategy.mode == transcode_strategy_t::audio_split
			? "audio split task started" : "transcode task started"));
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskVideoConvertAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string local_path;
	std::string err;
	if (!normalize_local_video_path(req.getParameter("path"), local_path, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	struct stat st;
	if (stat(local_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		json_error(res, 404, "source video not found", req.isKeepAlive());
		return true;
	}
	if (!is_local_convertible_video_name(local_path.c_str())) {
		json_error(res, 400, "local video must be rmvb, rm, or avi", req.isKeepAlive());
		return true;
	}

	const std::string parent = local_parent_path(local_path);
	bool dir_allowed = false;
	std::string locked_dir;
	std::string lock_err;
	if (!local_dir_lock_path_allows(upload_dir, parent,
		req.getParameter("local_dir_password") ? req.getParameter("local_dir_password") : "",
		dir_allowed, locked_dir, lock_err))
	{
		json_error(res, 500, lock_err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!dir_allowed) {
		json_error(res, 403, "directory is locked", req.isKeepAlive());
		return true;
	}

	bool file_allowed = false;
	if (!file_lock_path_allows(upload_dir, local_file_lock_key(local_path),
		req.getParameter("file_password") ? req.getParameter("file_password") : "",
		file_allowed, lock_err))
	{
		json_error(res, 500, lock_err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!file_allowed) {
		json_error(res, 403, "file is locked", req.isKeepAlive());
		return true;
	}

	const std::string ffmpeg = choose_ffmpeg_path();
	if (ffmpeg.empty()) {
		json_error(res, 500, "ffmpeg not found in tools directory", req.isKeepAlive());
		return true;
	}

	const std::string task_key = std::string("local:") + local_path;
	{
		const std::string scoped_key = scoped_task_key(upload_dir, task_key);
		std::lock_guard<std::mutex> guard(g_transcode_mutex);
		std::map<std::string, std::string>::iterator it =
			g_running_task_by_file.find(scoped_key);
		if (it != g_running_task_by_file.end()) {
			std::map<std::string, std::shared_ptr<transcode_task_t> >::iterator task_it =
				g_transcode_tasks.find(it->second);
			if (task_it != g_transcode_tasks.end() && !task_it->second->done) {
				acl::json json;
				acl::json_node& root = json.create_node();
				root.add_bool("ok", true);
				root.add_bool("started", false);
				root.add_bool("running", true);
				root.add_bool("local", true);
				root.add_text("task_id", task_it->second->id.c_str());
				root.add_text("name", task_it->second->output_name.c_str());
				root.add_number("progress", (long long) task_it->second->progress);
				root.add_bool("cancel_requested", task_it->second->cancel_requested);
				root.add_text("message", task_it->second->message.c_str());
				return sendJson(res, 200, root, req.isKeepAlive());
			}
		}
	}

	const std::string output_path = make_unique_local_transcoded_path(local_path);
	acl::string tmp_path;
	tmp_path.format("%s/.transcoding_tmp.%u.%lu.mp4", parent.c_str(),
		(unsigned) getpid(), (unsigned long) g_transcode_seq.load());

	std::shared_ptr<transcode_task_t> task(new transcode_task_t);
	task->id = make_task_id();
	task->scope = upload_dir;
	task->file_name = task_key;
	task->output_name = output_path;
	task->message = "等待后台转码";
	task->local = true;

	{
		std::lock_guard<std::mutex> guard(g_transcode_mutex);
		g_transcode_tasks[task->id] = task;
		g_running_task_by_file[scoped_task_key(task->scope, task->file_name)] = task->id;
	}

	transcode_strategy_t strategy;
	probe_transcode_strategy(ffmpeg, local_path, strategy, false);

	const std::string input_path(local_path);
	const std::string temp_path(tmp_path.c_str());
	go[task, ffmpeg, input_path, temp_path, output_path, strategy] {
		acl::gofiber_wait_thread([task, ffmpeg, input_path, temp_path, output_path, strategy] {
			run_transcode_task(task, ffmpeg, input_path, temp_path, output_path,
				"", strategy);
		});
	};

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_bool("started", true);
	root.add_bool("running", true);
	root.add_bool("local", true);
	root.add_bool("playable", false);
	root.add_text("task_id", task->id.c_str());
	root.add_text("name", output_path.c_str());
	root.add_bool("cancel_requested", false);
	root.add_number("progress", 0);
	root.add_text("message", "local transcode task started");
	// 删除同名 .meta 文件（全量转码完成后）
	const std::string meta_path = local_stream_state_path(local_path);
	::unlink(meta_path.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskVideoStreamAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string local_path;
	std::string err;
	if (!normalize_local_video_path(req.getParameter("path"), local_path, err)) {
		return sendText(res, 400, err.c_str(), false);
	}

	struct stat st;
	if (stat(local_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		return sendText(res, 404, "source video not found\n", false);
	}
	if (!is_local_convertible_video_name(local_path.c_str())) {
		return sendText(res, 400, "local video must be rmvb, rm, or avi\n", false);
	}

	const std::string parent = local_parent_path(local_path);
	bool dir_allowed = false;
	std::string locked_dir;
	std::string lock_err;
	if (!local_dir_lock_path_allows(upload_dir, parent,
		req.getParameter("local_dir_password") ? req.getParameter("local_dir_password") : "",
		dir_allowed, locked_dir, lock_err))
	{
		return sendText(res, 500, lock_err.c_str(), false);
	}
	if (!dir_allowed) {
		return sendText(res, 403, "directory is locked\n", false);
	}

	bool file_allowed = false;
	if (!file_lock_path_allows(upload_dir, local_file_lock_key(local_path),
		req.getParameter("file_password") ? req.getParameter("file_password") : "",
		file_allowed, lock_err))
	{
		return sendText(res, 500, lock_err.c_str(), false);
	}
	if (!file_allowed) {
		return sendText(res, 403, "file is locked\n", false);
	}

	const std::string ffmpeg = choose_ffmpeg_path();
	if (ffmpeg.empty()) {
		return sendText(res, 500, "ffmpeg not found in tools directory\n", false);
	}

	cleanup_local_stream_sidecars(parent);
	const long long start_position_ms = read_local_stream_position_ms(local_path);
	const std::string output_path = replace_ext_with_mp4(local_path);
	if (file_size_of(output_path.c_str()) > 0) {
		remove_local_stream_position(local_path);
		return send_existing_local_mp4(output_path, res);
	}
	const std::string tmp_path = local_stream_tmp_mp4_path(local_path);
	const std::string progress_path = local_stream_state_path(local_path);
	const bool append_existing_tmp = start_position_ms > 0
		&& file_size_of(tmp_path.c_str()) > 0;
	register_stream_sidecar(tmp_path.c_str());
	register_stream_sidecar(progress_path.c_str());

	std::shared_ptr<transcode_task_t> task;
	const char* task_id_param = req.getParameter("stream_task_id");
	if (task_id_param != NULL && *task_id_param != '\0') {
		task.reset(new transcode_task_t);
		task->id = task_id_param;
		task->scope = upload_dir;
		task->file_name = std::string("local-stream:") + local_path;
		task->output_name = output_path;
		task->message = "边转边看准备中";
		task->local = true;
		{
			std::lock_guard<std::mutex> guard(g_transcode_mutex);
			g_transcode_tasks[task->id] = task;
		}
	}

	std::string command = shell_quote(ffmpeg)
		+ " -hide_banner -loglevel error -nostdin";
	if (start_position_ms > 0) {
		char ss_buf[64];
		snprintf(ss_buf, sizeof(ss_buf), "%.3f", (double) start_position_ms / 1000.0);
		command += " -ss ";
		command += ss_buf;
	}
	command += " -i " + shell_quote(local_path)
		+ " -map 0:v:0 -map 0:a:0? -dn"
		+ " -c:v libx264 -preset veryfast -crf 23 -pix_fmt yuv420p"
		+ " -c:a aac -ac 2 -b:a 192k"
		+ " -progress " + shell_quote(progress_path.c_str())
		+ " -nostats"
		+ " -movflags frag_keyframe+empty_moov+default_base_moof"
		+ " -f mp4 pipe:1 2>/dev/null";

	FILE* fp = popen(command.c_str(), "r");
	if (fp == NULL) {
		cleanup_current_stream_sidecars(tmp_path.c_str(), progress_path.c_str(), false);
		if (task) {
			finish_task(task, false, "转码启动失败", "failed to start ffmpeg", -1);
		}
		return sendText(res, 500, "failed to start ffmpeg\n", false);
	}

	FILE* out = fopen(tmp_path.c_str(), append_existing_tmp ? "ab" : "wb");
	if (out == NULL) {
		pclose(fp);
		cleanup_current_stream_sidecars(tmp_path.c_str(), progress_path.c_str(), false);
		if (task) {
			finish_task(task, false, "转码失败", "failed to create output file", -1);
		}
		return sendText(res, 500, "failed to create output file\n", false);
	}

	const long long duration_ms = probe_duration_ms(ffmpeg, local_path);
	const long long remaining_duration_ms = duration_ms > start_position_ms
		? (duration_ms - start_position_ms) : duration_ms;
	if (task) {
		update_task_progress(task, 0.1, "边转边看中");
	}

	res.setStatus(200)
		.setKeepAlive(false)
		.setContentType("video/mp4")
		.setHeader("Content-Disposition", "inline")
		.setHeader("Cache-Control", "no-store")
		.setHeader("Accept-Ranges", "none");

	bool ok = true;
	bool client_ok = true;

	char buf[64 * 1024];
	while (true) {
		const size_t n = fread(buf, 1, sizeof(buf), fp);
		if (n > 0) {
			if (fwrite(buf, 1, n, out) != n) {
				ok = false;
				break;
			}
			if (client_ok && !res.write(buf, n)) {
				client_ok = false;
				ok = false;
				break;
			}
			if (task) {
				update_stream_task_progress_from_file(task, progress_path.c_str(),
					remaining_duration_ms);
			}
		}
		if (n < sizeof(buf)) {
			if (ferror(fp)) {
				ok = false;
			}
			break;
		}
	}
	const int code = pclose(fp);
	if (fclose(out) != 0) {
		ok = false;
	}
	const bool preserve_partial_files = !client_ok;
	if (code == 0 && ok && file_size_of(tmp_path.c_str()) > 0) {
		std::string remux_err;
		if (remux_mp4_faststart(ffmpeg, tmp_path.c_str(), output_path, remux_err)) {
			::unlink(tmp_path.c_str());
		} else if (::rename(tmp_path.c_str(), output_path.c_str()) != 0) {
			::unlink(tmp_path.c_str());
			ok = false;
		}
	} else {
		if (!preserve_partial_files) {
			::unlink(tmp_path.c_str());
		}
		ok = false;
	}
	cleanup_current_stream_sidecars(tmp_path.c_str(), progress_path.c_str(), preserve_partial_files);
	cleanup_local_stream_sidecars(parent);
	if (ok) {
		// 删除同名 .meta 文件（边转边看完成后）
		remove_local_stream_position(local_path);
	}
	if (task) {
		finish_task(task, ok, ok ? "边转边看完成" : "边转边看失败",
			ok ? "" : (client_ok ? "stream transcode failed" : "client disconnected"),
			ok ? file_size_of(output_path.c_str()) : -1);
	}
	if (client_ok) {
		return res.write(NULL, 0);
	}
	return ok;
}

bool LocalDiskVideoStreamStateAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string local_path;
	std::string err;
	int status = 400;
	if (!validate_local_stream_state_request(req, upload_dir, local_path, err, status)) {
		json_error(res, status, err.c_str(), req.isKeepAlive());
		return true;
	}

	if (req.getParameter("position_ms") != NULL) {
		long long position_ms = atoll(req.getParameter("position_ms")
			? req.getParameter("position_ms") : "0");
		if (position_ms < 0) {
			position_ms = 0;
		}
		if (!write_local_stream_position_ms(local_path, position_ms, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	}

	const long long saved_ms = read_local_stream_position_ms(local_path);
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", local_path.c_str());
	root.add_number("position_ms", saved_ms);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool VideoConvertProgressAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	const char* task_id = req.getParameter("task_id");
	transcode_task_snapshot_t snapshot;
	if (!snapshot_task_by_id(task_id, upload_dir, snapshot)) {
		json_error(res, 404, "transcode task not found", req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", snapshot.id.c_str());
	root.add_text("file", snapshot.file_name.c_str());
	root.add_text("name", snapshot.output_name.c_str());
	root.add_bool("done", snapshot.done);
	root.add_bool("success", snapshot.success);
	root.add_bool("cancel_requested", snapshot.cancel_requested);
	root.add_bool("local", snapshot.local);
	root.add_number("progress", (long long) snapshot.progress);
	root.add_text("message", snapshot.message.c_str());
	if (!snapshot.secondary_output_name.empty()) {
		root.add_text("secondary_name", snapshot.secondary_output_name.c_str());
	}
	if (!snapshot.error.empty()) {
		root.add_text("error", snapshot.error.c_str());
	}
	if (snapshot.size >= 0) {
		root.add_number("size", snapshot.size);
	}
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool VideoConvertTasksAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::vector<transcode_task_snapshot_t> tasks;
	snapshot_running_tasks(upload_dir, tasks);

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	acl::json_node& arr = json.create_array();
	root.add_child("tasks", arr);
	for (size_t i = 0; i < tasks.size(); ++i) {
		acl::json_node& node = arr.add_child(false, true);
		node.add_text("task_id", tasks[i].id.c_str());
		node.add_text("file", tasks[i].file_name.c_str());
		node.add_text("name", tasks[i].output_name.c_str());
		node.add_bool("done", tasks[i].done);
		node.add_bool("success", tasks[i].success);
		node.add_bool("cancel_requested", tasks[i].cancel_requested);
		node.add_bool("local", tasks[i].local);
		node.add_number("progress", (long long) tasks[i].progress);
		node.add_text("message", tasks[i].message.c_str());
		if (!tasks[i].secondary_output_name.empty()) {
			node.add_text("secondary_name", tasks[i].secondary_output_name.c_str());
		}
	}
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool VideoConvertCancelAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	const char* task_id = req.getParameter("task_id");
	transcode_task_snapshot_t snapshot;
	bool signal_sent = false;
	if (!request_cancel_task(task_id, upload_dir, snapshot, signal_sent)) {
		json_error(res, 404, "transcode task not found", req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", snapshot.id.c_str());
	root.add_bool("done", snapshot.done);
	root.add_bool("cancel_requested", true);
	root.add_bool("local", snapshot.local);
	root.add_bool("signal_sent", signal_sent);
	root.add_text("message", snapshot.done ? "task already finished" : "cancel requested");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

#include "stdafx.h"
#ifndef _WIN32
#include <csignal>
#endif

#include "convert_common.h"

namespace action {

#ifdef _WIN32
std::string quote_windows_arg(const char* arg) {
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

bool read_ffmpeg_line(ffmpeg_process_t* proc, char* buf, size_t size) {
	if (proc == nullptr || proc->read_pipe == nullptr
		  || buf == nullptr || size == 0) {
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
		if (!ReadFile(proc->read_pipe, tmp, sizeof(tmp), &n, nullptr) || n == 0) {
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

int wait_transcode_progress(const std::shared_ptr<transcode_task_t>& task,
	ffmpeg_process_t& proc, const long long duration_ms,
	const double start_percent, const double progress_span,
	const char* progress_msg, const double end_percent,
	const char* end_msg)
{
#ifdef _WIN32
	set_task_process_pid(task, proc.pid > 0 ? static_cast<long>(proc.pid) : -1);
#else
	set_task_process_pid(task, proc.stream
		? static_cast<long>(proc.stream->pid) : -1);
#endif
	update_task_progress(task, start_percent, progress_msg);

	char buf[4096];
#ifdef _WIN32
	while (read_ffmpeg_line(&proc, buf, sizeof(buf))) {
#else
	int ret;
	while ((ret = acl_vstream_gets_nonl(proc.stream, buf, sizeof(buf) - 1))
		  != ACL_VSTREAM_EOF) {
		buf[ret] = '\0';
#endif
		std::string line(buf);
		const long long current_ms = parse_progress_ms_line(line);
		if (current_ms >= 0 && duration_ms > 0) {
			const double percent = start_percent + static_cast<double>(current_ms)
			                 * progress_span / static_cast<double>(duration_ms);
			update_task_progress(task, percent, progress_msg);
		} else if (line == "progress=end") {
			update_task_progress(task, end_percent, end_msg);
		}

		if (is_task_cancel_requested(task)) {
#ifdef _WIN32
			if (proc.process != nullptr) {
				TerminateProcess(proc.process, 1);
			}
#else
			if (proc.stream && proc.stream->pid > 0) {
				kill(proc.stream->pid, SIGTERM);
			}
#endif
		}
	}

#ifdef _WIN32
	DWORD exit_code = 1;
	if (proc.process != nullptr) {
		WaitForSingleObject(proc.process, INFINITE);
		GetExitCodeProcess(proc.process, &exit_code);
	}
	if (proc.read_pipe != nullptr) {
		CloseHandle(proc.read_pipe);
	}
	if (proc.thread != nullptr) {
		CloseHandle(proc.thread);
	}
	if (proc.process != nullptr) {
		CloseHandle(proc.process);
	}
	return (int) exit_code;
#else
	const int code = acl_vstream_pclose(proc.stream);
	return code;
#endif
}

static ffmpeg_process_ptr start_ffmpeg_process_direct(ACL_ARGV* args) {
#ifdef _WIN32
	SECURITY_ATTRIBUTES sa;
	memset(&sa, 0, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE read_pipe = nullptr;
	HANDLE write_pipe = nullptr;
	if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
		acl_argv_free(args);
		return nullptr;
	}
	SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

	std::string command;
	for (int i = 0; args->argv[i] != nullptr; ++i) {
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
		return nullptr;
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
	acl_argv_free(args);
	if (!ok) {
		CloseHandle(read_pipe);
		return nullptr;
	}

	//ffmpeg_process_t* proc = new ffmpeg_process_t;
	auto proc = std::make_shared<ffmpeg_process_t>();
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
	if (stream == nullptr) {
		return nullptr;
	}

	auto proc = std::make_shared<ffmpeg_process_t>();
	proc->stream = stream;
	return proc;
#endif
}

ffmpeg_process_ptr start_ffmpeg_process(ACL_ARGV* args) {
	ffmpeg_process_ptr res;
	acl::gofiber_wait_thread([&res, args] {
		res = start_ffmpeg_process_direct(args);
	});
	return res;
}

} // namespace action

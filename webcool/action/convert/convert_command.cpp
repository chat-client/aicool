#include "stdafx.h"
#include "../action_util.h"
#include "convert_common.h"

namespace action {

std::string shell_quote(const std::string& s) {
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

static int run_command_capture_direct(const std::string& command,
	  std::string& output) {

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

int run_command_capture(const std::string& command, std::string& output) {
	int ret = 0;
	acl::gofiber_wait_thread([&ret, &command, &output] {
		ret = run_command_capture_direct(command, output);
	});
	return ret;
}

std::string trim_text(const std::string& s) {
	size_t b = 0;
	while (b < s.size() && (s[b] == ' ' || s[b] == '\t'
		  || s[b] == '\r' || s[b] == '\n')) {
		++b;
	}
	size_t e = s.size();
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t'
		  || s[e - 1] == '\r' || s[e - 1] == '\n')) {
		--e;
	}
	return s.substr(b, e - b);
}

long long parse_duration_ms_from_text(const std::string& text) {
	const char* p = strstr(text.c_str(), "Duration: ");
	if (p == nullptr) {
		return -1;
	}
	p += 10;
	int hh = 0, mm = 0;
	double ss = 0;
	if (sscanf(p, "%d:%d:%lf", &hh, &mm, &ss) != 3) {
		return -1;
	}
	return static_cast<long long>((hh * 3600.0 + mm * 60.0 + ss) * 1000.0);
}

long long probe_duration_ms(const std::string& ffmpeg,
	const std::string& input_file)
{
	const std::string cmd = shell_quote(ffmpeg) + " -hide_banner -i "
		+ shell_quote(input_file) + " 2>&1";
	std::string out;
	run_command_capture(cmd, out);
	return parse_duration_ms_from_text(out);
}

long long parse_progress_ms_line(const std::string& line) {
	if (line.compare(0, 12, "out_time_us=") == 0) {
		const long long us = safe_atoll(line.c_str() + 12, 0);
		return us / 1000;
	}
	if (line.compare(0, 12, "out_time_ms=") == 0) {
		long long value = safe_atoll(line.c_str() + 12, 0);
		if (value > 1000000) {
			return value / 1000;
		}
		return value;
	}
	if (line.compare(0, 9, "out_time=") == 0) {
		return parse_duration_ms_from_text(std::string("Duration: ")
				+ std::string(line.c_str() + 9));
	}
	return -1;
}

} // namespace action

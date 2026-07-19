#include "stdafx.h"
#include "convert_common.h"

namespace action {

namespace {

 ffmpeg_process_ptr start_local_stream_ffmpeg_in_thread(const std::string& ffmpeg,
	const std::string& local_path, long long start_position_ms,
	const std::string& progress_path)
{
	ACL_ARGV* args = acl_argv_alloc(32);
	acl_argv_add(args,
		ffmpeg.c_str(),
		"-hide_banner",
		"-loglevel", "error",
		"-nostdin",
		nullptr);
	if (start_position_ms > 0) {
		char ss_buf[64];
		snprintf(ss_buf, sizeof(ss_buf), "%.3f",
			static_cast<double>(start_position_ms) / 1000.0);
		acl_argv_add(args, "-ss", ss_buf, nullptr);
	}
	acl_argv_add(args,
		"-i", local_path.c_str(),
		"-map", "0:v:0",
		"-map", "0:a:0?",
		"-dn",
		"-c:v", "libx264",
		"-preset", "veryfast",
		"-crf", "23",
		"-pix_fmt", "yuv420p",
		"-c:a", "aac",
		"-ac", "2",
		"-b:a", "192k",
		"-progress", progress_path.c_str(),
		"-nostats",
		"-movflags", "frag_keyframe+empty_moov+default_base_moof",
		"-f", "mp4",
		"pipe:1",
		nullptr);
	return start_ffmpeg_process_in_thread(args);
}

#ifndef _WIN32
 int close_local_stream_ffmpeg(const ffmpeg_process_ptr& proc, bool kill_process)
{
	if (!proc || !proc->stream) {
		return -1;
	}
	if (kill_process && proc->stream->pid > 0) {
		kill(static_cast<pid_t>(proc->stream->pid), SIGTERM);
	}
	const int code = acl_vstream_pclose(proc->stream);
	proc->stream = nullptr;
	return code;
}
#endif

} // namespace

bool LocalDiskVideoStreamAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string local_path;
	std::string err;
	if (!normalize_local_video_path(req.getParameter("path"), local_path, err)) {
		return sendText(res, 400, err.c_str(), false);
	}

	struct stat st{};
	if (stat(local_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		return sendText(res, 404, "source video not found\n", false);
	}
	if (!is_local_convertible_video_name(local_path.c_str())) {
		return sendText(res, 400, "local video must be rmvb, rm, avi, mov, wmv, mpg, or mpeg\n", false);
	}

	const std::string parent = local_parent_path(local_path);
	std::string lock_err;
	int lock_status = 500;
	if (!ensure_local_video_transcode_lock_policy(upload_dir, local_path, lock_err, lock_status)) {
		return sendText(res, lock_status, (lock_err + "\n").c_str(), false);
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
	register_stream_sidecar(tmp_path);
	register_stream_sidecar(progress_path);

	std::shared_ptr<transcode_task_t> task;
	const char* task_id_param = req.getParameter("stream_task_id");
	if (task_id_param != nullptr && *task_id_param != '\0') {
		task = std::make_shared<transcode_task_t>();
		task->id = task_id_param;
		task->scope = upload_dir;
		task->file_name = std::string("local-stream:") + local_path;
		task->output_name = output_path;
		task->message = "边转边看准备中";
		task->local = true;
		{
			std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
			g_transcode_tasks[task->id] = task;
		}
	}

	long long duration_ms = 0;
	long long remaining_duration_ms = 0;
	acl::gofiber([&] {
		duration_ms = probe_duration_ms_in_thread(ffmpeg, local_path);
		remaining_duration_ms = duration_ms > start_position_ms
			? duration_ms - start_position_ms : duration_ms;
	});

	ffmpeg_process_ptr proc = start_local_stream_ffmpeg_in_thread(
		ffmpeg, local_path, start_position_ms, progress_path);
#ifndef _WIN32
	if (!proc || !proc->stream) {
#else
	if (!proc || !proc->read_pipe) {
#endif
		cleanup_current_stream_sidecars(tmp_path, progress_path, false);
		if (task) {
			finish_task(task, false, "转码启动失败", "failed to start ffmpeg", -1);
		}
		return sendText(res, 500, "failed to start ffmpeg\n", false);
	}

	FILE* out = fopen(tmp_path.c_str(), append_existing_tmp ? "ab" : "wb");
	if (out == nullptr) {
#ifndef _WIN32
		close_local_stream_ffmpeg(proc, true);
#else
		if (proc->process != nullptr) {
			TerminateProcess(proc->process, 1);
		}
#endif
		cleanup_current_stream_sidecars(tmp_path, progress_path, false);
		if (task) {
			finish_task(task, false, "转码失败", "failed to create output file", -1);
		}
		return sendText(res, 500, "failed to create output file\n", false);
	}

	if (task) {
#ifndef _WIN32
		set_task_process_pid(task, proc->stream
			? static_cast<long>(proc->stream->pid) : -1);
#else
		set_task_process_pid(task, proc ? static_cast<long>(proc->pid) : -1);
#endif
		update_task_progress(task, 0.1, "边转边看中");
	}

	res.setStatus(200)
		.setKeepAlive(false)
		.setChunkedTransferEncoding(true)
		.setContentType("video/mp4")
		.setHeader("Content-Disposition", "inline")
		.setHeader("Cache-Control", "no-store")
		.setHeader("Accept-Ranges", "none");

	bool ok = true;
	bool client_ok = true;
	std::vector<char> buf(8192);

	while (true) {
#ifndef _WIN32
		const int n = acl_vstream_read(proc->stream, buf.data(), buf.size());
		if (n == ACL_VSTREAM_EOF) {
			break;
		}
		if (n <= 0) {
			if (n < 0) {
				ok = false;
			}
			break;
		}
#else
		DWORD n = 0;
		if (!ReadFile(proc->read_pipe, buf.data(), static_cast<DWORD>(buf.size()),
			&n, nullptr) || n == 0) {
			if (n == 0 && GetLastError() != ERROR_SUCCESS) {
				ok = false;
			}
			break;
		}
#endif
		if (fwrite(buf.data(), 1, static_cast<size_t>(n), out) != static_cast<size_t>(n)) {
			ok = false;
			break;
		}
		if (!res.write(buf.data(), static_cast<size_t>(n))) {
			client_ok = false;
			ok = false;
			break;
		}
		if (task) {
			update_stream_task_progress_from_file(task, progress_path,
				remaining_duration_ms);
		}
	}

#ifndef _WIN32
	const int code = close_local_stream_ffmpeg(proc, !client_ok);
#else
	DWORD exit_code = 1;
	if (!client_ok && proc->process != nullptr) {
		TerminateProcess(proc->process, 1);
	}
	if (proc->process != nullptr) {
		WaitForSingleObject(proc->process, INFINITE);
		GetExitCodeProcess(proc->process, &exit_code);
	}
	if (proc->read_pipe != nullptr) {
		CloseHandle(proc->read_pipe);
	}
	if (proc->thread != nullptr) {
		CloseHandle(proc->thread);
	}
	if (proc->process != nullptr) {
		CloseHandle(proc->process);
	}
	const int code = static_cast<int>(exit_code);
#endif

	if (fclose(out) != 0) {
		ok = false;
	}
	const bool preserve_partial_files = !client_ok;
	if (code == 0 && ok && file_size_of(tmp_path.c_str()) > 0) {
		std::string remux_err;
		if (remux_mp4_faststart(ffmpeg, tmp_path, output_path, remux_err)) {
			unlink(tmp_path.c_str());
		} else if (rename(tmp_path.c_str(), output_path.c_str()) != 0) {
			unlink(tmp_path.c_str());
			ok = false;
		}
	} else {
		if (!preserve_partial_files) {
			unlink(tmp_path.c_str());
		}
		ok = false;
	}
	cleanup_current_stream_sidecars(tmp_path, progress_path, preserve_partial_files);
	cleanup_local_stream_sidecars(parent);
	if (ok) {
		remove_local_stream_position(local_path);
	}
	if (task) {
		finish_task(task, ok, ok ? "边转边看完成" : "边转边看失败",
			ok ? "" : (client_ok ? "stream transcode failed" : "client disconnected"),
			ok ? file_size_of(output_path.c_str()) : -1);
	}
	if (client_ok) {
		return res.write(nullptr, 0);
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

	if (req.getParameter("position_ms") != nullptr) {
		const char* ptr = req.getParameter("position_ms");
		long long position_ms = safe_atoll(ptr ? ptr : "0", -1);
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

} // namespace action

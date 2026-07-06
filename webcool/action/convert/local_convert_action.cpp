#include "stdafx.h"
#include "convert_common.h"

namespace action {

bool LocalDiskVideoConvertAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string local_path;
	std::string err;
	if (!normalize_local_video_path(req.getParameter("path"), local_path, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	struct stat st{};
	if (stat(local_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		json_error(res, 404, "source video not found", req.isKeepAlive());
		return true;
	}
	if (!is_local_convertible_video_name(local_path.c_str())) {
		json_error(res, 400, "local video must be rmvb, rm, avi, mov, wmv, mpg, or mpeg", req.isKeepAlive());
		return true;
	}

	const std::string parent = local_parent_path(local_path);
	std::string lock_err;
	int lock_status = 500;
	if (!ensure_local_video_transcode_lock_policy(upload_dir, local_path, lock_err, lock_status)) {
		json_error(res, lock_status, lock_err.c_str(), req.isKeepAlive());
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
		std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
		auto it = g_running_task_by_file.find(scoped_key);
		if (it != g_running_task_by_file.end()) {
			auto task_it = g_transcode_tasks.find(it->second);
			if (task_it != g_transcode_tasks.end() && !task_it->second->done) {
				acl::json json;
				acl::json_node& root = json.create_node();
				root.add_bool("ok", true);
				root.add_bool("started", false);
				root.add_bool("running", true);
				root.add_bool("local", true);
				root.add_text("task_id", task_it->second->id.c_str());
				root.add_text("name", task_it->second->output_name.c_str());
				root.add_number("progress", static_cast<long long>(task_it->second->progress));
				root.add_bool("cancel_requested", task_it->second->cancel_requested);
				root.add_text("message", task_it->second->message.c_str());
				return sendJson(res, 200, root, req.isKeepAlive());
			}
		}
	}

	const std::string output_path = make_unique_local_transcoded_path(local_path);
	acl::string tmp_path;
	tmp_path.format("%s/.transcoding_tmp.%u.%lu.mp4", parent.c_str(),
		static_cast<unsigned>(getpid()), static_cast<unsigned long>(g_transcode_seq.load()));

	std::shared_ptr<transcode_task_t> task = std::make_shared<transcode_task_t>();
	task->id = make_task_id();
	task->scope = upload_dir;
	task->file_name = task_key;
	task->output_name = output_path;
	task->message = "等待后台转码";
	task->local = true;

	{
		std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
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
	unlink(meta_path.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

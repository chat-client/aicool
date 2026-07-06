#include "stdafx.h"
#include "convert_common.h"

namespace action {

namespace {

struct remote_convert_input_t {
	std::string file_path;
	std::string in_path;
	std::string ffmpeg;
};

struct remote_convert_output_t {
	std::string output_name;
	std::string secondary_output_name;
	std::string out_path;
	std::string secondary_out_path;
	std::string tmp_path;
};

bool prepare_remote_convert_input(const request_t& req, response_t& res,
	const std::string& upload_dir, remote_convert_input_t& input)
{
	const char* file = req.getParameter("file");
	if (file == nullptr || *file == '\0') {
		json_error(res, 400, "missing query parameter: file", req.isKeepAlive());
		return false;
	}

	std::string path_err;
	if (!normalize_relative_path(file, input.file_path, path_err, false)) {
		json_error(res, 400, path_err.c_str(), req.isKeepAlive());
		return false;
	}
	if (!resolve_upload_regular_file_path(upload_dir, input.file_path, input.file_path)) {
		json_error(res, 404, "source video not found", req.isKeepAlive());
		return false;
	}

	const std::string basename = base_name_from_relative_path(input.file_path);
	if (!is_video_name(basename.c_str())) {
		json_error(res, 400, "file is not a supported video", req.isKeepAlive());
		return false;
	}

	input.in_path = join_upload_path(upload_dir, input.file_path);
	if (file_size_of(input.in_path.c_str()) <= 0) {
		json_error(res, 404, "source video not found", req.isKeepAlive());
		return false;
	}

	std::string lock_err;
	int lock_status = 500;
	if (!ensure_remote_video_transcode_lock_policy(upload_dir, input.file_path,
		lock_err, lock_status))
	{
		json_error(res, lock_status, lock_err.c_str(), req.isKeepAlive());
		return false;
	}

	input.ffmpeg = choose_ffmpeg_path();
	if (input.ffmpeg.empty()) {
		json_error(res, 500, "ffmpeg not found in tools directory", req.isKeepAlive());
		return false;
	}
	return true;
}

bool send_remote_playable_response(response_t& res, bool keep_alive,
	const std::string& file_path)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_bool("started", false);
	root.add_bool("completed", true);
	root.add_bool("playable", true);
	root.add_text("name", file_path.c_str());
	root.add_text("message", "video already playable");
	return sendJson(res, 200, root, keep_alive);
}

bool try_send_remote_running_task_response(response_t& res, bool keep_alive,
	const std::string& upload_dir, const std::string& file_path)
{
	const std::string task_key = scoped_task_key(upload_dir, file_path);
	std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
	const auto it = g_running_task_by_file.find(task_key);
	if (it == g_running_task_by_file.end()) {
		return false;
	}
	const auto task_it = g_transcode_tasks.find(it->second);
	if (task_it == g_transcode_tasks.end() || task_it->second->done) {
		return false;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_bool("started", false);
	root.add_bool("running", true);
	root.add_text("task_id", task_it->second->id.c_str());
	root.add_text("name", task_it->second->output_name.c_str());
	root.add_number("progress", static_cast<long long>(task_it->second->progress));
	root.add_bool("cancel_requested", task_it->second->cancel_requested);
	root.add_text("message", task_it->second->message.c_str());
	sendJson(res, 200, root, keep_alive);
	return true;
}

transcode_strategy_t resolve_remote_transcode_strategy(const request_t& req,
	const std::string& ffmpeg, const std::string& in_path)
{
	transcode_strategy_t strategy;
	probe_transcode_strategy(ffmpeg, in_path, strategy, true);
	const char* requested_mode = req.getParameter("mode");
	if (requested_mode != nullptr && strcmp(requested_mode, "audio_only") == 0) {
		strategy.mode = transcode_strategy_t::audio_only;
	}
	if (requested_mode != nullptr && strcmp(requested_mode, "audio_split") == 0) {
		strategy.mode = transcode_strategy_t::audio_split;
	}
	if (requested_mode != nullptr && strcmp(requested_mode, "full_mp4") == 0) {
		strategy.mode = transcode_strategy_t::full_mp4;
	}
	return strategy;
}

remote_convert_output_t plan_remote_convert_output(const std::string& upload_dir,
	const std::string& file_path, const transcode_strategy_t& strategy)
{
	remote_convert_output_t output;
	if (strategy.mode == transcode_strategy_t::audio_only) {
		output.output_name = make_unique_audio_only_name(upload_dir, file_path);
	} else if (strategy.mode == transcode_strategy_t::audio_split) {
		make_unique_split_output_names(upload_dir, file_path, output.output_name,
			output.secondary_output_name);
	} else {
		output.output_name = make_unique_transcoded_name(upload_dir, file_path);
	}

	output.out_path = join_upload_path(upload_dir, output.output_name);
	const std::string out_dir = local_parent_path(output.out_path);
	output.secondary_out_path = output.secondary_output_name.empty()
		? std::string() : join_upload_path(upload_dir, output.secondary_output_name);

	acl::string tmp_path;
	if (strategy.mode == transcode_strategy_t::audio_only) {
		tmp_path.format("%s/.transcoding_tmp.%u.%lu.m4a", out_dir.c_str(),
			static_cast<unsigned>(getpid()),
			static_cast<unsigned long>(g_transcode_seq.load()));
	} else {
		tmp_path.format("%s/.transcoding_tmp.%u.%lu.mp4", out_dir.c_str(),
			static_cast<unsigned>(getpid()),
			static_cast<unsigned long>(g_transcode_seq.load()));
	}
	output.tmp_path = tmp_path.c_str();
	return output;
}

std::shared_ptr<transcode_task_t> register_remote_transcode_task(
	const std::string& upload_dir, const std::string& file_path,
	const remote_convert_output_t& output)
{
	auto task = std::make_shared<transcode_task_t>();
	task->id = make_task_id();
	task->scope = upload_dir;
	task->file_name = file_path;
	task->output_name = output.output_name;
	task->secondary_output_name = output.secondary_output_name;
	task->message = "等待后台转码";

	std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
	g_transcode_tasks[task->id] = task;
	g_running_task_by_file[scoped_task_key(task->scope, task->file_name)] = task->id;
	return task;
}

void launch_remote_transcode_task(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& in_path,
	const remote_convert_output_t& output, const transcode_strategy_t& strategy)
{
	const std::string input_path(in_path);
	const std::string temp_path(output.tmp_path);
	const std::string output_path(output.out_path);
	const std::string secondary_output_path(output.secondary_out_path);

	acl::gofiber_wait_thread([task, ffmpeg, input_path, temp_path, output_path,
			secondary_output_path, strategy] {
		run_transcode_task(task, ffmpeg, input_path, temp_path, output_path,
			secondary_output_path, strategy);
	});
}

const char* remote_transcode_started_message(const transcode_strategy_t& strategy)
{
	if (strategy.mode == transcode_strategy_t::audio_only) {
		return "audio only task started";
	}
	if (strategy.mode == transcode_strategy_t::audio_split) {
		return "audio split task started";
	}
	return "transcode task started";
}

bool send_remote_transcode_started_response(response_t& res, bool keep_alive,
	const std::shared_ptr<transcode_task_t>& task,
	const remote_convert_output_t& output, const transcode_strategy_t& strategy)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_bool("started", true);
	root.add_bool("running", true);
	root.add_bool("playable", false);
	root.add_text("task_id", task->id.c_str());
	root.add_text("name", output.output_name.c_str());
	if (!output.secondary_output_name.empty()) {
		root.add_text("secondary_name", output.secondary_output_name.c_str());
	}
	root.add_bool("cancel_requested", false);
	root.add_number("progress", 0);
	root.add_text("message", remote_transcode_started_message(strategy));
	return sendJson(res, 200, root, keep_alive);
}

} // namespace

bool VideoConvertAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	remote_convert_input_t input;
	if (!prepare_remote_convert_input(req, res, upload_dir, input)) {
		return true;
	}

	std::string probe_reason;
	if (browser_can_play_video_by_probe(input.ffmpeg, input.in_path, probe_reason)) {
		return send_remote_playable_response(res, req.isKeepAlive(), input.file_path);
	}

	if (try_send_remote_running_task_response(res, req.isKeepAlive(),
		upload_dir, input.file_path))
	{
		return true;
	}

	const transcode_strategy_t strategy = resolve_remote_transcode_strategy(
		req, input.ffmpeg, input.in_path);
	const remote_convert_output_t output = plan_remote_convert_output(
		upload_dir, input.file_path, strategy);
	const std::shared_ptr<transcode_task_t> task = register_remote_transcode_task(
		upload_dir, input.file_path, output);
	launch_remote_transcode_task(task, input.ffmpeg, input.in_path, output, strategy);
	return send_remote_transcode_started_response(res, req.isKeepAlive(),
		task, output, strategy);
}

} // namespace action

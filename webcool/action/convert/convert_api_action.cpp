#include "stdafx.h"
#include "convert_common.h"
#include "../file/file_common.h"

namespace action {

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
	root.add_number("progress", static_cast<long long>(snapshot.progress));
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
	for (auto & task : tasks) {
		acl::json_node& node = arr.add_child(false, true);
		node.add_text("task_id", task.id.c_str());
		node.add_text("file", task.file_name.c_str());
		node.add_text("name", task.output_name.c_str());
		node.add_bool("done", task.done);
		node.add_bool("success", task.success);
		node.add_bool("cancel_requested", task.cancel_requested);
		node.add_bool("local", task.local);
		node.add_number("progress", static_cast<long long>(task.progress));
		node.add_text("message", task.message.c_str());
		if (!task.secondary_output_name.empty()) {
			node.add_text("secondary_name", task.secondary_output_name.c_str());
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

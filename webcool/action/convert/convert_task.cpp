#include "stdafx.h"
#include "convert_common.h"

namespace action {

webcool::mutex g_transcode_mutex;
std::map<std::string, std::shared_ptr<transcode_task_t> > g_transcode_tasks;
std::map<std::string, std::string> g_running_task_by_file;
std::set<std::string> g_active_stream_sidecars;
std::atomic<unsigned long> g_transcode_seq(1);
std::string make_task_id() {
	char buf[64];
	snprintf(buf, sizeof(buf), "tx-%u-%lu", static_cast<unsigned>(getpid()),
		static_cast<unsigned long>(g_transcode_seq.fetch_add(1)));
	return {buf};
}

void update_task_progress(const std::shared_ptr<transcode_task_t>& task,
	double percent, const char* msg)
{
	std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
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

void set_task_process_pid(const std::shared_ptr<transcode_task_t>& task,
	long process_pid)
{
	std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
	task->process_pid = process_pid;
}

bool is_task_cancel_requested(const std::shared_ptr<transcode_task_t>& task) {
	std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
	return task->cancel_requested;
}

std::string scoped_task_key(const std::string& scope,
	const std::string& file_name)
{
	return scope + "\n" + file_name;
}

void finish_task(const std::shared_ptr<transcode_task_t>& task,
	bool success, const char* msg, const char* err, long long size)
{
	std::string sync_dir;
	{
		std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
		task->done = true;
		task->success = success;
		task->progress = success ? 100.0 : task->progress;
		task->size = size;
		task->process_pid = -1;
		task->message = msg ? msg : "";
		task->error = err ? err : "";
		if (success && !task->local) {
			sync_dir = task->scope;
		}
		const auto it = g_running_task_by_file.find(
			scoped_task_key(task->scope, task->file_name));
		if (it != g_running_task_by_file.end() && it->second == task->id) {
			g_running_task_by_file.erase(it);
		}
	}
	if (!sync_dir.empty()) {
		std::string sync_err;
		std::vector<std::string> sync_paths;
		std::vector<std::string> delete_paths;
		if (!task->output_name.empty()) {
			sync_paths.push_back(task->output_name);
		}
		if (!task->secondary_output_name.empty()) {
			sync_paths.push_back(task->secondary_output_name);
		}
		(void) storage_backup_sync_paths(sync_dir, sync_paths, delete_paths, sync_err);
	}
}
bool snapshot_task_by_id(const char* task_id, const std::string& scope,
	transcode_task_snapshot_t& snapshot)
{
	if (task_id == nullptr || *task_id == '\0') {
		return false;
	}
	std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
	const auto it = g_transcode_tasks.find(task_id);
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

void snapshot_running_tasks(const std::string& scope,
	std::vector<transcode_task_snapshot_t>& out) {
	out.clear();
	std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
	for (const auto & task : g_transcode_tasks) {
		if (!task.second || task.second->done
			  || task.second->scope != scope) {
			continue;
		}
		transcode_task_snapshot_t snapshot;
		snapshot.id = task.second->id;
		snapshot.scope = task.second->scope;
		snapshot.file_name = task.second->file_name;
		snapshot.output_name = task.second->output_name;
		snapshot.secondary_output_name = task.second->secondary_output_name;
		snapshot.error = task.second->error;
		snapshot.message = task.second->message;
		snapshot.progress = task.second->progress;
		snapshot.process_pid = task.second->process_pid;
		snapshot.done = task.second->done;
		snapshot.success = task.second->success;
		snapshot.cancel_requested = task.second->cancel_requested;
		snapshot.local = task.second->local;
		snapshot.size = task.second->size;
		out.push_back(snapshot);
	}
}

bool request_cancel_task(const char* task_id, const std::string& scope,
	transcode_task_snapshot_t& snapshot, bool& signal_sent)
{
	signal_sent = false;
	if (task_id == nullptr || *task_id == '\0') {
		return false;
	}

	long pid = -1;
	{
		std::lock_guard<webcool::mutex> guard(g_transcode_mutex);
		const auto it = g_transcode_tasks.find(task_id);
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
		signal_sent = kill(static_cast<pid_t>(pid), SIGTERM) == 0;
	}
#endif
	return true;
}

} // namespace action

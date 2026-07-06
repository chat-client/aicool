#include "stdafx.h"
#include "admin_internal.h"

#ifdef _WIN32
#include "../../platform_compat.h"
#else
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>
#endif
#include <cerrno>

namespace action {
namespace admin_internal {

webcool::mutex g_storage_migrate_mutex;
storage_migrate_task_t g_storage_migrate_task;
unsigned long long g_storage_migrate_seq = 0;

std::string make_task_id()
{
	std::lock_guard<webcool::mutex> guard(g_storage_migrate_mutex);
	++g_storage_migrate_seq;
	return std::string("storage-migrate-")
		+ std::to_string(static_cast<long long>(time(nullptr)))
		+ "-" + std::to_string(static_cast<long long>(getpid()))
		+ "-" + std::to_string(static_cast<long long>(g_storage_migrate_seq));
}

void update_task(const storage_migrate_task_t& task)
{
	std::lock_guard<webcool::mutex> guard(g_storage_migrate_mutex);
	storage_migrate_task_t merged = task;
	if (g_storage_migrate_task.id == task.id
		&& task.state != "done"
		&& task.state != "failed"
		&& task.state != "cancelled")
	{
		merged.pause_requested = task.pause_requested
			|| g_storage_migrate_task.pause_requested;
		merged.cancel_requested = task.cancel_requested
			|| g_storage_migrate_task.cancel_requested;
		if (merged.conflict_resolution.empty()) {
			merged.conflict_resolution = g_storage_migrate_task.conflict_resolution;
		}
		if (merged.conflict_default.empty()) {
			merged.conflict_default = g_storage_migrate_task.conflict_default;
		}
	}
	g_storage_migrate_task = merged;
}

storage_migrate_task_t current_storage_task_snapshot()
{
	std::lock_guard<webcool::mutex> guard(g_storage_migrate_mutex);
	return g_storage_migrate_task;
}

bool storage_task_wait_if_paused_or_cancelled(storage_migrate_task_t& task)
{
	while (true) {
		storage_migrate_task_t snapshot = current_storage_task_snapshot();
		if (snapshot.id != task.id) {
			return false;
		}
		task.pause_requested = snapshot.pause_requested;
		task.cancel_requested = snapshot.cancel_requested;
		if (task.cancel_requested) {
			task.state = "cancelled";
			task.message = "迁移已取消";
			update_task(task);
			return false;
		}
		if (!task.pause_requested) {
			if (task.state == "paused") {
				task.state = "running";
				task.message = "继续迁移";
				update_task(task);
			}
			return true;
		}
		task.state = "paused";
		task.message = "迁移已暂停";
		update_task(task);
		acl_doze(200);
	}
}

bool copy_file_with_progress(const storage_move_item_t& item,
	storage_migrate_task_t& task, std::string& err)
{
	FILE* in = fopen(item.source.c_str(), "rb");
	if (in == nullptr) {
		err = strerror(errno);
		return false;
	}
	FILE* out = fopen(item.target.c_str(), "wb");
	if (out == nullptr) {
		err = strerror(errno);
		fclose(in);
		return false;
	}
	char buf[1024 * 64];
	bool ok = true;
	while (true) {
		if (!storage_task_wait_if_paused_or_cancelled(task)) {
			err = "cancelled";
			ok = false;
			break;
		}
		const size_t n = fread(buf, 1, sizeof(buf), in);
		if (n > 0 && fwrite(buf, 1, n, out) != n) {
			err = strerror(errno);
			ok = false;
			break;
		}
		if (n > 0) {
			task.moved_bytes += static_cast<long long>(n);
			update_task(task);
		}
		if (n < sizeof(buf)) {
			if (ferror(in)) {
				err = strerror(errno);
				ok = false;
			}
			break;
		}
	}
	if (fclose(out) != 0 && ok) {
		err = strerror(errno);
		ok = false;
	}
	fclose(in);
	if (!ok) {
		unlink(item.target.c_str());
	}
	return ok;
}

bool backup_project_db_files(const std::string& storage_dir,
	bool move_files, std::string& err)
{
	static const char* db_files[] = {
		".video_resume.db",
		".tag_catalog.db",
		".recycle_bin.db",
		".folder_catalog.db"
	};
	const std::string backup_dir = join_storage_path(storage_dir, ".backup");
	const std::string date_suffix = storage_backup_date_suffix();
	bool backup_ready = false;
	for (auto & db_file : db_files) {
		const std::string source = join_storage_path(storage_dir, db_file);
		struct stat st{};
		if (lstat(source.c_str(), &st) != 0) {
			if (errno == ENOENT) {
				continue;
			}
			err = strerror(errno);
			return false;
		}
		if (!S_ISREG(st.st_mode)) {
			continue;
		}
		if (!backup_ready) {
			if (!make_dir_recursive(backup_dir.c_str())) {
				err = "cannot create backup directory";
				return false;
			}
			backup_ready = true;
		}
		const std::string backup_name = std::string(db_file) + "." + date_suffix;
		std::string dest = join_storage_path(backup_dir, backup_name.c_str());
		struct stat dest_st{};
		if (lstat(dest.c_str(), &dest_st) == 0) {
			bool found_free_name = false;
			for (int seq = 1; seq < 10000; ++seq) {
				const std::string candidate_name = backup_name + "." + std::to_string(seq);
				dest = join_storage_path(backup_dir, candidate_name.c_str());
				if (lstat(dest.c_str(), &dest_st) != 0 && errno == ENOENT) {
					found_free_name = true;
					break;
				}
			}
			if (!found_free_name) {
				err = "cannot allocate backup database file name";
				return false;
			}
		} else if (errno != ENOENT) {
			err = strerror(errno);
			return false;
		}
		if (move_files) {
			if (rename(source.c_str(), dest.c_str()) != 0) {
				err = strerror(errno);
				return false;
			}
		} else if (!copy_storage_file_plain(source, dest, err)) {
			return false;
		}
	}
	return true;
}

std::string wait_storage_conflict_resolution(storage_migrate_task_t& task,
	const storage_move_item_t& item)
{
	if (!task.conflict_default.empty()) {
		return task.conflict_default;
	}
	storage_migrate_task_t initial_snapshot = current_storage_task_snapshot();
	if (initial_snapshot.id == task.id && !initial_snapshot.conflict_default.empty()) {
		task.conflict_default = initial_snapshot.conflict_default;
		return task.conflict_default;
	}
	task.state = "conflict";
	task.message = "发现同名文件";
	task.conflict_source = item.source;
	task.conflict_target = item.target;
	task.conflict_name = storage_base_name(item.source);
	task.conflict_resolution.clear();
	update_task(task);
	while (true) {
		acl_doze(200);
		storage_migrate_task_t snapshot = current_storage_task_snapshot();
		if (snapshot.id != task.id) {
			return "cancel";
		}
		if (snapshot.cancel_requested) {
			task.cancel_requested = true;
			return "cancel";
		}
		if (snapshot.pause_requested) {
			task.pause_requested = true;
			task.state = "paused";
			task.message = "迁移已暂停";
			update_task(task);
			continue;
		}
		if (task.state == "paused") {
			task.pause_requested = false;
			task.state = "conflict";
			task.message = "发现同名文件";
			update_task(task);
		}
		if (!snapshot.conflict_resolution.empty()) {
			std::string choice = snapshot.conflict_resolution;
			if (choice == "remember-overwrite") {
				task.conflict_default = "overwrite";
			} else if (choice == "remember-skip") {
				task.conflict_default = "skip";
			}
			task.conflict_resolution.clear();
			task.conflict_source.clear();
			task.conflict_target.clear();
			task.conflict_name.clear();
			task.state = "running";
			task.message = "继续迁移";
			update_task(task);
			return task.conflict_default.empty()
				? std::string(choice) : task.conflict_default;
		}
	}
}

void run_storage_migration(storage_migrate_task_t task,
	const std::vector<storage_move_item_t>& items)
{
	task.state = "running";
	task.message = "正在准备迁移";
	update_task(task);

	std::string err;
	for (auto & item : items) {
		if (!storage_task_wait_if_paused_or_cancelled(task)) {
			return;
		}
		if (item.directory) {
			if (!make_dir_recursive(item.target.c_str())) {
				task.state = "failed";
				task.error = "cannot create target directory";
				update_task(task);
				return;
			}
			continue;
		}
		struct stat target_st{};
		if (lstat(item.target.c_str(), &target_st) == 0) {
			const std::string choice = wait_storage_conflict_resolution(task, item);
			if (choice == "cancel") {
				task.state = "cancelled";
				task.message = "迁移已取消";
				update_task(task);
				return;
			}
			if (choice == "skip") {
				task.moved_bytes += item.size;
				task.moved_files += 1;
				task.message = std::string("正在处理同名文件(跳过)：") + item.source;
				update_task(task);
				continue;
			}
			if (choice != "overwrite") {
				task.state = "failed";
				task.error = "invalid conflict resolution";
				update_task(task);
				return;
			}
			task.message = std::string("正在处理同名文件(覆盖)：") + item.source;
			update_task(task);
		} else if (errno != ENOENT) {
			task.state = "failed";
			task.error = strerror(errno);
			update_task(task);
			return;
		}
		if (task.message.compare(0, strlen("正在处理同名文件(覆盖)："), "正在处理同名文件(覆盖)：") != 0) {
			task.message = std::string("正在拷贝：") + item.source;
			update_task(task);
		}
		if (!copy_file_with_progress(item, task, err)) {
			task.state = "failed";
			task.error = err;
			update_task(task);
			return;
		}
		task.moved_files += 1;
		update_task(task);
	}

	std::string init_err;
	if (!init_storage_databases(task.target_dir, init_err)) {
		task.state = "failed";
		task.error = init_err;
		update_task(task);
		return;
	}
	runtime_upload_dir_set(task.target_dir);
	std::string sync_err;
	(void) storage_backup_sync_now(task.target_dir, sync_err);
	task.state = "done";
	task.message = "迁移完成";
	task.moved_bytes = task.total_bytes;
	update_task(task);
}

} // namespace admin_internal

using namespace admin_internal;

bool AdminStorageMigrateAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string source_dir, target_dir, err;
	const char* migrate_param = req.getParameter("migrate");
	const bool migrate_files = !(migrate_param != nullptr
		&& (strcmp(migrate_param, "0") == 0
			|| strcasecmp(migrate_param, "false") == 0
			|| strcasecmp(migrate_param, "no") == 0));
	if (!ensure_storage_target_path(req.getParameter("path") ? req.getParameter("path") : "",
		target_dir, err))
	{
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!migrate_files) {
		if (!init_storage_databases(target_dir, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		runtime_upload_dir_set(target_dir);
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_bool("migrated", false);
		root.add_text("path", target_dir.c_str());
		return sendJson(res, 200, root, req.isKeepAlive());
	}
	if (!canonical_existing_path(upload_dir, source_dir, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (source_dir == target_dir) {
		json_error(res, 409, "target path is current storage path", req.isKeepAlive());
		return true;
	}
	if (is_same_or_child_path(source_dir, target_dir)) {
		json_error(res, 409, "target path cannot be inside current storage path", req.isKeepAlive());
		return true;
	}
	{
		std::lock_guard<webcool::mutex> guard(g_settings_mutex);
		webcool_settings_t settings;
		if (!load_settings_unlocked(upload_dir, settings, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		for (auto & backup_path : settings.backup_paths) {
			std::string backup_dir;
			if (canonical_existing_path(backup_path.path, backup_dir, err)
				&& target_dir == backup_dir)
			{
				json_error(res, 409,
					"target path is current backup path; use swap instead",
					req.isKeepAlive());
				return true;
			}
		}
	}
	{
		std::lock_guard<webcool::mutex> guard(g_storage_migrate_mutex);
		if (g_storage_migrate_task.state == "queued"
			|| g_storage_migrate_task.state == "running")
		{
			json_error(res, 409, "storage migration is already running", req.isKeepAlive());
			return true;
		}
	}

	std::vector<storage_move_item_t> items;
	int status = 200;
	bool ok = true;
	acl::gofiber_wait_thread([&] {
		if (!backup_project_db_files(source_dir, false, err)) {
			status = 500;
			ok = false;
			return;
		}
		if (!backup_project_db_files(target_dir, true, err)) {
			status = 500;
			ok = false;
			return;
		}
		if (!collect_move_items(source_dir, target_dir, items, err)) {
			status = 500;
			ok = false;
			return;
		}
	});
	if (!ok) {
		json_error(res, status, err.c_str(), req.isKeepAlive());
		return true;
	}

	storage_migrate_task_t task;
	task.id = make_task_id();
	task.state = "queued";
	task.message = "等待移动";
	task.source_dir = source_dir;
	task.target_dir = target_dir;
	task.cleanup_done = false;
	task.pause_requested = false;
	task.cancel_requested = false;
	task.total_bytes = 0;
	task.moved_bytes = 0;
	task.total_files = 0;
	task.moved_files = 0;
	for (auto & item : items) {
		if (!item.directory) {
			task.total_files += 1;
			task.total_bytes += item.size;
		}
	}
	update_task(task);
	go[task, items] {
		acl::gofiber_wait_thread([task, items] {
			run_storage_migration(task, items);
		});
	};

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", task.id.c_str());
	root.add_number("total_files", task.total_files);
	root.add_number("total_bytes", task.total_bytes);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AdminStorageMigrateProgressAction::run(request_t& req, response_t& res)
{
	storage_migrate_task_t task;
	{
		std::lock_guard<webcool::mutex> guard(g_storage_migrate_mutex);
		task = g_storage_migrate_task;
	}
	const char* task_id = req.getParameter("task_id");
	if (task_id != nullptr && *task_id != '\0' && task.id != task_id) {
		json_error(res, 404, "task not found", req.isKeepAlive());
		return true;
	}
	const double progress = task.total_bytes > 0
		? static_cast<double>(task.moved_bytes) * 100.0 / static_cast<double>(task.total_bytes)
		: task.state == "done" ? 100.0 : 0.0;
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", task.id.c_str());
	root.add_text("state", task.state.c_str());
	root.add_text("message", task.message.c_str());
	root.add_text("error", task.error.c_str());
	root.add_text("source_dir", task.source_dir.c_str());
	root.add_text("target_dir", task.target_dir.c_str());
	root.add_text("conflict_source", task.conflict_source.c_str());
	root.add_text("conflict_target", task.conflict_target.c_str());
	root.add_text("conflict_name", task.conflict_name.c_str());
	root.add_bool("cleanup_done", task.cleanup_done);
	root.add_bool("pause_requested", task.pause_requested);
	root.add_bool("cancel_requested", task.cancel_requested);
	root.add_number("progress", static_cast<long long>(progress));
	root.add_number("total_bytes", task.total_bytes);
	root.add_number("moved_bytes", task.moved_bytes);
	root.add_number("total_files", task.total_files);
	root.add_number("moved_files", task.moved_files);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AdminStorageMigrateResolveAction::run(request_t& req, response_t& res)
{
	const char* task_id = req.getParameter("task_id");
	const char* choice_text = req.getParameter("choice");
	std::string choice = choice_text ? choice_text : "";
	if (choice != "overwrite" && choice != "skip" && choice != "cancel"
		&& choice != "remember-overwrite" && choice != "remember-skip")
	{
		json_error(res, 400, "invalid conflict choice", req.isKeepAlive());
		return true;
	}
	{
		std::lock_guard<webcool::mutex> guard(g_storage_migrate_mutex);
		if (task_id != nullptr && *task_id != '\0' && g_storage_migrate_task.id != task_id) {
			json_error(res, 404, "task not found", req.isKeepAlive());
			return true;
		}
		if (g_storage_migrate_task.state != "conflict") {
			json_error(res, 409, "migration task is not waiting for conflict resolution", req.isKeepAlive());
			return true;
		}
		g_storage_migrate_task.conflict_resolution = choice;
		if (choice == "remember-overwrite") {
			g_storage_migrate_task.conflict_default = "overwrite";
		} else if (choice == "remember-skip") {
			g_storage_migrate_task.conflict_default = "skip";
		}
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("choice", choice.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AdminStorageMigrateControlAction::run(request_t& req, response_t& res)
{
	const char* task_id = req.getParameter("task_id");
	const char* action_text = req.getParameter("action");
	std::string action = action_text ? action_text : "";
	if (action != "pause" && action != "resume" && action != "cancel") {
		json_error(res, 400, "invalid migration control action", req.isKeepAlive());
		return true;
	}
	{
		std::lock_guard<webcool::mutex> guard(g_storage_migrate_mutex);
		if (task_id != nullptr && *task_id != '\0' && g_storage_migrate_task.id != task_id) {
			json_error(res, 404, "task not found", req.isKeepAlive());
			return true;
		}
		if (g_storage_migrate_task.state != "queued"
			&& g_storage_migrate_task.state != "running"
			&& g_storage_migrate_task.state != "paused"
			&& g_storage_migrate_task.state != "conflict")
		{
			json_error(res, 409, "migration task cannot be controlled", req.isKeepAlive());
			return true;
		}
		if (action == "pause") {
			g_storage_migrate_task.pause_requested = true;
		} else if (action == "resume") {
			g_storage_migrate_task.pause_requested = false;
		} else if (action == "cancel") {
			g_storage_migrate_task.cancel_requested = true;
			g_storage_migrate_task.pause_requested = false;
			if (g_storage_migrate_task.state == "conflict"
				|| g_storage_migrate_task.state == "paused")
			{
				g_storage_migrate_task.state = "cancelled";
				g_storage_migrate_task.message = "迁移已取消";
			}
		}
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("action", action.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AdminStorageMigrateCleanupAction::run(request_t& req, response_t& res)
{
	const char* task_id = req.getParameter("task_id");
	storage_migrate_task_t task;
	{
		std::lock_guard<webcool::mutex> guard(g_storage_migrate_mutex);
		if (task_id != nullptr && *task_id != '\0' && g_storage_migrate_task.id != task_id) {
			json_error(res, 404, "task not found", req.isKeepAlive());
			return true;
		}
		task = g_storage_migrate_task;
	}
	if (task.state != "done") {
		json_error(res, 409, "migration task is not completed", req.isKeepAlive());
		return true;
	}
	std::string err;
	if (!delete_storage_contents_except_backup(task.source_dir, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	{
		std::lock_guard<webcool::mutex> guard(g_storage_migrate_mutex);
		if (g_storage_migrate_task.id == task.id) {
			g_storage_migrate_task.cleanup_done = true;
		}
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("source_dir", task.source_dir.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action
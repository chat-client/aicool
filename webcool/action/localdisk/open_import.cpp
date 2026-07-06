#include "stdafx.h"
#include "local_disk_common.h"

namespace action {

namespace {

static std::string local_import_json_text(acl::json_node* node)
{
	if (node == NULL) {
		return "";
	}
	const char* value = node->get_string();
	if (value == NULL) {
		value = node->get_text();
	}
	return value ? value : "";
}

static void local_import_json_paths(acl::json_node* node,
	std::vector<std::string>& paths)
{
	if (node == NULL) {
		return;
	}
	if (!node->is_array()) {
		split_local_paths(local_import_json_text(node).c_str(), paths);
		return;
	}
	for (acl::json_node* child = node->first_child(); child != NULL;
		child = node->next_child())
	{
		const std::string path = local_import_json_text(child);
		if (!path.empty()) {
			paths.push_back(path);
		}
	}
}

static bool read_local_import_body(request_t& req, std::string& body,
	std::string& err)
{
	body.clear();
	err.clear();
	const long long content_length = req.getContentLength();
	if (content_length <= 0) {
		return true;
	}
	if (content_length > 8 * 1024 * 1024) {
		err = "request body too large";
		return false;
	}

	acl::istream& in = req.getInputStream();
	char buf[8192];
	long long read_total = 0;
	while (read_total < content_length) {
		size_t want = sizeof(buf);
		const long long remain = content_length - read_total;
		if ((long long) want > remain) {
			want = (size_t) remain;
		}
		const int n = in.read(buf, want);
		if (n < 0) {
			err = acl::last_serror();
			return false;
		}
		if (n == 0) {
			err = "request body incomplete";
			return false;
		}
		body.append(buf, (size_t) n);
		read_total += n;
	}
	return true;
}

static bool local_import_request_data(request_t& req, std::string& folder,
	std::string& folder_password, std::vector<std::string>& paths,
	std::string& err)
{
	folder = req.getParameter("folder") ? req.getParameter("folder") : "";
	folder_password = req.getParameter("folder_password")
		? req.getParameter("folder_password") : "";
	split_local_paths(req.getParameter("paths"), paths);

	if (req.getContentLength() <= 0 || !paths.empty()) {
		return true;
	}
	if (req.getRequestType() != acl::HTTP_REQUEST_TEXT_JSON) {
		if (req.getRequestType() == acl::HTTP_REQUEST_NORMAL) {
			return true;
		}
		std::string body;
		if (!read_local_import_body(req, body, err)) {
			return false;
		}
		split_local_paths(body.c_str(), paths);
		return true;
	}

	acl::json* body = req.getJson(4 * 1024 * 1024);
	if (body == NULL) {
		err = "invalid json body";
		return false;
	}

	acl::json_node* folder_node = (*body)["folder"];
	if (folder_node != NULL) {
		folder = local_import_json_text(folder_node);
	}
	acl::json_node* password_node = (*body)["folder_password"];
	if (password_node != NULL) {
		folder_password = local_import_json_text(password_node);
	}

	acl::json_node* paths_node = (*body)["paths"];
	if (paths_node != NULL) {
		paths.clear();
		local_import_json_paths(paths_node, paths);
	}
	return true;
}

} // namespace

bool LocalDiskOpenTrashAction::run(request_t& req, response_t& res)
{
	std::string err;
	if (!run_open_command(err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("message", "system Trash opened");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskOpenFileAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	const bool choose_app = req.getParameter("chooser") != NULL
		&& strcmp(req.getParameter("chooser"), "1") == 0;
	std::string path;
	std::string err;
	if (!normalize_local_path(req.getParameter("path"), path, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	struct stat st;
	if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		json_error(res, 404, "file not found", req.isKeepAlive());
		return true;
	}
	if (!ensure_local_dir_unlocked_for_request(upload_dir, req, res,
		parent_path(path), "parent directory is locked"))
	{
		return true;
	}
	bool file_lock_allowed = false;
	if (!file_lock_path_allows(upload_dir, local_file_lock_key(path),
		req.getParameter("file_password") ? req.getParameter("file_password") : "",
		file_lock_allowed, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!file_lock_allowed) {
		json_error(res, 403, "file is locked", req.isKeepAlive());
		return true;
	}
	if (!run_open_file_command(path, choose_app, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", path.c_str());
	root.add_text("message", choose_app ? "local player chooser opened" : "file opened");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskImportAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::vector<std::string> raw_paths;
	std::string raw_folder_path;
	std::string folder_password;
	std::string folder_path;
	std::string err;
	if (!local_import_request_data(req, raw_folder_path, folder_password,
		raw_paths, err))
	{
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!normalize_relative_path(raw_folder_path.c_str(), folder_path, err, true)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!folder_path.empty() && !upload_directory_exists(upload_dir, folder_path)) {
		json_error(res, 404, "target folder not found", req.isKeepAlive());
		return true;
	}
	if (!make_dir_recursive(upload_dir.c_str())) {
		json_error(res, 500, "cannot access upload dir", req.isKeepAlive());
		return true;
	}

	bool lock_allowed = false;
	std::string locked_path;
	std::string lock_err;
	if (!folder_lock_path_allows(upload_dir, folder_path,
		folder_password, lock_allowed, locked_path, lock_err))
	{
		json_error(res, 500, lock_err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!lock_allowed) {
		json_error(res, 403, "folder is locked", req.isKeepAlive());
		return true;
	}

	if (raw_paths.empty()) {
		json_error(res, 400, "no local files selected", req.isKeepAlive());
		return true;
	}

	std::vector<std::string> sources;
	std::vector<bool> source_is_dir;
	for (size_t i = 0; i < raw_paths.size(); ++i) {
		std::string source;
		if (!normalize_local_path(raw_paths[i].c_str(), source, err)) {
			json_error(res, 400, err.c_str(), req.isKeepAlive());
			return true;
		}

		struct stat st;
		if (stat(source.c_str(), &st) != 0) {
			json_error(res, 400, "local path not found", req.isKeepAlive());
			return true;
		}
		if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) {
			json_error(res, 400, "unsupported local path", req.isKeepAlive());
			return true;
		}
		sources.push_back(source);
		source_is_dir.push_back(S_ISDIR(st.st_mode));
	}

	std::vector<std::string> filtered_sources;
	std::vector<bool> filtered_is_dir;
	for (size_t i = 0; i < sources.size(); ++i) {
		bool covered_by_parent = false;
		for (size_t j = 0; j < sources.size(); ++j) {
			if (i == j || !source_is_dir[j]) {
				continue;
			}
			if (is_same_or_child_path(sources[j], sources[i])
				&& sources[j] != sources[i])
			{
				covered_by_parent = true;
				break;
			}
		}
		if (!covered_by_parent) {
			filtered_sources.push_back(sources[i]);
			filtered_is_dir.push_back(source_is_dir[i]);
		}
	}

	std::vector<std::string> dirs;
	std::vector<local_import_file_t> files;
	std::set<std::string> used_relative_paths;
	for (size_t i = 0; i < filtered_sources.size(); ++i) {
		const std::string source = filtered_sources[i];
		if (filtered_is_dir[i]) {
			std::string remote_dir = unique_upload_directory_relative(
				upload_dir, folder_path, local_base_name(source));
			if (remote_dir.empty()) {
				json_error(res, 500, "cannot create unique target directory name",
					req.isKeepAlive());
				return true;
			}
			if (used_relative_paths.find(remote_dir) != used_relative_paths.end()) {
				const std::string base_name = local_base_name(source);
				for (int n = 1; n < 10000; ++n) {
					const std::string candidate = folder_path.empty()
						? (base_name + "." + std::to_string(n))
						: (folder_path + "/" + base_name + "." + std::to_string(n));
					if (used_relative_paths.find(candidate) == used_relative_paths.end()) {
						remote_dir = candidate;
						break;
					}
				}
			}
			if (!collect_local_import_directory(source, remote_dir, dirs, files, err)) {
				json_error(res, 500, err.c_str(), req.isKeepAlive());
				return true;
			}
			used_relative_paths.insert(remote_dir);
			for (size_t j = 0; j < files.size(); ++j) {
				used_relative_paths.insert(files[j].relative_path);
			}
			continue;
		}

		struct stat st;
		if (stat(source.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
			json_error(res, 400, "local file not found", req.isKeepAlive());
			return true;
		}
		std::string relative_path;
		if (unique_upload_path(upload_dir, folder_path, local_base_name(source),
			relative_path).empty())
		{
			json_error(res, 500, "cannot create unique target file name",
				req.isKeepAlive());
			return true;
		}
		if (used_relative_paths.find(relative_path) != used_relative_paths.end()) {
			const std::string base_name = local_base_name(source);
			for (int n = 1; n < 10000; ++n) {
				const std::string candidate = folder_path.empty()
					? (base_name + "." + std::to_string(n))
					: (folder_path + "/" + base_name + "." + std::to_string(n));
				if (used_relative_paths.find(candidate) == used_relative_paths.end()) {
					relative_path = candidate;
					break;
				}
			}
		}
		local_import_file_t file;
		file.source = source;
		file.name = local_base_name(source);
		file.relative_path = relative_path;
		file.size = regular_file_size(file.source);
		files.push_back(file);
		used_relative_paths.insert(relative_path);
	}

	const std::string task_id = create_local_import_task_id();
	local_import_task_t task;
	task.state = "queued";
	task.message = "等待上传";
	task.total_bytes = 0;
	task.copied_bytes = 0;
	task.total_files = (int) files.size();
	task.saved_count = 0;
	task.pause_requested = false;
	task.cancel_requested = false;
	for (size_t i = 0; i < files.size(); ++i) {
		task.total_bytes += files[i].size;
		task.names.push_back(files[i].name);
		task.sizes.push_back(files[i].size);
		task.copied_sizes.push_back(0);
		task.file_states.push_back("pending");
	}
	update_local_import_task(task_id, task);
	std::thread(run_local_import_task, task_id, upload_dir, folder_path, dirs, files).detach();

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", task_id.c_str());
	root.add_number("count", 0);
	root.add_number("total_files", (long long) files.size());
	root.add_number("total_dirs", (long long) dirs.size());
	root.add_number("total_bytes", task.total_bytes);
	root.add_text("folder_path", folder_path.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskImportProgressAction::run(request_t& req, response_t& res)
{
	const char* task_id_param = req.getParameter("task_id");
	std::string task_id = task_id_param ? task_id_param : "";
	if (task_id.empty()) {
		json_error(res, 400, "task_id is required", req.isKeepAlive());
		return true;
	}

	local_import_task_t task;
	{
		std::lock_guard<webcool::mutex> guard(g_local_import_mutex);
		std::map<std::string, local_import_task_t>::const_iterator it =
			g_local_import_tasks.find(task_id);
		if (it == g_local_import_tasks.end()) {
			json_error(res, 404, "task not found", req.isKeepAlive());
			return true;
		}
		task = it->second;
	}

	const double progress = task.total_bytes > 0
		? ((double) task.copied_bytes * 100.0 / (double) task.total_bytes)
		: (task.state == "done" ? 100.0 : 0.0);

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("task_id", task_id.c_str());
	root.add_text("state", task.state.c_str());
	root.add_text("message", task.message.c_str());
	root.add_text("error", task.error.c_str());
	root.add_number("progress", (long long) progress);
	root.add_number("copied_bytes", task.copied_bytes);
	root.add_number("total_bytes", task.total_bytes);
	root.add_number("count", (long long) task.saved_count);
	root.add_number("saved_count", (long long) task.saved_count);
	root.add_number("total_files", (long long) task.total_files);
	root.add_bool("pause_requested", task.pause_requested);
	root.add_bool("cancel_requested", task.cancel_requested);
	acl::json_node& files = json.create_array();
	root.add_child("files", files);
	for (size_t i = 0; i < task.names.size(); ++i) {
		acl::json_node& item = files.add_child(false, true);
		const long long size = i < task.sizes.size() ? task.sizes[i] : 0;
		const long long copied = i < task.copied_sizes.size() ? task.copied_sizes[i] : 0;
		const std::string state = i < task.file_states.size() ? task.file_states[i] : "";
		const std::string remote_path = i < task.remote_paths.size() ? task.remote_paths[i] : "";
		const double file_progress = size > 0
			? ((double) copied * 100.0 / (double) size)
			: (state == "done" ? 100.0 : 0.0);
		item.add_text("name", task.names[i].c_str());
		item.add_text("path", remote_path.c_str());
		item.add_text("remote_path", remote_path.c_str());
		item.add_text("state", state.c_str());
		item.add_bool("saved", state == "done");
		item.add_number("size", size);
		item.add_number("copied", copied);
		item.add_number("progress", (long long) file_progress);
	}
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool LocalDiskImportControlAction::run(request_t& req, response_t& res)
{
	const char* task_id_param = req.getParameter("task_id");
	const char* action_param = req.getParameter("action");
	std::string task_id = task_id_param ? task_id_param : "";
	std::string action = action_param ? action_param : "";
	if (task_id.empty()) {
		json_error(res, 400, "task_id is required", req.isKeepAlive());
		return true;
	}
	if (action != "pause" && action != "resume" && action != "cancel") {
		json_error(res, 400, "invalid import control action", req.isKeepAlive());
		return true;
	}
	{
		std::lock_guard<webcool::mutex> guard(g_local_import_mutex);
		std::map<std::string, local_import_task_t>::iterator it =
			g_local_import_tasks.find(task_id);
		if (it == g_local_import_tasks.end()) {
			json_error(res, 404, "task not found", req.isKeepAlive());
			return true;
		}
		if (it->second.state != "queued"
			&& it->second.state != "running"
			&& it->second.state != "paused")
		{
			json_error(res, 409, "import task cannot be controlled",
				req.isKeepAlive());
			return true;
		}
		if (action == "pause") {
			it->second.pause_requested = true;
		} else if (action == "resume") {
			it->second.pause_requested = false;
		} else if (action == "cancel") {
			it->second.cancel_requested = true;
			it->second.pause_requested = false;
			if (it->second.state == "paused") {
				it->second.state = "cancelled";
				it->second.message = "上传已取消";
			}
		}
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("action", action.c_str());
	return sendJson(res, 200, root, req.isKeepAlive());
}


} // namespace action

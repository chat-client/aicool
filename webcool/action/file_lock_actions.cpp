#include "stdafx.h"
#include "actions.h"
#include "action_util.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#ifdef _WIN32
#include "../platform_compat.h"
#else
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <fstream>
#include <map>
#include "common/webcool_mutex.h"
#include <string>

namespace action {

namespace {

static webcool::mutex g_file_lock_mutex;
static const char* g_remote_file_lock_prefix = "remote:";
static const char* g_local_file_lock_prefix = "local:";
static const char* g_local_dir_lock_prefix = "local-dir:";

#if 0
static void json_error(response_t& res, int status, const char* msg,
	bool keep_alive)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", msg ? msg : "unknown error");
	sendJson(res, status, root, keep_alive);
}
#endif

static std::string file_locks_file_path(const std::string& upload_dir) {
	return upload_dir + "/.file_locks.txt";
}

static bool validate_file_lock_password(const std::string& password,
	std::string& err)
{
	err.clear();
	if (password.empty()) {
		err = "password is empty";
		return false;
	}
	if (password.size() > 120) {
		err = "password is too long";
		return false;
	}
	for (size_t i = 0; i < password.size(); ++i) {
		unsigned char c = (unsigned char) password[i];
		if (c < 32 || c == 127) {
			err = "password contains control character";
			return false;
		}
	}
	return true;
}

static bool load_file_locks_locked(const std::string& upload_dir,
	std::map<std::string, std::string>& locks, std::string& err)
{
	locks.clear();
	err.clear();
	std::ifstream in(file_locks_file_path(upload_dir).c_str());
	if (!in.good()) {
		if (in.eof() || errno == ENOENT) {
			return true;
		}
		return true;
	}

	std::string line;
	while (std::getline(in, line)) {
		if (line.empty()) {
			continue;
		}
		const std::string::size_type tab = line.find('\t');
		if (tab == std::string::npos) {
			continue;
		}
		const std::string key = line.substr(0, tab);
		const std::string password = line.substr(tab + 1);
		if (!key.empty() && !password.empty()) {
			locks[key] = password;
		}
	}
	if (in.bad()) {
		err = "read file locks failed";
		return false;
	}
	return true;
}

static bool save_file_locks_locked(const std::string& upload_dir,
	const std::map<std::string, std::string>& locks, std::string& err)
{
	err.clear();
	if (!make_dir_recursive(upload_dir.c_str())) {
		err = "cannot access upload dir";
		return false;
	}
	std::ofstream out(file_locks_file_path(upload_dir).c_str(),
		std::ios::out | std::ios::trunc);
	if (!out.good()) {
		err = "open file locks file failed";
		return false;
	}
	for (std::map<std::string, std::string>::const_iterator it = locks.begin();
		it != locks.end(); ++it)
	{
		out << it->first << '\t' << it->second << '\n';
	}
	if (!out.good()) {
		err = "write file locks failed";
		return false;
	}
	return true;
}

static bool normalize_local_path_for_lock(const char* input, bool directory,
	std::string& path, std::string& err)
{
	err.clear();
	path.clear();
	if (input == NULL || *input == '\0') {
		err = "missing local file path";
		return false;
	}
#ifdef _WIN32
	const size_t len = strlen(input);
	const bool absolute = (len >= 3
			&& ((input[0] >= 'A' && input[0] <= 'Z')
				|| (input[0] >= 'a' && input[0] <= 'z'))
			&& input[1] == ':'
			&& (input[2] == '/' || input[2] == '\\'))
		|| (len >= 2
			&& (input[0] == '/' || input[0] == '\\')
			&& (input[1] == '/' || input[1] == '\\'));
	if (!absolute) {
		err = "absolute path is required";
		return false;
	}
#else
	if (*input != '/') {
		err = "absolute path is required";
		return false;
	}
#endif
	char resolved[PATH_MAX];
	if (realpath(input, resolved) == NULL) {
		err = strerror(errno);
		return false;
	}
	struct stat st;
	if (stat(resolved, &st) != 0) {
		err = strerror(errno);
		return false;
	}
	if (directory && !S_ISDIR(st.st_mode)) {
		err = "directory not found";
		return false;
	}
	if (!directory && !S_ISREG(st.st_mode)) {
		err = "file not found";
		return false;
	}
	path = resolved;
	return true;
}

static std::string remote_file_lock_key(const std::string& relative_path) {
	return std::string(g_remote_file_lock_prefix) + relative_path;
}

static std::string local_file_lock_key(const std::string& path) {
	return std::string(g_local_file_lock_prefix) + path;
}

static std::string local_dir_lock_key(const std::string& path) {
	return std::string(g_local_dir_lock_prefix) + path;
}

static bool is_same_or_child_path(const std::string& base,
	const std::string& path)
{
	if (base == path) {
		return true;
	}
	if (base == "/") {
		return !path.empty() && path[0] == '/';
	}
	return path.size() > base.size()
		&& path.compare(0, base.size(), base) == 0
		&& (path[base.size()] == '/' || path[base.size()] == '\\');
}

static bool request_file_lock_key(request_t& req, const std::string& upload_dir,
	std::string& key, std::string& file_path, bool& local, std::string& err)
{
	key.clear();
	file_path.clear();
	local = false;
	const char* local_text = req.getParameter("local");
	local = local_text != NULL
		&& (strcmp(local_text, "1") == 0
			|| strcasecmp(local_text, "true") == 0
			|| strcasecmp(local_text, "yes") == 0);
	if (local) {
		const char* dir_text = req.getParameter("dir");
		const bool directory = dir_text != NULL
			&& (strcmp(dir_text, "1") == 0
				|| strcasecmp(dir_text, "true") == 0
				|| strcasecmp(dir_text, "yes") == 0);
		if (!normalize_local_path_for_lock(req.getParameter("path"), directory, file_path, err)) {
			return false;
		}
		if (directory && file_path == "/") {
			err = "root directory cannot be locked";
			return false;
		}
		key = directory ? local_dir_lock_key(file_path) : local_file_lock_key(file_path);
		return true;
	}
	if (!normalize_relative_path(req.getParameter("file"), file_path, err, false)) {
		return false;
	}
	if (!upload_regular_file_exists(upload_dir, file_path)) {
		err = "file not found";
		return false;
	}
	key = remote_file_lock_key(file_path);
	return true;
}

} // namespace

bool file_lock_path_allows(const std::string& upload_dir,
	const std::string& file_key, const std::string& password,
	bool& allowed, std::string& err)
{
	allowed = false;
	err.clear();
	std::lock_guard<webcool::mutex> guard(g_file_lock_mutex);
	std::map<std::string, std::string> locks;
	if (!load_file_locks_locked(upload_dir, locks, err)) {
		return false;
	}
	std::map<std::string, std::string>::const_iterator it = locks.find(file_key);
	if (it == locks.end()) {
		allowed = true;
		return true;
	}
	allowed = it->second == password;
	return true;
}

bool file_lock_path_has_lock(const std::string& upload_dir,
	const std::string& file_key, bool& locked, std::string& err)
{
	locked = false;
	err.clear();
	std::lock_guard<webcool::mutex> guard(g_file_lock_mutex);
	std::map<std::string, std::string> locks;
	if (!load_file_locks_locked(upload_dir, locks, err)) {
		return false;
	}
	locked = locks.find(file_key) != locks.end();
	return true;
}

bool file_lock_rename_key(const std::string& upload_dir,
	const std::string& old_key, const std::string& new_key, std::string& err)
{
	err.clear();
	if (old_key.empty() || new_key.empty() || old_key == new_key) {
		return true;
	}
	std::lock_guard<webcool::mutex> guard(g_file_lock_mutex);
	std::map<std::string, std::string> locks;
	if (!load_file_locks_locked(upload_dir, locks, err)) {
		return false;
	}
	std::map<std::string, std::string>::iterator it = locks.find(old_key);
	if (it == locks.end()) {
		return true;
	}
	locks[new_key] = it->second;
	locks.erase(it);
	return save_file_locks_locked(upload_dir, locks, err);
}

bool file_lock_rename_prefix(const std::string& upload_dir,
	const std::string& old_prefix, const std::string& new_prefix,
	std::string& err)
{
	err.clear();
	if (old_prefix.empty() || new_prefix.empty() || old_prefix == new_prefix) {
		return true;
	}
	std::lock_guard<webcool::mutex> guard(g_file_lock_mutex);
	std::map<std::string, std::string> locks;
	if (!load_file_locks_locked(upload_dir, locks, err)) {
		return false;
	}
	std::map<std::string, std::string> updated;
	for (std::map<std::string, std::string>::const_iterator it = locks.begin();
		it != locks.end(); ++it)
	{
		const std::string& key = it->first;
		if (key == old_prefix) {
			updated[new_prefix] = it->second;
		} else if (key.size() > old_prefix.size()
			&& key.compare(0, old_prefix.size(), old_prefix) == 0
			&& key[old_prefix.size()] == '/')
		{
			updated[new_prefix + key.substr(old_prefix.size())] = it->second;
		} else {
			updated[key] = it->second;
		}
	}
	locks.swap(updated);
	return save_file_locks_locked(upload_dir, locks, err);
}

bool local_dir_lock_path_allows(const std::string& upload_dir,
	const std::string& path, const std::string& password,
	bool& allowed, std::string& locked_path, std::string& err)
{
	allowed = false;
	locked_path.clear();
	err.clear();
	std::lock_guard<webcool::mutex> guard(g_file_lock_mutex);
	std::map<std::string, std::string> locks;
	if (!load_file_locks_locked(upload_dir, locks, err)) {
		return false;
	}
	for (std::map<std::string, std::string>::const_iterator it = locks.begin();
		it != locks.end(); ++it)
	{
		if (it->first.compare(0, strlen(g_local_dir_lock_prefix),
			g_local_dir_lock_prefix) != 0)
		{
			continue;
		}
		const std::string candidate = it->first.substr(strlen(g_local_dir_lock_prefix));
		if (is_same_or_child_path(candidate, path)
			&& candidate.size() >= locked_path.size())
		{
			locked_path = candidate;
		}
	}
	if (locked_path.empty()) {
		allowed = true;
		return true;
	}
	std::map<std::string, std::string>::const_iterator lock_it =
		locks.find(local_dir_lock_key(locked_path));
	allowed = lock_it != locks.end() && lock_it->second == password;
	return true;
}

bool local_dir_lock_path_has_lock(const std::string& upload_dir,
	const std::string& path, bool& locked, std::string& err)
{
	locked = false;
	err.clear();
	std::lock_guard<webcool::mutex> guard(g_file_lock_mutex);
	std::map<std::string, std::string> locks;
	if (!load_file_locks_locked(upload_dir, locks, err)) {
		return false;
	}
	locked = locks.find(local_dir_lock_key(path)) != locks.end();
	return true;
}

bool named_lock_set(const std::string& upload_dir,
	const std::string& key, const std::string& password, std::string& err)
{
	err.clear();
	if (key.empty()) {
		err = "lock key is empty";
		return false;
	}
	if (!validate_file_lock_password(password, err)) {
		return false;
	}
	std::lock_guard<webcool::mutex> guard(g_file_lock_mutex);
	std::map<std::string, std::string> locks;
	if (!load_file_locks_locked(upload_dir, locks, err)) {
		return false;
	}
	locks[key] = password;
	return save_file_locks_locked(upload_dir, locks, err);
}

bool named_lock_remove(const std::string& upload_dir,
	const std::string& key, const std::string& password, std::string& err)
{
	err.clear();
	std::lock_guard<webcool::mutex> guard(g_file_lock_mutex);
	std::map<std::string, std::string> locks;
	if (!load_file_locks_locked(upload_dir, locks, err)) {
		return false;
	}
	std::map<std::string, std::string>::iterator it = locks.find(key);
	if (it == locks.end()) {
		err = "lock not found";
		return false;
	}
	if (it->second != password) {
		err = "password is incorrect";
		return false;
	}
	locks.erase(it);
	return save_file_locks_locked(upload_dir, locks, err);
}

bool named_lock_verify(const std::string& upload_dir,
	const std::string& key, const std::string& password, bool& allowed,
	std::string& err)
{
	allowed = false;
	err.clear();
	std::lock_guard<webcool::mutex> guard(g_file_lock_mutex);
	std::map<std::string, std::string> locks;
	if (!load_file_locks_locked(upload_dir, locks, err)) {
		return false;
	}
	std::map<std::string, std::string>::const_iterator it = locks.find(key);
	if (it == locks.end()) {
		allowed = true;
		return true;
	}
	allowed = it->second == password;
	return true;
}

bool FileLockAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string key, file_path, err;
	bool local = false;
	if (!request_file_lock_key(req, upload_dir, key, file_path, local, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	const char* password_text = req.getParameter("password");
	const std::string password = password_text ? password_text : "";
	if (!validate_file_lock_password(password, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	{
		std::lock_guard<webcool::mutex> guard(g_file_lock_mutex);
		std::map<std::string, std::string> locks;
		if (!load_file_locks_locked(upload_dir, locks, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		locks[key] = password;
		if (!save_file_locks_locked(upload_dir, locks, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("file", file_path.c_str());
	root.add_bool("local", local);
	root.add_text("message", "file locked");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool FileUnlockAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string key, file_path, err;
	bool local = false;
	if (!request_file_lock_key(req, upload_dir, key, file_path, local, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	const std::string password = req.getParameter("password")
		? req.getParameter("password")
		: "";

	{
		std::lock_guard<webcool::mutex> guard(g_file_lock_mutex);
		std::map<std::string, std::string> locks;
		if (!load_file_locks_locked(upload_dir, locks, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		std::map<std::string, std::string>::iterator it = locks.find(key);
		if (it == locks.end()) {
			json_error(res, 404, "file lock not found", req.isKeepAlive());
			return true;
		}
		if (it->second != password) {
			json_error(res, 403, "password is incorrect", req.isKeepAlive());
			return true;
		}
		locks.erase(it);
		if (!save_file_locks_locked(upload_dir, locks, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("file", file_path.c_str());
	root.add_bool("local", local);
	root.add_text("message", "file lock removed");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool FileLockVerifyAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string key, file_path, err;
	bool local = false;
	if (!request_file_lock_key(req, upload_dir, key, file_path, local, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	bool allowed = false;
	if (!file_lock_path_allows(upload_dir, key,
		req.getParameter("password") ? req.getParameter("password") : "",
		allowed, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!allowed) {
		json_error(res, 403, "password is incorrect", req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("file", file_path.c_str());
	root.add_bool("local", local);
	root.add_text("message", "file lock verified");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

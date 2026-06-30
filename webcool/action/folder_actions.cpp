#include "stdafx.h"
#include "actions.h"
#include "action_util.h"

#ifdef _WIN32
#include "../platform_compat.h"
#else
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace action {

extern std::mutex g_recycle_mutex;

namespace {

struct folder_node_t {
	std::string name;
	std::string path;
	long long direct_file_count;
	std::vector<folder_node_t> children;
};

static std::string remote_file_lock_key(const std::string& path) {
	return std::string("remote:") + path;
}

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

static bool request_bool_param(request_t& req, const char* name) {
	const char* value = req.getParameter(name);
	return value != NULL && (
		strcmp(value, "1") == 0
		|| strcasecmp(value, "true") == 0
		|| strcasecmp(value, "yes") == 0
		|| strcasecmp(value, "on") == 0);
}

static bool should_skip_entry(const char* name, bool show_hidden) {
	if (name == NULL || *name == '\0') {
		return true;
	}
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
		return true;
	}
	if (!show_hidden && name[0] == '.') {
		return true;
	}
	return false;
}

static bool should_skip_self_parent_entry(const char* name) {
	if (name == NULL || *name == '\0') {
		return true;
	}
	return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static bool copy_regular_file_plain(const std::string& source,
	const std::string& dest, mode_t mode, std::string& err)
{
	FILE* in = fopen(source.c_str(), "rb");
	if (in == NULL) {
		err = strerror(errno);
		return false;
	}
	FILE* out = fopen(dest.c_str(), "wb");
	if (out == NULL) {
		err = strerror(errno);
		fclose(in);
		return false;
	}

	char buf[1024 * 64];
	bool ok = true;
	while (true) {
		const size_t n = fread(buf, 1, sizeof(buf), in);
		if (n > 0 && fwrite(buf, 1, n, out) != n) {
			err = strerror(errno);
			ok = false;
			break;
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
		::unlink(dest.c_str());
		return false;
	}
	(void) chmod(dest.c_str(), mode & 0777);
	return true;
}

static bool remove_path_recursive(const std::string& path, std::string& err)
{
	struct stat st;
	if (lstat(path.c_str(), &st) != 0) {
		if (errno == ENOENT) {
			return true;
		}
		err = strerror(errno);
		logger_error("stat %s error=%s", path.c_str(), err.c_str());
		return false;
	}

	if (!S_ISDIR(st.st_mode)) {
		if (::unlink(path.c_str()) == 0 || errno == ENOENT) {
			return true;
		}
		err = strerror(errno);
		logger_error("unlink %s error=%s", path.c_str(), err.c_str());
		return false;
	}

	DIR* dir = opendir(path.c_str());
	if (dir == NULL) {
		if (errno == ENOENT) {
			return true;
		}
		err = strerror(errno);
		logger_error("opendir %s error=%s", path.c_str(), err.c_str());
		return false;
	}

	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		const std::string child = path + "/" + entry->d_name;
		if (!remove_path_recursive(child, err)) {
			closedir(dir);
			return false;
		}
	}
	closedir(dir);

	if (::rmdir(path.c_str()) == 0 || errno == ENOENT) {
		logger_debug(DEBUG_FOLDER, 1, "remove %s ok", path.c_str());
		return true;
	}
	err = strerror(errno);
	logger_error("rmdir %s error=%s", path.c_str(), err.c_str());
	return false;
}

static bool copy_path_recursive(const std::string& source,
	const std::string& dest, std::string& err)
{
	struct stat st;
	if (lstat(source.c_str(), &st) != 0) {
		err = strerror(errno);
		return false;
	}
	if (S_ISREG(st.st_mode)) {
		return copy_regular_file_plain(source, dest, st.st_mode, err);
	}
	if (S_ISLNK(st.st_mode)) {
		char target[4096];
		const ssize_t n = readlink(source.c_str(), target, sizeof(target) - 1);
		if (n < 0) {
			err = strerror(errno);
			return false;
		}
		target[n] = '\0';
		if (::symlink(target, dest.c_str()) != 0) {
			err = strerror(errno);
			return false;
		}
		return true;
	}
	if (!S_ISDIR(st.st_mode)) {
		err = "unsupported file type in directory copy";
		return false;
	}

	if (::mkdir(dest.c_str(), st.st_mode & 0777) != 0) {
		err = strerror(errno);
		return false;
	}
	bool ok = true;
	DIR* dir = opendir(source.c_str());
	if (dir == NULL) {
		err = strerror(errno);
		ok = false;
	} else {
		struct dirent* entry = NULL;
		while ((entry = readdir(dir)) != NULL) {
			if (should_skip_self_parent_entry(entry->d_name)) {
				continue;
			}
			const std::string child_source = source + "/" + entry->d_name;
			const std::string child_dest = dest + "/" + entry->d_name;
			if (!copy_path_recursive(child_source, child_dest, err)) {
				ok = false;
				break;
			}
		}
		closedir(dir);
	}
	if (!ok) {
		std::string remove_err;
		remove_path_recursive(dest, remove_err);
		return false;
	}
	(void) chmod(dest.c_str(), st.st_mode & 0777);
	return true;
}

static bool validate_folder_segment(const std::string& name, std::string& err) {
	err.clear();
	if (name.empty()) {
		err = "folder name is empty";
		return false;
	}
	if (name.size() > 120) {
		err = "folder name is too long";
		return false;
	}
	for (size_t i = 0; i < name.size(); ++i) {
		unsigned char c = (unsigned char) name[i];
		if (c < 32 || c == 127) {
			err = "folder name contains control character";
			return false;
		}
		if (name[i] == '/' || name[i] == '\\') {
			err = "folder name cannot contain slash";
			return false;
		}
	}
	if (name == "." || name == "..") {
		err = "invalid folder name";
		return false;
	}
	return true;
}

static bool list_folder_tree(const std::string& upload_dir,
	const std::string& relative_path, folder_node_t& node, std::string& err,
	long long& folder_count, bool show_hidden)
{
	err.clear();
	node.direct_file_count = 0;
	node.children.clear();

	std::string full = join_upload_path(upload_dir, relative_path);
	DIR* dir = opendir(full.c_str());
	if (dir == NULL) {
		err = strerror(errno);
		return false;
	}

	std::vector<folder_node_t> children;
	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (should_skip_entry(entry->d_name, show_hidden)) {
			continue;
		}

		std::string child_name(entry->d_name);
		std::string child_rel = relative_path.empty()
			? child_name
			: (relative_path + "/" + child_name);
		std::string child_full = join_upload_path(upload_dir, child_rel);

		struct stat st;
		if (stat(child_full.c_str(), &st) != 0) {
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			folder_node_t child;
			child.name = child_name;
			child.path = child_rel;
			folder_count++;
			if (!list_folder_tree(upload_dir, child_rel, child, err, folder_count, show_hidden)) {
				closedir(dir);
				return false;
			}
			children.push_back(child);
		} else if (S_ISREG(st.st_mode)) {
			if (!is_protected_virtual_path(child_rel)) {
				node.direct_file_count++;
			}
		}
	}

	closedir(dir);
	std::sort(children.begin(), children.end(),
		[](const folder_node_t& a, const folder_node_t& b) {
			return a.name < b.name;
		});
	node.children.swap(children);
	return true;
}

static bool folder_lock_unlocked(
	const std::string& path,
	const std::map<std::string, std::string>& locks,
	const std::map<std::string, std::string>& unlocked_locks)
{
	std::map<std::string, std::string>::const_iterator lock_it = locks.find(path);
	if (lock_it == locks.end()) {
		return true;
	}
	std::map<std::string, std::string>::const_iterator unlocked_it = unlocked_locks.find(path);
	return unlocked_it != unlocked_locks.end() && unlocked_it->second == lock_it->second;
}

static void append_folder_json(acl::json& json, acl::json_node& arr,
	const folder_node_t& node, const std::map<std::string, std::string>& locks,
	const std::map<std::string, std::string>& unlocked_locks)
{
	const bool locked = locks.find(node.path) != locks.end();
	const bool unlocked = folder_lock_unlocked(node.path, locks, unlocked_locks);
	acl::json_node& item = arr.add_child(false, true);
	item.add_text("name", node.name.c_str());
	item.add_text("path", node.path.c_str());
	item.add_text("parent_path", parent_relative_path(node.path).c_str());
	item.add_bool("locked", locked);
	item.add_number("file_count", locked && !unlocked ? 0 : node.direct_file_count);
	acl::json_node& children = json.create_array();
	item.add_child("children", children);
	if (!locked || unlocked) {
		for (size_t i = 0; i < node.children.size(); ++i) {
			append_folder_json(json, children, node.children[i], locks, unlocked_locks);
		}
	}
	item.add_number("folder_count", locked && !unlocked ? 0 : (long long) node.children.size());
}

static bool is_same_or_child_path(const std::string& base_path,
	const std::string& test_path)
{
	if (base_path.empty()) {
		return true;
	}
	if (test_path == base_path) {
		return true;
	}
	return test_path.size() > base_path.size()
		&& test_path.compare(0, base_path.size(), base_path) == 0
		&& test_path[base_path.size()] == '/';
}

static std::mutex g_folder_lock_mutex;
static unsigned long g_folder_recycle_seq = 0;

static std::string make_folder_recycle_unique_name(
	const std::string& original_name)
{
	(void) original_name;
	// Caller must hold g_recycle_mutex (see alloc_recycle_folder_target).
	const time_t now = time(NULL);
	char buf[128];
	++g_folder_recycle_seq;
	snprintf(buf, sizeof(buf), "%lld_%d_folder_%lu",
		(long long) now, (int) getpid(), g_folder_recycle_seq);
	return std::string(buf);
}

static bool alloc_recycle_folder_target(const std::string& upload_dir,
	const std::string& original_name, std::string& recycle_rel,
	std::string& err)
{
	err.clear();
	recycle_rel.clear();
	std::string path = join_upload_path(upload_dir, recycle_folder_name());
	if (!make_dir_recursive(path.c_str())) {
		err = "cannot access recycle folder: ";
		err += path.c_str();
		return false;
	}
	std::lock_guard<std::mutex> guard(g_recycle_mutex);
	for (int i = 0; i < 1024; ++i) {
		const std::string unique_name = make_folder_recycle_unique_name(original_name);
		const std::string candidate = std::string(recycle_folder_name()) + "/" + unique_name;
		if (!upload_regular_file_exists(upload_dir, candidate)
			&& !upload_directory_exists(upload_dir, candidate))
		{
			recycle_rel = candidate;
			return true;
		}
	}
	err = "cannot allocate recycle folder name";
	return false;
}

static std::string folder_locks_file_path(const std::string& upload_dir) {
	return upload_dir + "/.folder_locks.txt";
}

static bool validate_lock_password(const std::string& password,
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
		if (password[i] == '\t') {
			err = "password cannot contain tab";
			return false;
		}
	}
	return true;
}

static bool load_folder_locks_locked(const std::string& upload_dir,
	std::map<std::string, std::string>& locks, std::string& err)
{
	err.clear();
	locks.clear();
	std::ifstream in(folder_locks_file_path(upload_dir).c_str());
	if (!in.good()) {
		return true;
	}

	std::string line;
	while (std::getline(in, line)) {
		if (line.empty()) {
			continue;
		}
		std::string::size_type pos = line.find('\t');
		if (pos == std::string::npos) {
			continue;
		}
		std::string path = line.substr(0, pos);
		std::string password = line.substr(pos + 1);
		if (!path.empty() && !password.empty()) {
			locks[path] = password;
		}
	}
	if (!in.eof() && in.fail()) {
		err = "read folder locks failed";
		return false;
	}
	return true;
}

static bool save_folder_locks_locked(const std::string& upload_dir,
	const std::map<std::string, std::string>& locks, std::string& err)
{
	err.clear();
	if (!make_dir_recursive(upload_dir.c_str())) {
		err = "cannot access upload dir";
		return false;
	}
	std::ofstream out(folder_locks_file_path(upload_dir).c_str(),
		std::ios::out | std::ios::trunc);
	if (!out.good()) {
		err = "open folder locks file failed";
		return false;
	}
	for (std::map<std::string, std::string>::const_iterator it = locks.begin();
		it != locks.end(); ++it)
	{
		out << it->first << '\t' << it->second << '\n';
	}
	out.close();
	if (!out.good()) {
		err = "write folder locks file failed";
		return false;
	}
	return true;
}

static bool find_locked_ancestor_locked(
	const std::map<std::string, std::string>& locks,
	const std::string& relative_path, std::string& locked_path)
{
	locked_path.clear();
	for (std::map<std::string, std::string>::const_iterator it = locks.begin();
		it != locks.end(); ++it)
	{
		if (is_same_or_child_path(it->first, relative_path)
			&& it->first.size() >= locked_path.size())
		{
			locked_path = it->first;
		}
	}
	return !locked_path.empty();
}

static bool rename_folder_locks_prefix(const std::string& upload_dir,
	const std::string& old_prefix, const std::string& new_prefix,
	std::string& err)
{
	err.clear();
	if (old_prefix.empty() || new_prefix.empty() || old_prefix == new_prefix) {
		return true;
	}
	std::lock_guard<std::mutex> guard(g_folder_lock_mutex);
	std::map<std::string, std::string> locks;
	if (!load_folder_locks_locked(upload_dir, locks, err)) {
		return false;
	}
	std::map<std::string, std::string> next;
	for (std::map<std::string, std::string>::const_iterator it = locks.begin();
		it != locks.end(); ++it)
	{
		std::string path = it->first;
		if (path == old_prefix) {
			path = new_prefix;
		} else if (path.size() > old_prefix.size()
			&& path.compare(0, old_prefix.size(), old_prefix) == 0
			&& path[old_prefix.size()] == '/')
		{
			path = new_prefix + path.substr(old_prefix.size());
		}
		next[path] = it->second;
	}
	return save_folder_locks_locked(upload_dir, next, err);
}

} // namespace

bool init_category_folder_db(const std::string& upload_dir, std::string& err) {
	err.clear();
	if (!make_dir_recursive(upload_dir.c_str())) {
		err = "cannot access upload dir";
		return false;
	}
	return true;
}

bool folder_lock_path_allows(const std::string& upload_dir,
	const std::string& relative_path, const std::string& password,
	bool& allowed, std::string& locked_path, std::string& err)
{
	allowed = false;
	locked_path.clear();
	std::lock_guard<std::mutex> guard(g_folder_lock_mutex);
	std::map<std::string, std::string> locks;
	if (!load_folder_locks_locked(upload_dir, locks, err)) {
		return false;
	}
	if (!find_locked_ancestor_locked(locks, relative_path, locked_path)) {
		allowed = true;
		return true;
	}
	std::map<std::string, std::string>::const_iterator it = locks.find(locked_path);
	allowed = it != locks.end() && it->second == password;
	return true;
}

bool folder_lock_path_has_lock(const std::string& upload_dir,
	const std::string& relative_path, bool& locked, std::string& err)
{
	locked = false;
	std::lock_guard<std::mutex> guard(g_folder_lock_mutex);
	std::map<std::string, std::string> locks;
	if (!load_folder_locks_locked(upload_dir, locks, err)) {
		return false;
	}
	locked = locks.find(relative_path) != locks.end();
	return true;
}

bool folder_lock_rename_prefix(const std::string& upload_dir,
	const std::string& old_prefix, const std::string& new_prefix,
	std::string& err)
{
	return rename_folder_locks_prefix(upload_dir, old_prefix, new_prefix, err);
}

bool folder_bind_file(const std::string&, const std::string&, long long,
	std::string& err)
{
	err.clear();
	err = "folder binding by id is no longer supported";
	return false;
}

bool folder_unbind_file(const std::string&, const std::string&, std::string& err) {
	err.clear();
	return true;
}

bool folder_load_file_bindings(const std::string&,
	std::map<std::string, long long>& file_to_folder_id,
	std::map<long long, std::string>& folder_id_to_name,
	std::string& err)
{
	err.clear();
	file_to_folder_id.clear();
	folder_id_to_name.clear();
	return true;
}

bool FolderListAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	if (!make_dir_recursive(upload_dir.c_str())) {
		json_error(res, 500, "cannot access upload dir", req.isKeepAlive());
		return true;
	}

	std::string err;
	if (!ensure_shared_upload_dir(err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	const bool show_hidden = request_bool_param(req, "show_hidden");
	folder_node_t root_node;
	root_node.name = "根目录";
	root_node.path.clear();
	long long folder_count = 0;
	if (!list_folder_tree(upload_dir, std::string(), root_node, err, folder_count, show_hidden)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	bool has_shared_folder = false;
	for (size_t i = 0; i < root_node.children.size(); ++i) {
		if (root_node.children[i].path == shared_folder_name()) {
			has_shared_folder = true;
			break;
		}
	}
	if (!has_shared_folder) {
		folder_node_t shared_node;
		shared_node.name = shared_folder_name();
		shared_node.path = shared_folder_name();
		folder_count++;
		if (!list_folder_tree(upload_dir, shared_folder_name(), shared_node,
			err, folder_count, show_hidden))
		{
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		root_node.children.push_back(shared_node);
		std::sort(root_node.children.begin(), root_node.children.end(),
			[](const folder_node_t& a, const folder_node_t& b) {
				return a.name < b.name;
			});
	}

	std::map<std::string, std::string> locks;
	{
		std::lock_guard<std::mutex> guard(g_folder_lock_mutex);
		if (!load_folder_locks_locked(upload_dir, locks, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	}
	std::map<std::string, std::string> unlocked_locks;
	long long unlock_count = 0;
	const char* unlock_count_text = req.getParameter("unlock_count");
	if (unlock_count_text != NULL && *unlock_count_text != '\0') {
		unlock_count = atoll(unlock_count_text);
		if (unlock_count < 0 || unlock_count > 100) {
			json_error(res, 400, "invalid unlock count", req.isKeepAlive());
			return true;
		}
	}
	for (long long i = 0; i < unlock_count; ++i) {
		std::ostringstream path_key;
		path_key << "unlock_path_" << i;
		std::ostringstream password_key;
		password_key << "unlock_password_" << i;
		const char* path_text = req.getParameter(path_key.str().c_str());
		const char* password_text = req.getParameter(password_key.str().c_str());
		if (path_text == NULL || password_text == NULL) {
			continue;
		}
		std::string unlock_path;
		if (!normalize_relative_path(path_text, unlock_path, err, false)) {
			json_error(res, 400, err.c_str(), req.isKeepAlive());
			return true;
		}
		unlocked_locks[unlock_path] = password_text;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("root_name", root_node.name.c_str());
	root.add_text("root_path", "");
	root.add_number("file_count", root_node.direct_file_count);
	root.add_number("count", folder_count);
	acl::json_node& folders = json.create_array();
	root.add_child("folders", folders);
	for (size_t i = 0; i < root_node.children.size(); ++i) {
		append_folder_json(json, folders, root_node.children[i], locks, unlocked_locks);
	}
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool FolderCreateAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string parent_path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("parent"), parent_path, err, true)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(parent_path)) {
		json_error(res, 409, "cannot create folder inside recycle folder", req.isKeepAlive());
		return true;
	}
	bool lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, parent_path,
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!lock_allowed) {
		json_error(res, 403, "folder is locked", req.isKeepAlive());
		return true;
	}

	std::string name = req.getParameter("name") ? req.getParameter("name") : "";
	if (!validate_folder_segment(name, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	if (!make_dir_recursive(upload_dir.c_str())) {
		json_error(res, 500, "cannot access upload dir", req.isKeepAlive());
		return true;
	}
	if (!parent_path.empty() && !upload_directory_exists(upload_dir, parent_path)) {
		json_error(res, 404, "parent folder not found", req.isKeepAlive());
		return true;
	}

	const std::string new_path = parent_path.empty() ? name : (parent_path + "/" + name);
	const std::string full = join_upload_path(upload_dir, new_path);
	struct stat st;
	if (stat(full.c_str(), &st) == 0) {
		json_error(res, 409, S_ISDIR(st.st_mode) ? "folder already exists" : "target path already exists", req.isKeepAlive());
		return true;
	}
	if (!make_dir(full.c_str())) {
		json_error(res, 500, "create folder failed", req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("name", name.c_str());
	root.add_text("path", new_path.c_str());
	root.add_text("parent_path", parent_path.c_str());
	root.add_text("message", "folder created");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool FolderRenameAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string old_path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("path"), old_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(old_path)) {
		json_error(res, 409, "recycle folder is protected", req.isKeepAlive());
		return true;
	}
	if (is_shared_root_path(old_path)) {
		json_error(res, 409, "shared folder is protected", req.isKeepAlive());
		return true;
	}
	bool lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, old_path,
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!lock_allowed) {
		json_error(res, 403, "folder is locked", req.isKeepAlive());
		return true;
	}

	std::string new_name = req.getParameter("name") ? req.getParameter("name") : "";
	if (!validate_folder_segment(new_name, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!upload_directory_exists(upload_dir, old_path)) {
		json_error(res, 404, "folder not found", req.isKeepAlive());
		return true;
	}

	const std::string parent_path = parent_relative_path(old_path);
	const std::string new_path = parent_path.empty() ? new_name : (parent_path + "/" + new_name);
	if (is_recycle_root_path(new_path)) {
		json_error(res, 409, "recycle folder name is protected", req.isKeepAlive());
		return true;
	}
	if (new_path == old_path) {
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("path", old_path.c_str());
		root.add_text("message", "folder unchanged");
		return sendJson(res, 200, root, req.isKeepAlive());
	}
	if (upload_directory_exists(upload_dir, new_path)) {
		json_error(res, 409, "folder already exists", req.isKeepAlive());
		return true;
	}

	std::string old_full = join_upload_path(upload_dir, old_path);
	std::string new_full = join_upload_path(upload_dir, new_path);
	if (::rename(old_full.c_str(), new_full.c_str()) != 0) {
		json_error(res, 500, "rename folder failed", req.isKeepAlive());
		return true;
	}

	if (!rename_folder_locks_prefix(upload_dir, old_path, new_path, err)) {
		::rename(new_full.c_str(), old_full.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	bool tag_updated = false;
	std::string rename_err;
	if (!tag_rename_folder_prefix(upload_dir, old_path, new_path, rename_err)) {
		std::string rollback_err;
		rename_folder_locks_prefix(upload_dir, new_path, old_path, rollback_err);
		::rename(new_full.c_str(), old_full.c_str());
		json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
		return true;
	}
	tag_updated = true;
	if (!video_resume_rename_folder_prefix(upload_dir, old_path, new_path, rename_err)) {
		if (tag_updated) {
			std::string rollback_err;
			tag_rename_folder_prefix(upload_dir, new_path, old_path, rollback_err);
		}
		std::string lock_rollback_err;
		rename_folder_locks_prefix(upload_dir, new_path, old_path, lock_rollback_err);
		::rename(new_full.c_str(), old_full.c_str());
		json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", new_path.c_str());
	root.add_text("old_path", old_path.c_str());
	root.add_text("name", new_name.c_str());
	root.add_text("message", "folder renamed");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool FolderDeleteAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("path"), path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(path)) {
		json_error(res, 409, "recycle folder is protected", req.isKeepAlive());
		return true;
	}
	if (is_shared_root_path(path)) {
		json_error(res, 409, "shared folder is protected", req.isKeepAlive());
		return true;
	}
	bool lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, path,
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!lock_allowed) {
		json_error(res, 403, "folder is locked", req.isKeepAlive());
		return true;
	}
	if (!upload_directory_exists(upload_dir, path)) {
		json_error(res, 404, "folder not found", req.isKeepAlive());
		return true;
	}

	const std::string folder_name = base_name_from_relative_path(path);
	std::string recycle_path;
	if (!alloc_recycle_folder_target(upload_dir, folder_name, recycle_path, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	const std::string from_full = join_upload_path(upload_dir, path);
	const std::string to_full = join_upload_path(upload_dir, recycle_path);
	if (::rename(from_full.c_str(), to_full.c_str()) != 0) {
		json_error(res, 500, "move folder to recycle bin failed", req.isKeepAlive());
		return true;
	}

	if (!rename_folder_locks_prefix(upload_dir, path, recycle_path, err)) {
		(void) ::rename(to_full.c_str(), from_full.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	bool tag_updated = false;
	std::string rename_err;
	if (!tag_rename_folder_prefix(upload_dir, path, recycle_path, rename_err)) {
		std::string rollback_err;
		rename_folder_locks_prefix(upload_dir, recycle_path, path, rollback_err);
		(void) ::rename(to_full.c_str(), from_full.c_str());
		json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
		return true;
	}
	tag_updated = true;
	if (!video_resume_rename_folder_prefix(upload_dir, path, recycle_path, rename_err)) {
		if (tag_updated) {
			std::string rollback_err;
			tag_rename_folder_prefix(upload_dir, recycle_path, path, rollback_err);
		}
		std::string lock_rollback_err;
		rename_folder_locks_prefix(upload_dir, recycle_path, path, lock_rollback_err);
		(void) ::rename(to_full.c_str(), from_full.c_str());
		json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!recycle_bin_insert_record(upload_dir, recycle_path, path, err)) {
		std::string rollback_err;
		video_resume_rename_folder_prefix(upload_dir, recycle_path, path, rollback_err);
		tag_rename_folder_prefix(upload_dir, recycle_path, path, rollback_err);
		rename_folder_locks_prefix(upload_dir, recycle_path, path, rollback_err);
		(void) ::rename(to_full.c_str(), from_full.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	{
		std::string sync_err;
		std::vector<std::string> sync_paths;
		std::vector<std::string> delete_paths;
		std::vector<std::string> move_from_paths;
		std::vector<std::string> move_to_paths;
		move_from_paths.push_back(path);
		move_to_paths.push_back(recycle_path);
		sync_paths.push_back(".recycle_bin.db");
		sync_paths.push_back(".recycle_bin.db-wal");
		sync_paths.push_back(".recycle_bin.db-shm");
		sync_paths.push_back(".recycle_bin.db-journal");
		sync_paths.push_back(".tag_catalog.db");
		sync_paths.push_back(".tag_catalog.db-wal");
		sync_paths.push_back(".tag_catalog.db-shm");
		sync_paths.push_back(".tag_catalog.db-journal");
		sync_paths.push_back(".video_resume.db");
		sync_paths.push_back(".video_resume.db-wal");
		sync_paths.push_back(".video_resume.db-shm");
		sync_paths.push_back(".video_resume.db-journal");
		sync_paths.push_back(".folder_locks.txt");
		(void) storage_backup_sync_path_moves(upload_dir, sync_paths, delete_paths,
			move_from_paths, move_to_paths, sync_err);
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", path.c_str());
	root.add_text("recycle_path", recycle_path.c_str());
	root.add_text("message", "folder moved to recycle bin");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool FolderLockAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("path"), path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(path)) {
		json_error(res, 409, "recycle folder is protected", req.isKeepAlive());
		return true;
	}
	if (!upload_directory_exists(upload_dir, path)) {
		json_error(res, 404, "folder not found", req.isKeepAlive());
		return true;
	}
	bool lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, path,
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!lock_allowed) {
		json_error(res, 403, "folder is locked", req.isKeepAlive());
		return true;
	}

	const std::string password = req.getParameter("password") ? req.getParameter("password") : "";
	if (!validate_lock_password(password, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}

	{
		std::lock_guard<std::mutex> guard(g_folder_lock_mutex);
		std::map<std::string, std::string> locks;
		if (!load_folder_locks_locked(upload_dir, locks, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		locks[path] = password;
		if (!save_folder_locks_locked(upload_dir, locks, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", path.c_str());
	root.add_text("message", "folder locked");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool FolderUnlockAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("path"), path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	const std::string password = req.getParameter("password") ? req.getParameter("password") : "";

	{
		std::lock_guard<std::mutex> guard(g_folder_lock_mutex);
		std::map<std::string, std::string> locks;
		if (!load_folder_locks_locked(upload_dir, locks, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		std::map<std::string, std::string>::iterator it = locks.find(path);
		if (it == locks.end()) {
			json_error(res, 404, "folder lock not found", req.isKeepAlive());
			return true;
		}
		if (it->second != password) {
			json_error(res, 403, "password is incorrect", req.isKeepAlive());
			return true;
		}
		locks.erase(it);
		if (!save_folder_locks_locked(upload_dir, locks, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", path.c_str());
	root.add_text("message", "folder unlocked");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool FolderLockVerifyAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("path"), path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	const std::string password = req.getParameter("password") ? req.getParameter("password") : "";
	bool allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, path, password, allowed, locked_path, err)) {
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
	root.add_text("path", path.c_str());
	root.add_text("locked_path", locked_path.c_str());
	root.add_text("message", "folder lock verified");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool FolderMoveAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string source_path;
	std::string err;
	if (!normalize_relative_path(req.getParameter("path"), source_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(source_path)) {
		json_error(res, 409, "recycle folder is protected", req.isKeepAlive());
		return true;
	}
	if (is_shared_root_path(source_path)) {
		json_error(res, 409, "shared folder is protected", req.isKeepAlive());
		return true;
	}
	bool source_lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, source_path,
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		source_lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!source_lock_allowed) {
		json_error(res, 403, "source folder is locked", req.isKeepAlive());
		return true;
	}

	std::string target_parent;
	if (!normalize_relative_path(req.getParameter("folder"), target_parent, err, true)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(target_parent)) {
		json_error(res, 409, "cannot move folder into recycle folder", req.isKeepAlive());
		return true;
	}
	bool target_lock_allowed = false;
	if (!folder_lock_path_allows(upload_dir, target_parent,
		req.getParameter("target_folder_password") ? req.getParameter("target_folder_password") : "",
		target_lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!target_lock_allowed) {
		json_error(res, 403, "target folder is locked", req.isKeepAlive());
		return true;
	}

	if (!upload_directory_exists(upload_dir, source_path)) {
		json_error(res, 404, "folder not found", req.isKeepAlive());
		return true;
	}
	if (!target_parent.empty() && !upload_directory_exists(upload_dir, target_parent)) {
		json_error(res, 404, "target folder not found", req.isKeepAlive());
		return true;
	}
	if (is_same_or_child_path(source_path, target_parent)) {
		json_error(res, 409, "cannot move folder into itself or its child", req.isKeepAlive());
		return true;
	}

	const std::string folder_name = base_name_from_relative_path(source_path);
	const std::string target_path = target_parent.empty()
		? folder_name
		: (target_parent + "/" + folder_name);
	if (target_path == source_path) {
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("path", source_path.c_str());
		root.add_text("message", "folder unchanged");
		return sendJson(res, 200, root, req.isKeepAlive());
	}
	if (upload_directory_exists(upload_dir, target_path)) {
		json_error(res, 409, "target folder already exists", req.isKeepAlive());
		return true;
	}

	const std::string source_full = join_upload_path(upload_dir, source_path);
	const std::string target_full = join_upload_path(upload_dir, target_path);
	if (::rename(source_full.c_str(), target_full.c_str()) != 0) {
		json_error(res, 500, "move folder failed", req.isKeepAlive());
		return true;
	}

	if (!rename_folder_locks_prefix(upload_dir, source_path, target_path, err)) {
		::rename(target_full.c_str(), source_full.c_str());
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	bool tag_updated = false;
	std::string rename_err;
	if (!tag_rename_folder_prefix(upload_dir, source_path, target_path, rename_err)) {
		std::string lock_rollback_err;
		rename_folder_locks_prefix(upload_dir, target_path, source_path, lock_rollback_err);
		::rename(target_full.c_str(), source_full.c_str());
		json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
		return true;
	}
	tag_updated = true;
	if (!video_resume_rename_folder_prefix(upload_dir, source_path, target_path, rename_err)) {
		if (tag_updated) {
			std::string rollback_err;
			tag_rename_folder_prefix(upload_dir, target_path, source_path, rollback_err);
		}
		std::string lock_rollback_err;
		rename_folder_locks_prefix(upload_dir, target_path, source_path, lock_rollback_err);
		::rename(target_full.c_str(), source_full.c_str());
		json_error(res, 500, rename_err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", target_path.c_str());
	root.add_text("old_path", source_path.c_str());
	root.add_text("parent_path", target_parent.c_str());
	root.add_text("name", folder_name.c_str());
	root.add_text("message", "folder moved");
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool FolderCopyAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string source_path;
	std::string err;
	const bool overwrite = req.getParameter("overwrite") != NULL
		&& strcmp(req.getParameter("overwrite"), "1") == 0;
	if (!normalize_relative_path(req.getParameter("path"), source_path, err, false)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (source_path.empty()) {
		json_error(res, 409, "root folder cannot be copied", req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(source_path)) {
		json_error(res, 409, "recycle folder is protected", req.isKeepAlive());
		return true;
	}
	if (!upload_directory_exists(upload_dir, source_path)) {
		json_error(res, 404, "folder not found", req.isKeepAlive());
		return true;
	}

	bool source_lock_allowed = false;
	std::string locked_path;
	if (!folder_lock_path_allows(upload_dir, source_path,
		req.getParameter("folder_password") ? req.getParameter("folder_password") : "",
		source_lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!source_lock_allowed) {
		json_error(res, 403, "source folder is locked", req.isKeepAlive());
		return true;
	}

	std::string target_parent;
	if (!normalize_relative_path(req.getParameter("folder"), target_parent, err, true)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (is_recycle_file_path(target_parent)) {
		json_error(res, 409, "cannot copy folder into recycle folder", req.isKeepAlive());
		return true;
	}
	if (!target_parent.empty() && !upload_directory_exists(upload_dir, target_parent)) {
		json_error(res, 404, "target folder not found", req.isKeepAlive());
		return true;
	}
	if (is_same_or_child_path(source_path, target_parent)) {
		json_error(res, 409, "cannot copy folder into itself or its child",
			req.isKeepAlive());
		return true;
	}

	bool target_lock_allowed = false;
	if (!folder_lock_path_allows(upload_dir, target_parent,
		req.getParameter("target_folder_password") ? req.getParameter("target_folder_password") : "",
		target_lock_allowed, locked_path, err))
	{
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}
	if (!target_lock_allowed) {
		json_error(res, 403, "target folder is locked", req.isKeepAlive());
		return true;
	}

	const std::string folder_name = base_name_from_relative_path(source_path);
	const std::string target_path = target_parent.empty()
		? folder_name
		: (target_parent + "/" + folder_name);
	if (target_path == source_path) {
		json_error(res, 409, "source and destination are the same", req.isKeepAlive());
		return true;
	}

	const std::string source_full = join_upload_path(upload_dir, source_path);
	const std::string target_full = join_upload_path(upload_dir, target_path);
	struct stat target_st;
	if (lstat(target_full.c_str(), &target_st) == 0) {
		if (!overwrite) {
			json_error(res, 409, "target already contains a path with same name",
				req.isKeepAlive());
			return true;
		}
		if (S_ISDIR(target_st.st_mode)) {
			bool dest_lock_allowed = false;
			if (!folder_lock_path_allows(upload_dir, target_path,
				req.getParameter("target_folder_password") ? req.getParameter("target_folder_password") : "",
				dest_lock_allowed, locked_path, err))
			{
				json_error(res, 500, err.c_str(), req.isKeepAlive());
				return true;
			}
			if (!dest_lock_allowed) {
				json_error(res, 403, "destination folder is locked", req.isKeepAlive());
				return true;
			}
		} else {
			bool dest_file_allowed = false;
			if (!file_lock_path_allows(upload_dir, remote_file_lock_key(target_path),
				req.getParameter("file_password") ? req.getParameter("file_password") : "",
				dest_file_allowed, err))
			{
				json_error(res, 500, err.c_str(), req.isKeepAlive());
				return true;
			}
			if (!dest_file_allowed) {
				json_error(res, 403, "destination file is locked", req.isKeepAlive());
				return true;
			}
		}
		if (!remove_path_recursive(target_full, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	} else if (errno != ENOENT) {
		json_error(res, 500, strerror(errno), req.isKeepAlive());
		return true;
	}

	if (req.getParameter("async") != NULL && strcmp(req.getParameter("async"), "1") == 0) {
		const std::string task_id = start_remote_copy_task(source_full, target_full,
			target_path, true, upload_dir);
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		root.add_text("task_id", task_id.c_str());
		root.add_text("path", target_path.c_str());
		root.add_text("folder_path", target_parent.c_str());
		root.add_bool("directory", true);
		root.add_text("message", "copy task started");
		return sendJson(res, 200, root, req.isKeepAlive());
	}
	if (!copy_path_recursive(source_full, target_full, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return true;
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", target_path.c_str());
	root.add_text("source", source_path.c_str());
	root.add_text("folder_path", target_parent.c_str());
	root.add_bool("overwritten", overwrite);
	root.add_text("message", "folder copied");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

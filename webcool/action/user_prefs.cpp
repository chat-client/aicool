#include "stdafx.h"
#include "actions.h"
#include "action_util.h"

#ifdef _WIN32
#include "../platform_compat.h"
#else
#include <unistd.h>
#endif

#include <cerrno>
#include <fstream>
#include "common/webcool_mutex.h"
#include <sys/stat.h>

namespace action {
namespace {

webcool::mutex g_user_prefs_mutex;
const char* kAuthDirName = "webcool_auth";

std::string auth_prefs_dir(const std::string& upload_dir)
{
	return join_upload_path(upload_dir,
		std::string(kAuthDirName) + "/user_prefs");
}

std::string user_prefs_file(const std::string& upload_dir,
	const std::string& username)
{
	return auth_prefs_dir(upload_dir) + "/" + username + ".prefs";
}

bool ensure_user_prefs_dir(const std::string& upload_dir, std::string& err)
{
	const std::string dir = auth_prefs_dir(upload_dir);
	if (!make_dir_recursive(dir.c_str())) {
		err = "cannot create user preferences directory";
		return false;
	}
	return true;
}

void sync_user_prefs_backup(const std::string& upload_dir,
	const std::string& username)
{
	std::vector<std::string> sync_paths;
	std::vector<std::string> delete_paths;
	std::string err;
	sync_paths.emplace_back(std::string(kAuthDirName) + "/user_prefs/"
		+ username + ".prefs");
	(void) storage_backup_sync_paths(upload_dir, sync_paths, delete_paths, err);
}

std::string normalize_ui_language_value(const std::string& value)
{
	return value == "en" ? "en" : "zh";
}

std::string normalize_font_size_value(const std::string& value)
{
	if (value == "md" || value == "lg") {
		return value;
	}
	return "sm";
}

} // namespace

user_prefs_t default_user_prefs()
{
	user_prefs_t prefs;
	prefs.ui_language = "zh";
	prefs.font_size = "sm";
	return prefs;
}

bool valid_username_for_prefs(const std::string& username, std::string& err)
{
	if (username.size() < 3 || username.size() > 40) {
		err = "invalid username";
		return false;
	}
	for (const char c : username) {
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
		{
			err = "invalid username";
			return false;
		}
	}
	return true;
}

bool load_user_prefs(const std::string& upload_dir,
	const std::string& username, user_prefs_t& prefs, std::string& err)
{
	std::lock_guard<webcool::mutex> guard(g_user_prefs_mutex);
	prefs = default_user_prefs();
	if (!valid_username_for_prefs(username, err)) {
		return false;
	}
	std::ifstream in(user_prefs_file(upload_dir, username).c_str(), std::ios::in);
	if (!in.good()) {
		return true;
	}
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty()) {
			continue;
		}
		const std::string::size_type pos = line.find('=');
		if (pos == std::string::npos) {
			err = "invalid user preferences database";
			return false;
		}
		const std::string key = line.substr(0, pos);
		const std::string value = line.substr(pos + 1);
		if (key == "ui_language") {
			prefs.ui_language = normalize_ui_language_value(value);
		} else if (key == "font_size") {
			prefs.font_size = normalize_font_size_value(value);
		}
	}
	return true;
}

bool save_user_prefs(const std::string& upload_dir,
	const std::string& username, const user_prefs_t& prefs, std::string& err)
{
	std::lock_guard<webcool::mutex> guard(g_user_prefs_mutex);
	if (!valid_username_for_prefs(username, err)) {
		return false;
	}
	if (!ensure_user_prefs_dir(upload_dir, err)) {
		return false;
	}
	user_prefs_t normalized;
	normalized.ui_language = normalize_ui_language_value(prefs.ui_language);
	normalized.font_size = normalize_font_size_value(prefs.font_size);
	const std::string path = user_prefs_file(upload_dir, username);
	const std::string tmp = path + ".tmp";
	std::ofstream out(tmp.c_str(), std::ios::out | std::ios::trunc);
	if (!out.good()) {
		err = "cannot write user preferences";
		return false;
	}
	out << "ui_language=" << normalized.ui_language << '\n'
		<< "font_size=" << normalized.font_size << '\n';
	out.close();
	if (!out.good()) {
		err = "cannot flush user preferences";
		return false;
	}
	if (rename(tmp.c_str(), path.c_str()) != 0) {
		err = strerror(errno);
		return false;
	}
	sync_user_prefs_backup(upload_dir, username);
	return true;
}

bool delete_user_prefs(const std::string& upload_dir,
	const std::string& username, std::string& err)
{
	std::lock_guard<webcool::mutex> guard(g_user_prefs_mutex);
	if (!valid_username_for_prefs(username, err)) {
		return false;
	}
	const std::string path = user_prefs_file(upload_dir, username);
	if (unlink(path.c_str()) != 0 && errno != ENOENT) {
		err = strerror(errno);
		return false;
	}
	std::vector<std::string> sync_paths;
	std::vector<std::string> delete_paths;
	delete_paths.emplace_back(std::string(kAuthDirName) + "/user_prefs/"
		+ username + ".prefs");
	(void) storage_backup_sync_paths(upload_dir, sync_paths, delete_paths, err);
	return true;
}

bool rename_user_prefs(const std::string& upload_dir,
	const std::string& old_username, const std::string& new_username,
	std::string& err)
{
	std::lock_guard<webcool::mutex> guard(g_user_prefs_mutex);
	std::string old_err;
	std::string new_err;
	if (!valid_username_for_prefs(old_username, old_err)
		|| !valid_username_for_prefs(new_username, new_err))
	{
		err = old_err.empty() ? new_err : old_err;
		return false;
	}
	if (old_username == new_username) {
		return true;
	}
	const std::string old_path = user_prefs_file(upload_dir, old_username);
	const std::string new_path = user_prefs_file(upload_dir, new_username);
	struct stat st;
	if (stat(old_path.c_str(), &st) != 0) {
		if (errno == ENOENT) {
			return true;
		}
		err = strerror(errno);
		return false;
	}
	if (stat(new_path.c_str(), &st) == 0) {
		err = "target user preferences already exist";
		return false;
	}
	if (!ensure_user_prefs_dir(upload_dir, err)) {
		return false;
	}
	if (rename(old_path.c_str(), new_path.c_str()) != 0) {
		err = strerror(errno);
		return false;
	}
	std::vector<std::string> sync_paths;
	std::vector<std::string> delete_paths;
	sync_paths.emplace_back(std::string(kAuthDirName) + "/user_prefs/"
		+ new_username + ".prefs");
	delete_paths.emplace_back(std::string(kAuthDirName) + "/user_prefs/"
		+ old_username + ".prefs");
	(void) storage_backup_sync_paths(upload_dir, sync_paths, delete_paths, err);
	return true;
}

void add_user_prefs_json(acl::json_node& root, const user_prefs_t& prefs)
{
	root.add_text("ui_language",
		normalize_ui_language_value(prefs.ui_language).c_str());
	root.add_text("font_size", normalize_font_size_value(prefs.font_size).c_str());
}

bool append_authenticated_user_prefs(acl::json_node& root,
	const std::string& upload_dir, const std::string& username)
{
	user_prefs_t prefs;
	std::string err;
	if (!load_user_prefs(upload_dir, username, prefs, err)) {
		add_user_prefs_json(root, default_user_prefs());
		return false;
	}
	add_user_prefs_json(root, prefs);
	return true;
}

bool AuthPreferencesGetAction::run(const request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string username;
	bool admin = false;
	if (!auth_request_allowed(req, upload_dir, username, admin)) {
		return auth_send_required(req, res);
	}
	user_prefs_t prefs;
	std::string err;
	if (!load_user_prefs(upload_dir, username, prefs, err)) {
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", err.c_str());
		return sendJson(res, 500, root, req.isKeepAlive());
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	add_user_prefs_json(root, prefs);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AuthPreferencesSetAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string username;
	bool admin = false;
	if (!auth_request_allowed(req, upload_dir, username, admin)) {
		return auth_send_required(req, res);
	}
	user_prefs_t prefs;
	std::string err;
	if (!load_user_prefs(upload_dir, username, prefs, err)) {
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", err.c_str());
		return sendJson(res, 500, root, req.isKeepAlive());
	}
	const char* ui_language = req.getParameter("ui_language");
	const char* font_size = req.getParameter("font_size");
	if (ui_language != nullptr && ui_language[0] != '\0') {
		prefs.ui_language = normalize_ui_language_value(ui_language);
	}
	if (font_size != nullptr && font_size[0] != '\0') {
		prefs.font_size = normalize_font_size_value(font_size);
	}
	if ((ui_language == nullptr || ui_language[0] == '\0')
		&& (font_size == nullptr || font_size[0] == '\0'))
	{
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "ui_language or font_size required");
		return sendJson(res, 400, root, req.isKeepAlive());
	}
	if (!save_user_prefs(upload_dir, username, prefs, err)) {
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", err.c_str());
		return sendJson(res, 500, root, req.isKeepAlive());
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	add_user_prefs_json(root, prefs);
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action

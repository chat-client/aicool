#include "stdafx.h"
#include "actions.h"
#include "action_util.h"

#ifdef _WIN32
#include "../platform_compat.h"
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace action {
namespace {

auto kAuthCookieName = "webcool_auth";
const char* kAuthDirName = "webcool_auth";
const char* kLegacyAuthDirName = ".webcool_auth";
const char* kUserStorageDirName = "webcool_users";
const char* kLegacyUserStorageDirName = ".webcool_users";
constexpr long long kSessionTtlSeconds = 7LL * 24 * 60 * 60;

struct user_record_t {
	std::string username;
	std::string salt;
	std::string password_hash;
	bool admin = false;
};

std::mutex g_auth_mutex;

#if 0
static void json_error(response_t& res, int status, const char* msg,
	bool keep_alive)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", msg);
	sendJson(res, status, root, keep_alive);
}
#endif

std::string auth_dir(const std::string& upload_dir)
{
	return join_upload_path(upload_dir, kAuthDirName);
}

std::string users_file(const std::string& upload_dir)
{
	return auth_dir(upload_dir) + "/users.db";
}

bool directory_exists(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool migrate_legacy_auth_dir(const std::string& upload_dir, std::string& err)
{
	err.clear();
	const std::string target = auth_dir(upload_dir);
	if (directory_exists(target)) {
		return true;
	}

	const std::string legacy = join_upload_path(upload_dir, kLegacyAuthDirName);
	if (!directory_exists(legacy)) {
		return true;
	}

	if (rename(legacy.c_str(), target.c_str()) != 0) {
		err = std::string("cannot migrate auth directory: ") + strerror(errno);
		return false;
	}
	return true;
}

std::string user_storage_dir(const std::string& upload_dir,
	const std::string& username)
{
	return join_upload_path(upload_dir,
		std::string(kUserStorageDirName) + "/" + username);
}

std::string legacy_user_storage_dir(const std::string& upload_dir,
	const std::string& username)
{
	return join_upload_path(upload_dir,
		std::string(kLegacyUserStorageDirName) + "/" + username);
}

bool migrate_legacy_user_storage_dir(const std::string& upload_dir,
	const std::string& username, std::string& err)
{
	err.clear();
	const std::string target = user_storage_dir(upload_dir, username);
	if (directory_exists(target)) {
		return true;
	}

	const std::string legacy = legacy_user_storage_dir(upload_dir, username);
	if (!directory_exists(legacy)) {
		return true;
	}

	const std::string target_parent = join_upload_path(upload_dir,
		kUserStorageDirName);
	if (!make_dir_recursive(target_parent.c_str())) {
		err = "cannot create user storage directory";
		return false;
	}
	if (rename(legacy.c_str(), target.c_str()) != 0) {
		err = std::string("cannot migrate user storage directory: ")
			+ strerror(errno);
		return false;
	}
	return true;
}

void sync_users_db_backup(const std::string& upload_dir)
{
	std::vector<std::string> sync_paths;
	std::vector<std::string> delete_paths;
	std::string err;
	sync_paths.emplace_back(std::string(kAuthDirName) + "/users.db");
	(void) storage_backup_sync_paths(upload_dir, sync_paths, delete_paths, err);
}

bool ensure_auth_dir(const std::string& upload_dir, std::string& err)
{
	if (!migrate_legacy_auth_dir(upload_dir, err)) {
		return false;
	}
	const std::string dir = auth_dir(upload_dir);
	if (!make_dir_recursive(dir.c_str())) {
		err = "cannot create auth directory";
		return false;
	}
	return true;
}

std::vector<std::string> split_tab(const std::string& line)
{
	std::vector<std::string> out;
	std::string::size_type pos = 0;
	while (true) {
		std::string::size_type tab = line.find('\t', pos);
		if (tab == std::string::npos) {
			out.push_back(line.substr(pos));
			break;
		}
		out.push_back(line.substr(pos, tab - pos));
		pos = tab + 1;
	}
	return out;
}

bool load_users_unlocked(const std::string& upload_dir,
	std::vector<user_record_t>& users, std::string& err)
{
	users.clear();
	std::ifstream in(users_file(upload_dir).c_str(), std::ios::in);
	if (!in.good()) {
		return true;
	}
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty()) {
			continue;
		}
		std::vector<std::string> parts = split_tab(line);
		if (parts.size() != 4) {
			err = "invalid users database";
			return false;
		}
		user_record_t user;
		user.username = parts[0];
		user.salt = parts[1];
		user.password_hash = parts[2];
		user.admin = parts[3] == "admin";
		users.push_back(user);
	}
	return true;
}

bool save_users_unlocked(const std::string& upload_dir,
	const std::vector<user_record_t>& users, std::string& err)
{
	if (!ensure_auth_dir(upload_dir, err)) {
		return false;
	}
	const std::string path = users_file(upload_dir);
	const std::string tmp = path + ".tmp";
	std::ofstream out(tmp.c_str(), std::ios::out | std::ios::trunc);
	if (!out.good()) {
		err = "cannot write users database";
		return false;
	}
	for (const auto & user : users) {
		out << user.username << '\t'
			<< user.salt << '\t'
			<< user.password_hash << '\t'
			<< (user.admin ? "admin" : "user") << '\n';
	}
	out.close();
	if (!out.good()) {
		err = "cannot flush users database";
		return false;
	}
	if (rename(tmp.c_str(), path.c_str()) != 0) {
		err = strerror(errno);
		return false;
	}
	return true;
}

bool valid_username(const std::string& username, std::string& err)
{
	if (username.size() < 3 || username.size() > 40) {
		err = "username length must be 3-40";
		return false;
	}
	for (const char c : username) {
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
		{
			err = "username contains invalid character";
			return false;
		}
	}
	return true;
}

bool valid_password(const std::string& password, std::string& err)
{
	if (password.size() < 6 || password.size() > 128) {
		err = "password length must be 6-128";
		return false;
	}
	for (char i : password) {
		if (static_cast<unsigned char>(i) < 32) {
			err = "password contains control character";
			return false;
		}
	}
	return true;
}

std::string md5_hex(const std::string& text)
{
	char out[33] = {};
	acl::md5::md5_string(text.data(), text.size(), nullptr, 0, out, sizeof(out));
	return out;
}

std::string random_hex(size_t bytes)
{
	static unsigned long long seq = 0;
	static bool seeded = false;
	std::ostringstream ss;
	if (!seeded) {
		seeded = true;
		srand(static_cast<unsigned int>(time(nullptr) ^ getpid()));
	}
#ifndef _WIN32
	std::vector<unsigned char> random_bytes(bytes, 0);
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd >= 0) {
		size_t got = 0;
		while (got < bytes) {
			const ssize_t n = read(fd, &random_bytes[got], bytes - got);
			if (n <= 0) {
				break;
			}
			got += static_cast<size_t>(n);
		}
		close(fd);
		if (got == bytes) {
			static const char* hex = "0123456789abcdef";
			for (unsigned char b : random_bytes) {
					ss << hex[(b >> 4) & 0xf] << hex[b & 0xf];
			}
			return ss.str();
		}
	}
#endif
	for (size_t i = 0; i < bytes; ++i) {
		auto v = static_cast<unsigned int>(rand());
		v ^= static_cast<unsigned int>(time(nullptr));
		v ^= static_cast<unsigned int>(getpid());
		v ^= static_cast<unsigned int>(++seq * 1103515245ULL);
		const auto b = static_cast<unsigned char>(v & 0xff);
		static const char* hex = "0123456789abcdef";
		ss << hex[(b >> 4) & 0xf] << hex[b & 0xf];
	}
	return ss.str();
}

std::string password_hash(const std::string& salt,
	const std::string& password)
{
	std::string digest = md5_hex(salt + ":" + password);
	for (int i = 0; i < 9999; ++i) {
		digest = md5_hex(salt + ":" + digest + ":" + password);
	}
	return digest;
}

user_record_t* find_user(std::vector<user_record_t>& users,
	const std::string& username)
{
	for (auto & user : users) {
		if (user.username == username) {
			return &user;
		}
	}
	return nullptr;
}

std::string session_signature(const user_record_t& user,
	const std::string& expires, const std::string& nonce)
{
	return md5_hex(user.username + "~" + (user.admin ? "admin" : "user")
		+ "~" + expires + "~" + nonce + "~" + user.salt
		+ "~" + user.password_hash);
}

std::string create_session_token(const user_record_t& user)
{
	const std::string expires = std::to_string(
		time(nullptr) + static_cast<time_t>(kSessionTtlSeconds));
	const std::string nonce = random_hex(8);
	return user.username + "~" + (user.admin ? "admin" : "user") + "~"
		+ expires + "~" + nonce + "~" + session_signature(user, expires, nonce);
}

bool parse_session_token(const std::string& token, std::vector<std::string>& parts)
{
	parts.clear();
	std::string::size_type pos = 0;
	while (true) {
		std::string::size_type next = token.find('~', pos);
		if (next == std::string::npos) {
			parts.push_back(token.substr(pos));
			break;
		}
		parts.push_back(token.substr(pos, next - pos));
		pos = next + 1;
	}
	return parts.size() == 5;
}

std::string cookie_value(const request_t& req, const char* name)
{
	const char* cookie = req.getHeader("Cookie");
	if (cookie == nullptr || name == nullptr || *name == '\0') {
		return "";
	}
	const std::string all(cookie);
	const std::string key = std::string(name) + "=";
	std::string::size_type pos = 0;
	while (pos < all.size()) {
		while (pos < all.size() && (all[pos] == ' ' || all[pos] == ';')) {
			++pos;
		}
		std::string::size_type end = all.find(';', pos);
		if (end == std::string::npos) {
			end = all.size();
		}
		if (all.compare(pos, key.size(), key) == 0) {
			return all.substr(pos + key.size(), end - pos - key.size());
		}
		pos = end + 1;
	}
	return "";
}

std::string bearer_token(const request_t& req)
{
	const char* header = req.getHeader("Authorization");
	if (header != nullptr) {
		const std::string value(header);
		const std::string prefix = "Bearer ";
		if (value.compare(0, prefix.size(), prefix) == 0) {
			return value.substr(prefix.size());
		}
	}
	const char* query_token = req.getParameter("access_token");
	if (query_token != nullptr && query_token[0] != '\0') {
		return query_token;
	}
	return "";
}

void set_auth_cookie(response_t& res, const std::string& token)
{
	std::string value = std::string(kAuthCookieName) + "=" + token
		+ "; Path=/; HttpOnly; SameSite=Strict; Max-Age="
		+ std::to_string(kSessionTtlSeconds);
	res.setHeader("Set-Cookie", value.c_str());
	res.setHeader("Cache-Control", "no-store");
}

void clear_auth_cookie(response_t& res)
{
	std::string value = std::string(kAuthCookieName)
		+ "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0";
	res.setHeader("Set-Cookie", value.c_str());
	res.setHeader("Cache-Control", "no-store");
}

bool current_user(const request_t& req, const std::string& upload_dir,
	std::string& username, bool& admin)
{
	std::string token = bearer_token(req);
	if (token.empty()) {
		token = cookie_value(req, kAuthCookieName);
	}
	if (token.empty()) {
		return false;
	}
	std::vector<std::string> parts;
	if (!parse_session_token(token, parts)) {
		return false;
	}
	const std::string& token_username = parts[0];
	const std::string& token_role = parts[1];
	const std::string& token_expires = parts[2];
	const std::string& token_nonce = parts[3];
	const std::string& token_sig = parts[4];
	const long long expires = atoll(token_expires.c_str());
	if (expires <= static_cast<long long>(time(nullptr))) {
		return false;
	}

	std::lock_guard<std::mutex> guard(g_auth_mutex);
	std::vector<user_record_t> users;
	std::string err;
	if (!load_users_unlocked(upload_dir, users, err)) {
		return false;
	}
	user_record_t* user = find_user(users, token_username);
	if (user == nullptr) {
		return false;
	}
	if ((user->admin && token_role != "admin")
		|| (!user->admin && token_role != "user"))
	{
		return false;
	}
	if (session_signature(*user, token_expires, token_nonce) != token_sig) {
		return false;
	}
	username = user->username;
	admin = user->admin;
	return true;
}

bool require_admin(const request_t& req, response_t& res,
	const std::string& upload_dir, std::string& username)
{
	bool admin = false;
	if (!auth_request_allowed(req, upload_dir, username, admin)) {
		return auth_send_required(req, res);
	}
	if (!admin) {
		json_error(res, 403, "admin permission required", req.isKeepAlive());
		return false;
	}
	return true;
}

void add_user_json(acl::json_node& parent, const user_record_t& user)
{
	acl::json_node& node = parent.add_child(false, true);
	node.add_text("username", user.username.c_str());
	node.add_bool("admin", user.admin);
}

} // namespace

bool auth_system_initialized(const std::string& upload_dir)
{
	std::lock_guard<std::mutex> guard(g_auth_mutex);
	std::vector<user_record_t> users;
	std::string err;
	return load_users_unlocked(upload_dir, users, err) && !users.empty();
}

bool auth_request_allowed(const request_t& req, const std::string& upload_dir,
	std::string& username, bool& admin)
{
	if (!auth_system_initialized(upload_dir)) {
		return false;
	}
	return current_user(req, upload_dir, username, admin);
}

bool auth_current_user(const request_t& req, const std::string& upload_dir,
	std::string& username, bool& admin)
{
	return current_user(req, upload_dir, username, admin);
}

bool authenticated_user_upload_dir(const request_t& req,
	const std::string& upload_dir, std::string& user_upload_dir,
	std::string& err)
{
	std::string username;
	bool admin = false;
	err.clear();
	user_upload_dir.clear();
	if (!auth_request_allowed(req, upload_dir, username, admin)) {
		err = "authentication required";
		return false;
	}
	if (!migrate_legacy_user_storage_dir(upload_dir, username, err)) {
		user_upload_dir.clear();
		return false;
	}
	user_upload_dir = user_storage_dir(upload_dir, username);
	if (!make_dir_recursive(user_upload_dir.c_str())) {
		err = "cannot create user upload directory";
		user_upload_dir.clear();
		return false;
	}
	if (!init_category_folder_db(user_upload_dir, err)) {
		user_upload_dir.clear();
		return false;
	}
	if (!init_recycle_bin_db(user_upload_dir, err)) {
		user_upload_dir.clear();
		return false;
	}
	return true;
}

bool auth_send_required(const request_t& req, response_t& res)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", "authentication required");
	root.add_bool("auth_required", true);
	return sendJson(res, 401, root, req.isKeepAlive());
}

bool AuthStatusAction::run(const request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string username;
	bool admin = false;
	const bool initialized = auth_system_initialized(upload_dir);
	const bool authenticated = initialized
		&& current_user(req, upload_dir, username, admin);

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_bool("initialized", initialized);
	root.add_bool("authenticated", authenticated);
	if (authenticated) {
		root.add_text("username", username.c_str());
		root.add_bool("admin", admin);
		std::string err;
		const bool local_disk_allowed = local_disk_access_allowed(upload_dir,
			admin, err);
		root.add_bool("local_disk_allowed", err.empty() && local_disk_allowed);
		(void) append_authenticated_user_prefs(root, upload_dir, username);
	}
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AuthRegisterAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	const std::string username = req.getParameter("username")
		? req.getParameter("username") : "";
	const std::string password = req.getParameter("password")
		? req.getParameter("password") : "";
	std::string err;
	if (!valid_username(username, err) || !valid_password(password, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return false;
	}

	std::lock_guard<std::mutex> guard(g_auth_mutex);
	std::vector<user_record_t> users;
	if (!load_users_unlocked(upload_dir, users, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return false;
	}
	if (!users.empty()) {
		json_error(res, 409, "administrator already exists", req.isKeepAlive());
		return false;
	}
	user_record_t user;
	user.username = username;
	user.salt = random_hex(16);
	user.password_hash = password_hash(user.salt, password);
	user.admin = true;
	users.push_back(user);
	if (!save_users_unlocked(upload_dir, users, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return false;
	}
	sync_users_db_backup(upload_dir);
	const std::string token = create_session_token(user);
	set_auth_cookie(res, token);

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("username", user.username.c_str());
	root.add_bool("admin", true);
	root.add_bool("local_disk_allowed", true);
	root.add_text("auth_token", token.c_str());
	(void) append_authenticated_user_prefs(root, upload_dir, user.username);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AuthLoginAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	const std::string username = req.getParameter("username")
		? req.getParameter("username") : "";
	const std::string password = req.getParameter("password")
		? req.getParameter("password") : "";

	std::lock_guard<std::mutex> guard(g_auth_mutex);
	std::vector<user_record_t> users;
	std::string err;
	if (!load_users_unlocked(upload_dir, users, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return false;
	}
	for (auto & user : users) {
		if (user.username == username
			&& user.password_hash == password_hash(user.salt, password))
		{
			const std::string token = create_session_token(user);
			set_auth_cookie(res, token);
			acl::json json;
			acl::json_node& root = json.create_node();
			root.add_bool("ok", true);
			root.add_text("username", user.username.c_str());
			root.add_bool("admin", user.admin);
			std::string settings_err;
			const bool local_disk_allowed = local_disk_access_allowed(upload_dir,
				user.admin, settings_err);
			root.add_bool("local_disk_allowed",
				settings_err.empty() && local_disk_allowed);
			root.add_text("auth_token", token.c_str());
			(void) append_authenticated_user_prefs(root, upload_dir, user.username);
			return sendJson(res, 200, root, req.isKeepAlive());
		}
	}
	json_error(res, 403, "invalid username or password", req.isKeepAlive());
	return false;
}

bool AuthLogoutAction::run(const request_t& req, response_t& res)
{
	(void) req;
	clear_auth_cookie(res);
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AuthPasswordAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string username;
	bool admin = false;
	if (!auth_request_allowed(req, upload_dir, username, admin)) {
		return auth_send_required(req, res);
	}
	const std::string old_password = req.getParameter("old_password")
		? req.getParameter("old_password") : "";
	const std::string new_password = req.getParameter("new_password")
		? req.getParameter("new_password") : "";
	std::string err;
	if (!valid_password(new_password, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return false;
	}

	std::lock_guard<std::mutex> guard(g_auth_mutex);
	std::vector<user_record_t> users;
	if (!load_users_unlocked(upload_dir, users, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return false;
	}
	user_record_t* user = find_user(users, username);
	if (user == nullptr) {
		json_error(res, 404, "user not found", req.isKeepAlive());
		return false;
	}
	if (user->password_hash != password_hash(user->salt, old_password)) {
		json_error(res, 403, "old password is incorrect", req.isKeepAlive());
		return false;
	}
	user->salt = random_hex(16);
	user->password_hash = password_hash(user->salt, new_password);
	if (!save_users_unlocked(upload_dir, users, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return false;
	}
	sync_users_db_backup(upload_dir);
	const std::string token = create_session_token(*user);
	set_auth_cookie(res, token);

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("username", user->username.c_str());
	root.add_bool("admin", user->admin);
	std::string settings_err;
	const bool local_disk_allowed = local_disk_access_allowed(upload_dir,
		user->admin, settings_err);
	root.add_bool("local_disk_allowed", settings_err.empty() && local_disk_allowed);
	root.add_text("auth_token", token.c_str());
	(void) append_authenticated_user_prefs(root, upload_dir, user->username);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AuthUsersAction::run(const request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string current;
	if (!require_admin(req, res, upload_dir, current)) {
		return false;
	}

	std::lock_guard<std::mutex> guard(g_auth_mutex);
	std::vector<user_record_t> users;
	std::string err;
	if (!load_users_unlocked(upload_dir, users, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return false;
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	acl::json_node& arr = json.create_array();
	for (const auto & user : users) {
		add_user_json(arr, user);
	}
	root.add_child("users", arr);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AuthUserCreateAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string current;
	if (!require_admin(req, res, upload_dir, current)) {
		return false;
	}
	const std::string username = req.getParameter("username")
		? req.getParameter("username") : "";
	const std::string password = req.getParameter("password")
		? req.getParameter("password") : "";
	std::string err;
	if (!valid_username(username, err) || !valid_password(password, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return false;
	}

	std::lock_guard<std::mutex> guard(g_auth_mutex);
	std::vector<user_record_t> users;
	if (!load_users_unlocked(upload_dir, users, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return false;
	}
	for (auto & user : users) {
		if (user.username == username) {
			json_error(res, 409, "username already exists", req.isKeepAlive());
			return false;
		}
	}
	user_record_t user;
	user.username = username;
	user.salt = random_hex(16);
	user.password_hash = password_hash(user.salt, password);
	user.admin = false;
	users.push_back(user);
	if (!save_users_unlocked(upload_dir, users, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return false;
	}
	sync_users_db_backup(upload_dir);

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("username", user.username.c_str());
	root.add_bool("admin", false);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AuthUserUpdateAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string current;
	if (!require_admin(req, res, upload_dir, current)) {
		return false;
	}
	const std::string username = req.getParameter("username")
		? req.getParameter("username") : "";
	const std::string new_username = req.getParameter("new_username")
		? req.getParameter("new_username") : username;
	const std::string password = req.getParameter("password")
		? req.getParameter("password") : "";

	std::string err;
	if (!valid_username(username, err) || !valid_username(new_username, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return false;
	}
	if (!password.empty() && !valid_password(password, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return false;
	}

	std::lock_guard<std::mutex> guard(g_auth_mutex);
	std::vector<user_record_t> users;
	if (!load_users_unlocked(upload_dir, users, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return false;
	}
	user_record_t* target = find_user(users, username);
	if (target == nullptr) {
		json_error(res, 404, "user not found", req.isKeepAlive());
		return false;
	}
	if (target->admin) {
		json_error(res, 403, "administrator cannot be modified here", req.isKeepAlive());
		return false;
	}
	if (new_username != username && find_user(users, new_username) != nullptr) {
		json_error(res, 409, "username already exists", req.isKeepAlive());
		return false;
	}
	const std::string old_username = target->username;
	target->username = new_username;
	if (!password.empty()) {
		target->salt = random_hex(16);
		target->password_hash = password_hash(target->salt, password);
	}
	if (!save_users_unlocked(upload_dir, users, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return false;
	}
	if (old_username != new_username) {
		std::string prefs_err;
		if (!rename_user_prefs(upload_dir, old_username, new_username, prefs_err)) {
			json_error(res, 500, prefs_err.c_str(), req.isKeepAlive());
			return false;
		}
	}
	sync_users_db_backup(upload_dir);

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("username", target->username.c_str());
	root.add_bool("admin", false);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool AuthUserDeleteAction::run(const request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string current;
	if (!require_admin(req, res, upload_dir, current)) {
		return false;
	}
	const std::string username = req.getParameter("username")
		? req.getParameter("username") : "";
	std::string err;
	if (!valid_username(username, err)) {
		json_error(res, 400, err.c_str(), req.isKeepAlive());
		return false;
	}

	std::lock_guard<std::mutex> guard(g_auth_mutex);
	std::vector<user_record_t> users;
	if (!load_users_unlocked(upload_dir, users, err)) {
		json_error(res, 500, err.c_str(), req.isKeepAlive());
		return false;
	}
	for (auto it = users.begin(); it != users.end(); ++it) {
		if (it->username != username) {
			continue;
		}
		if (it->admin) {
			json_error(res, 403, "administrator cannot be deleted", req.isKeepAlive());
			return false;
		}
		users.erase(it);
		if (!save_users_unlocked(upload_dir, users, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return false;
		}
		std::string prefs_err;
		(void) delete_user_prefs(upload_dir, username, prefs_err);
		sync_users_db_backup(upload_dir);
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", true);
		return sendJson(res, 200, root, req.isKeepAlive());
	}
	json_error(res, 404, "user not found", req.isKeepAlive());
	return false;
}

} // namespace action

#include "stdafx.h"
#include "folder_common.h"

#include "common/webcool_mutex.h"

namespace action {

webcool::mutex g_folder_lock_mutex;

static std::string folder_locks_file_path(const std::string& upload_dir) {
	return upload_dir + "/.folder_locks.txt";
}

bool validate_lock_password(const std::string& password,
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

bool load_folder_locks_locked(const std::string& upload_dir,
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

bool save_folder_locks_locked(const std::string& upload_dir,
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

bool find_locked_ancestor_locked(
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

bool rename_folder_locks_prefix(const std::string& upload_dir,
	const std::string& old_prefix, const std::string& new_prefix,
	std::string& err)
{
	err.clear();
	if (old_prefix.empty() || new_prefix.empty() || old_prefix == new_prefix) {
		return true;
	}
	std::lock_guard<webcool::mutex> guard(g_folder_lock_mutex);
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

bool folder_lock_path_allows(const std::string& upload_dir,
	const std::string& relative_path, const std::string& password,
	bool& allowed, std::string& locked_path, std::string& err)
{
	allowed = false;
	locked_path.clear();
	std::lock_guard<webcool::mutex> guard(g_folder_lock_mutex);
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
	std::lock_guard<webcool::mutex> guard(g_folder_lock_mutex);
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

} // namespace action

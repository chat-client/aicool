#include "stdafx.h"
#include "folder_common.h"

#include <cstring>

namespace action {

bool should_skip_self_parent_entry(const char* name) {
	if (name == NULL || *name == '\0') {
		return true;
	}
	return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

bool validate_folder_segment(const std::string& name, std::string& err) {
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

bool remove_path_recursive(const std::string& path, std::string& err)
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

bool copy_path_recursive(const std::string& source,
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

bool is_same_or_child_path(const std::string& base_path,
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

} // namespace action

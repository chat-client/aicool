#include "stdafx.h"
#include "file_common.h"

namespace action {

bool collect_files_recursive(const std::string& upload_dir,
	const std::string& relative_dir, const std::string& folder_password,
	std::vector<file_entry_t>& out, std::string& err, bool show_hidden)
{
	err.clear();
	const std::string full_dir = join_upload_path(upload_dir, relative_dir);
	DIR* dir = opendir(full_dir.c_str());
	if (dir == NULL) {
		err = strerror(errno);
		return false;
	}

	struct dirent* entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (should_skip_entry(entry->d_name, show_hidden)) {
			continue;
		}
		if (is_protected_project_db_file(relative_dir, entry->d_name)) {
			continue;
		}
		const std::string name(entry->d_name);
		const std::string rel_path = relative_dir.empty() ? name : (relative_dir + "/" + name);
		const std::string full_path = join_upload_path(upload_dir, rel_path);
		struct stat st;
		if (stat(full_path.c_str(), &st) != 0) {
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			bool lock_allowed = false;
			std::string locked_path;
			if (!folder_lock_path_allows(upload_dir, rel_path, folder_password,
				lock_allowed, locked_path, err))
			{
				closedir(dir);
				return false;
			}
			if (!lock_allowed) {
				continue;
			}
			if (!collect_files_recursive(upload_dir, rel_path, folder_password, out, err, show_hidden)) {
				closedir(dir);
				return false;
			}
			continue;
		}
		if (!S_ISREG(st.st_mode)) {
			continue;
		}

		char uploaded_time[32];
		uploaded_time[0] = '\0';
		format_upload_time(st.st_mtime, uploaded_time, sizeof(uploaded_time));

		file_entry_t item;
		item.name = name;
		item.path = rel_path;
		item.folder_path = relative_dir;
		item.size = regular_file_size(full_path);
		item.uploaded_at = (long long) st.st_mtime;
		item.uploaded_time = uploaded_time;
		item.directory = false;
		item.locked = false;
		out.push_back(item);
	}
	closedir(dir);
	return true;
}

} // namespace action

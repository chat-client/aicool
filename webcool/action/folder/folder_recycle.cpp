#include "stdafx.h"
#include "folder_common.h"

#include <ctime>
#include "common/webcool_mutex.h"

namespace action {

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

bool alloc_recycle_folder_target(const std::string& upload_dir,
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
	std::lock_guard<webcool::mutex> guard(g_recycle_mutex);
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

} // namespace action

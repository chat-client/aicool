#include "stdafx.h"
#include "folder_common.h"

namespace action {

bool init_category_folder_db(const std::string& upload_dir, std::string& err) {
	err.clear();
	if (!make_dir_recursive(upload_dir.c_str())) {
		err = "cannot access upload dir";
		return false;
	}
	return true;
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

} // namespace action

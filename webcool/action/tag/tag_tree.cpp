#include "stdafx.h"
#include "tag_common.h"

namespace action {

void append_tag_json(const std::string& upload_dir,
	acl::json& json, acl::json_node& arr,
	const TagRow& row,
	const std::map<std::string, std::vector<std::string> >& children_by_parent,
	const std::map<std::string, TagRow>& rows_by_id,
	const std::map<std::string, long long>& direct_file_counts)
{
	acl::json_node& item = arr.add_child(false, true);
	item.add_text("id", row.id.c_str());
	item.add_text("name", row.name.c_str());
	item.add_text("parent_id", row.parent_id.c_str());
	std::map<std::string, long long>::const_iterator count_it =
		direct_file_counts.find(row.id);
	item.add_number("file_count",
		count_it == direct_file_counts.end() ? 0 : count_it->second);
	bool locked = false;
	std::string lock_err;
	if (file_lock_path_has_lock(upload_dir, tag_lock_key(row.id), locked, lock_err))
	{
		item.add_bool("locked", locked);
	}
	acl::json_node& files = json.create_array();
	item.add_child("files", files);
	acl::json_node& children = json.create_array();
	item.add_child("children", children);

	std::map<std::string, std::vector<std::string> >::const_iterator it =
		children_by_parent.find(row.id);
	if (it == children_by_parent.end()) {
		return;
	}

	for (size_t i = 0; i < it->second.size(); ++i) {
		std::map<std::string, TagRow>::const_iterator rit =
			rows_by_id.find(it->second[i]);
		if (rit == rows_by_id.end()) {
			continue;
		}
		append_tag_json(upload_dir, json, children, rit->second,
			children_by_parent, rows_by_id, direct_file_counts);
	}
}

} // namespace action

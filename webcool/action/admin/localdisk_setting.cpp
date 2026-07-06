#include "stdafx.h"
#include "admin_internal.h"

namespace action {
using namespace admin_internal;

bool AdminLocalDiskSettingsAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string err;
	webcool_settings_t settings;
	{
		std::lock_guard<webcool::mutex> guard(g_settings_mutex);
		if (!load_settings_unlocked(upload_dir, settings, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
		if (req.getMethod() == acl::HTTP_METHOD_POST) {
			settings.local_disk_admin = request_bool_param(req,
				"local_disk_admin", settings.local_disk_admin);
			settings.local_disk_user = request_bool_param(req,
				"local_disk_user", settings.local_disk_user);
			if (!save_settings_unlocked(upload_dir, settings, err)) {
				json_error(res, 500, err.c_str(), req.isKeepAlive());
				return true;
			}
		}
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_bool("local_disk_admin", settings.local_disk_admin);
	root.add_bool("local_disk_user", settings.local_disk_user);
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action
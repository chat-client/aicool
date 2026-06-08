#include "stdafx.h"
#include "http_servlet.h"
#include "action/actions.h"
#include "action/action_util.h"
#include <map>
#ifdef _WIN32
#include "platform_compat.h"
#else
#include <unistd.h>
#endif

namespace {

typedef bool (http_servlet::*route_handler)(request_t& req, response_t& res);

static bool is_auth_route(const char* path)
{
	return path != NULL && strncmp(path, "/api/v1/auth/", 13) == 0;
}

static bool is_admin_route(const char* path)
{
	return path != NULL && strncmp(path, "/api/v1/admin/", 14) == 0;
}

static bool is_local_disk_route(const char* path)
{
	return path != NULL && strncmp(path, "/api/v1/local-disk/", 19) == 0;
}

static bool request_bool_param(request_t& req, const char* name)
{
	const char* value = req.getParameter(name);
	return value != NULL && (
		strcmp(value, "1") == 0
		|| strcasecmp(value, "true") == 0
		|| strcasecmp(value, "yes") == 0
		|| strcasecmp(value, "on") == 0);
}

static bool request_needs_local_disk_access(const char* path, request_t& req)
{
	if (is_local_disk_route(path)) {
		return true;
	}
	if (path == NULL || !request_bool_param(req, "local")) {
		return false;
	}
	return strcmp(path, "/api/v1/image/save") == 0
		|| strcmp(path, "/api/v1/files/lock") == 0
		|| strcmp(path, "/api/v1/files/unlock") == 0
		|| strcmp(path, "/api/v1/files/lock/verify") == 0;
}

} // namespace

// ────────────────────────────────────────────────────────────────
// 构造 / 析构
// ────────────────────────────────────────────────────────────────
http_servlet::http_servlet(acl::socket_stream* stream,
	acl::session* session, const char* upload_dir)
: acl::HttpServlet(stream, session)
, upload_dir_(upload_dir)
{
	setLocalCharset("utf8");
	action::runtime_upload_dir_init(upload_dir ? upload_dir : "");
}

http_servlet::~http_servlet() {}

// ────────────────────────────────────────────────────────────────
// doGet：路由分发
// ────────────────────────────────────────────────────────────────
bool http_servlet::doGet(request_t& req, response_t& res) {
	const char* path = req.getPathInfo();
	if (path == NULL || *path == '\0') {
		return action::IndexAction::run(req, res);
	}
	if (strcmp(path, "/") == 0 || strncmp(path, "/webcool/html/", 14) == 0) {
		return action::IndexAction::run(req, res);
	}

	static const std::map<std::string, route_handler> routes = {
		{ "/api/v1/auth/status", &http_servlet::routeAuthStatus },
		{ "/api/v1/auth/users", &http_servlet::routeAuthUsers },
		{ "/api/v1/admin/template/reload", &http_servlet::routeTemplateReload },
		{ "/api/v1/admin/storage", &http_servlet::routeAdminStorageInfo },
		{ "/api/v1/admin/storage/migrate", &http_servlet::routeAdminStorageMigrate },
		{ "/api/v1/admin/storage/migrate/progress", &http_servlet::routeAdminStorageMigrateProgress },
		{ "/api/v1/admin/storage/migrate/resolve", &http_servlet::routeAdminStorageMigrateResolve },
		{ "/api/v1/admin/storage/migrate/control", &http_servlet::routeAdminStorageMigrateControl },
		{ "/api/v1/admin/storage/migrate/cleanup", &http_servlet::routeAdminStorageMigrateCleanup },
		{ "/api/v1/admin/local-disk-settings", &http_servlet::routeAdminLocalDiskSettings },
		{ "/api/v1/delete", &http_servlet::routeDelete },
		{ "/api/v1/restore", &http_servlet::routeRestore },
		{ "/api/v1/files/move", &http_servlet::routeMoveFile },
		{ "/api/v1/files/copy", &http_servlet::routeCopyFile },
		{ "/api/v1/remote-copy/progress", &http_servlet::routeRemoteCopyProgress },
		{ "/api/v1/remote-copy/cancel", &http_servlet::routeRemoteCopyCancel },
		{ "/api/v1/files/rename", &http_servlet::routeRenameFile },
		{ "/api/v1/files", &http_servlet::routeFiles },
		{ "/api/v1/download", &http_servlet::routeDownload },
		{ "/api/v1/open-file", &http_servlet::routeOpenFile },
		{ "/api/v1/image/save", &http_servlet::routeImageSave },
		{ "/api/v1/local-disk/list", &http_servlet::routeLocalDiskList },
		{ "/api/v1/local-disk/download", &http_servlet::routeLocalDiskDownload },
		{ "/api/v1/local-disk/delete", &http_servlet::routeLocalDiskDelete },
		{ "/api/v1/local-disk/mkdir", &http_servlet::routeLocalDiskCreateDir },
		{ "/api/v1/local-disk/move", &http_servlet::routeLocalDiskMove },
		{ "/api/v1/local-disk/copy", &http_servlet::routeLocalDiskCopy },
		{ "/api/v1/local-disk/rename", &http_servlet::routeLocalDiskRename },
		{ "/api/v1/local-disk/open-trash", &http_servlet::routeLocalDiskOpenTrash },
		{ "/api/v1/local-disk/open-file", &http_servlet::routeLocalDiskOpenFile },
		{ "/api/v1/local-disk/import", &http_servlet::routeLocalDiskImport },
		{ "/api/v1/local-disk/import/progress", &http_servlet::routeLocalDiskImportProgress },
		{ "/api/v1/local-disk/video/convert", &http_servlet::routeLocalDiskVideoConvert },
		{ "/api/v1/local-disk/video/stream", &http_servlet::routeLocalDiskVideoStream },
		{ "/api/v1/local-disk/video/stream-state", &http_servlet::routeLocalDiskVideoStreamState },
		{ "/api/v1/video/convert", &http_servlet::routeVideoConvert },
		{ "/api/v1/video/convert/cancel", &http_servlet::routeVideoConvertCancel },
		{ "/api/v1/video/convert/progress", &http_servlet::routeVideoConvertProgress },
		{ "/api/v1/video/convert/tasks", &http_servlet::routeVideoConvertTasks },
		{ "/api/v1/video/probe", &http_servlet::routeVideoProbe },
		{ "/api/v1/video/resume", &http_servlet::routeVideoResumeGet },
		{ "/api/v1/video/resume/save", &http_servlet::routeVideoResumeSet },
		{ "/api/v1/folders", &http_servlet::routeFolderList },
		{ "/api/v1/folders/create", &http_servlet::routeFolderCreate },
		{ "/api/v1/folders/rename", &http_servlet::routeFolderRename },
		{ "/api/v1/folders/move", &http_servlet::routeFolderMove },
		{ "/api/v1/folders/copy", &http_servlet::routeFolderCopy },
		{ "/api/v1/folders/delete", &http_servlet::routeFolderDelete },
		{ "/api/v1/folders/lock", &http_servlet::routeFolderLock },
		{ "/api/v1/folders/unlock", &http_servlet::routeFolderUnlock },
		{ "/api/v1/folders/lock/verify", &http_servlet::routeFolderLockVerify },
		{ "/api/v1/files/lock", &http_servlet::routeFileLock },
		{ "/api/v1/files/unlock", &http_servlet::routeFileUnlock },
		{ "/api/v1/files/lock/verify", &http_servlet::routeFileLockVerify },
		{ "/api/v1/tags", &http_servlet::routeTagList },
		{ "/api/v1/tags/rename", &http_servlet::routeTagRename },
		{ "/api/v1/tags/lock", &http_servlet::routeTagLock },
		{ "/api/v1/tags/unlock", &http_servlet::routeTagUnlock },
		{ "/api/v1/tags/lock/verify", &http_servlet::routeTagLockVerify },
		{ "/api/v1/tag-files", &http_servlet::routeTagFiles },
		{ "/api/v1/upload", &http_servlet::routeUpload },
	};
	std::map<std::string, route_handler>::const_iterator it = routes.find(path);
	if (it != routes.end()) {
		if (!is_auth_route(path)) {
			std::string username;
			bool admin = false;
			if (!action::auth_request_allowed(req, action::runtime_upload_dir_get(),
				username, admin))
			{
				return action::auth_send_required(req, res);
			}
			if (is_admin_route(path) && !admin) {
				acl::json json;
				acl::json_node& root = json.create_node();
				root.add_bool("ok", false);
				root.add_text("error", "admin permission required");
				return action::sendJson(res, 403, root, req.isKeepAlive());
			}
			if (request_needs_local_disk_access(path, req)) {
				std::string err;
				if (!action::local_disk_access_allowed(action::runtime_upload_dir_get(),
					admin, err))
				{
					acl::json json;
					acl::json_node& root = json.create_node();
					root.add_bool("ok", false);
					root.add_text("error", err.empty()
						? "local disk access denied" : err.c_str());
					return action::sendJson(res, err.empty() ? 403 : 500,
						root, req.isKeepAlive());
				}
			}
		}
		return (this->*(it->second))(req, res);
	}
	return action::IndexAction::run(req, res);
}

bool http_servlet::routeAuthStatus(request_t& req, response_t& res) {
	return action::AuthStatusAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeAuthRegister(request_t& req, response_t& res) {
	return action::AuthRegisterAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeAuthLogin(request_t& req, response_t& res) {
	return action::AuthLoginAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeAuthLogout(request_t& req, response_t& res) {
	return action::AuthLogoutAction::run(req, res);
}

bool http_servlet::routeAuthPassword(request_t& req, response_t& res) {
	return action::AuthPasswordAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeAuthUsers(request_t& req, response_t& res) {
	return action::AuthUsersAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeAuthUserCreate(request_t& req, response_t& res) {
	return action::AuthUserCreateAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeAuthUserUpdate(request_t& req, response_t& res) {
	return action::AuthUserUpdateAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeAuthUserDelete(request_t& req, response_t& res) {
	return action::AuthUserDeleteAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeTemplateReload(request_t& req, response_t& res) {
	return action::TemplateReloadAction::run(req, res);
}

bool http_servlet::userUploadDir(request_t& req, response_t& res,
	std::string& dir)
{
	std::string err;
	if (action::authenticated_user_upload_dir(req, action::runtime_upload_dir_get(),
		dir, err))
	{
		return true;
	}
	if (err == "authentication required") {
		return action::auth_send_required(req, res);
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", err.empty() ? "cannot access user upload directory" : err.c_str());
	action::sendJson(res, 500, root, req.isKeepAlive());
	return false;
}

bool http_servlet::routeAdminStorageInfo(request_t& req, response_t& res) {
	return action::AdminStorageInfoAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeAdminStorageMigrate(request_t& req, response_t& res) {
	return action::AdminStorageMigrateAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeAdminStorageMigrateProgress(request_t& req, response_t& res) {
	return action::AdminStorageMigrateProgressAction::run(req, res);
}

bool http_servlet::routeAdminStorageMigrateResolve(request_t& req, response_t& res) {
	return action::AdminStorageMigrateResolveAction::run(req, res);
}

bool http_servlet::routeAdminStorageMigrateControl(request_t& req, response_t& res) {
	return action::AdminStorageMigrateControlAction::run(req, res);
}

bool http_servlet::routeAdminStorageMigrateCleanup(request_t& req, response_t& res) {
	return action::AdminStorageMigrateCleanupAction::run(req, res);
}

bool http_servlet::routeAdminLocalDiskSettings(request_t& req, response_t& res) {
	return action::AdminLocalDiskSettingsAction::run(req, res,
		action::runtime_upload_dir_get());
}

bool http_servlet::routeDelete(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::DeleteAction::run(req, res, dir);
}

bool http_servlet::routeRestore(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::RestoreAction::run(req, res, dir);
}

bool http_servlet::routeMoveFile(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::MoveFileAction::run(req, res, dir);
}

bool http_servlet::routeCopyFile(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::CopyFileAction::run(req, res, dir);
}

bool http_servlet::routeRemoteCopyProgress(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::RemoteCopyProgressAction::run(req, res, dir);
}

bool http_servlet::routeRemoteCopyCancel(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::RemoteCopyCancelAction::run(req, res, dir);
}

bool http_servlet::routeRenameFile(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::RenameFileAction::run(req, res, dir);
}

bool http_servlet::routeFiles(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FilesAction::run(req, res, dir);
}

bool http_servlet::routeDownload(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::DownloadAction::run(req, res, dir);
}

bool http_servlet::routeImageSave(request_t& req, response_t& res) {
	if (request_bool_param(req, "local")) {
		return action::ImageSaveAction::run(req, res, action::runtime_upload_dir_get());
	}
	std::string dir;
	return userUploadDir(req, res, dir) && action::ImageSaveAction::run(req, res, dir);
}

bool http_servlet::routeOpenFile(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::OpenFileAction::run(req, res, dir);
}

bool http_servlet::routeLocalDiskList(request_t& req, response_t& res) {
	return action::LocalDiskListAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeLocalDiskDownload(request_t& req, response_t& res) {
	return action::LocalDiskDownloadAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeLocalDiskDelete(request_t& req, response_t& res) {
	return action::LocalDiskDeleteAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeLocalDiskCreateDir(request_t& req, response_t& res) {
	return action::LocalDiskCreateDirAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeLocalDiskMove(request_t& req, response_t& res) {
	return action::LocalDiskMoveAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeLocalDiskCopy(request_t& req, response_t& res) {
	return action::LocalDiskCopyAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeLocalDiskRename(request_t& req, response_t& res) {
	return action::LocalDiskRenameAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeLocalDiskOpenTrash(request_t& req, response_t& res) {
	return action::LocalDiskOpenTrashAction::run(req, res);
}

bool http_servlet::routeLocalDiskOpenFile(request_t& req, response_t& res) {
	return action::LocalDiskOpenFileAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeLocalDiskImport(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::LocalDiskImportAction::run(req, res, dir);
}

bool http_servlet::routeLocalDiskImportProgress(request_t& req, response_t& res) {
	return action::LocalDiskImportProgressAction::run(req, res);
}

bool http_servlet::routeLocalDiskVideoConvert(request_t& req, response_t& res) {
	return action::LocalDiskVideoConvertAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeLocalDiskVideoStream(request_t& req, response_t& res) {
	return action::LocalDiskVideoStreamAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeLocalDiskVideoStreamState(request_t& req, response_t& res) {
	return action::LocalDiskVideoStreamStateAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_servlet::routeUpload(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::UploadAction::run(req, res, dir);
}

bool http_servlet::routeVideoConvert(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::VideoConvertAction::run(req, res, dir);
}

bool http_servlet::routeVideoConvertProgress(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::VideoConvertProgressAction::run(req, res, dir);
}

bool http_servlet::routeVideoConvertTasks(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::VideoConvertTasksAction::run(req, res, dir);
}

bool http_servlet::routeVideoConvertCancel(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::VideoConvertCancelAction::run(req, res, dir);
}

bool http_servlet::routeVideoProbe(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::VideoProbeAction::run(req, res, dir);
}

bool http_servlet::routeVideoResumeGet(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::VideoResumeGetAction::run(req, res, dir);
}

bool http_servlet::routeVideoResumeSet(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::VideoResumeSetAction::run(req, res, dir);
}

bool http_servlet::routeFolderList(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderListAction::run(req, res, dir);
}

bool http_servlet::routeFolderCreate(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderCreateAction::run(req, res, dir);
}

bool http_servlet::routeFolderRename(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderRenameAction::run(req, res, dir);
}

bool http_servlet::routeFolderMove(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderMoveAction::run(req, res, dir);
}

bool http_servlet::routeFolderCopy(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderCopyAction::run(req, res, dir);
}

bool http_servlet::routeFolderDelete(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderDeleteAction::run(req, res, dir);
}

bool http_servlet::routeFolderLock(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderLockAction::run(req, res, dir);
}

bool http_servlet::routeFolderUnlock(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderUnlockAction::run(req, res, dir);
}

bool http_servlet::routeFolderLockVerify(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderLockVerifyAction::run(req, res, dir);
}

bool http_servlet::routeFileLock(request_t& req, response_t& res) {
	if (request_bool_param(req, "local")) {
		return action::FileLockAction::run(req, res, action::runtime_upload_dir_get());
	}
	std::string dir;
	return userUploadDir(req, res, dir) && action::FileLockAction::run(req, res, dir);
}

bool http_servlet::routeFileUnlock(request_t& req, response_t& res) {
	if (request_bool_param(req, "local")) {
		return action::FileUnlockAction::run(req, res, action::runtime_upload_dir_get());
	}
	std::string dir;
	return userUploadDir(req, res, dir) && action::FileUnlockAction::run(req, res, dir);
}

bool http_servlet::routeFileLockVerify(request_t& req, response_t& res) {
	if (request_bool_param(req, "local")) {
		return action::FileLockVerifyAction::run(req, res, action::runtime_upload_dir_get());
	}
	std::string dir;
	return userUploadDir(req, res, dir) && action::FileLockVerifyAction::run(req, res, dir);
}

bool http_servlet::routeTagList(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagListAction::run(req, res, dir);
}

bool http_servlet::routeTagCreate(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagCreateAction::run(req, res, dir);
}

bool http_servlet::routeTagRename(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagRenameAction::run(req, res, dir);
}

bool http_servlet::routeTagDelete(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagDeleteAction::run(req, res, dir);
}

bool http_servlet::routeTagBind(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagBindAction::run(req, res, dir);
}

bool http_servlet::routeTagUnbind(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagUnbindAction::run(req, res, dir);
}

bool http_servlet::routeTagLock(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagLockAction::run(req, res, dir);
}

bool http_servlet::routeTagUnlock(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagUnlockAction::run(req, res, dir);
}

bool http_servlet::routeTagLockVerify(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagLockVerifyAction::run(req, res, dir);
}

bool http_servlet::routeTagFiles(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagFilesAction::run(req, res, dir);
}

// ────────────────────────────────────────────────────────────────
// doPost
// ────────────────────────────────────────────────────────────────
bool http_servlet::doPost(request_t& req, response_t& res) {
	const char* path = req.getPathInfo();
	static const std::map<std::string, route_handler> routes = {
		{ "/api/v1/auth/register", &http_servlet::routeAuthRegister },
		{ "/api/v1/auth/login", &http_servlet::routeAuthLogin },
		{ "/api/v1/auth/logout", &http_servlet::routeAuthLogout },
		{ "/api/v1/auth/password", &http_servlet::routeAuthPassword },
		{ "/api/v1/auth/users/create", &http_servlet::routeAuthUserCreate },
		{ "/api/v1/auth/users/update", &http_servlet::routeAuthUserUpdate },
		{ "/api/v1/auth/users/delete", &http_servlet::routeAuthUserDelete },
		{ "/api/v1/upload", &http_servlet::routeUpload },
		{ "/api/v1/admin/storage/migrate", &http_servlet::routeAdminStorageMigrate },
		{ "/api/v1/admin/storage/migrate/resolve", &http_servlet::routeAdminStorageMigrateResolve },
		{ "/api/v1/admin/storage/migrate/control", &http_servlet::routeAdminStorageMigrateControl },
		{ "/api/v1/admin/storage/migrate/cleanup", &http_servlet::routeAdminStorageMigrateCleanup },
		{ "/api/v1/admin/local-disk-settings", &http_servlet::routeAdminLocalDiskSettings },
		{ "/api/v1/image/save", &http_servlet::routeImageSave },
		{ "/api/v1/restore", &http_servlet::routeRestore },
		{ "/api/v1/files/move", &http_servlet::routeMoveFile },
		{ "/api/v1/files/copy", &http_servlet::routeCopyFile },
		{ "/api/v1/remote-copy/progress", &http_servlet::routeRemoteCopyProgress },
		{ "/api/v1/remote-copy/cancel", &http_servlet::routeRemoteCopyCancel },
		{ "/api/v1/files/rename", &http_servlet::routeRenameFile },
		{ "/api/v1/video/convert", &http_servlet::routeVideoConvert },
		{ "/api/v1/video/convert/cancel", &http_servlet::routeVideoConvertCancel },
		{ "/api/v1/video/convert/progress", &http_servlet::routeVideoConvertProgress },
		{ "/api/v1/video/convert/tasks", &http_servlet::routeVideoConvertTasks },
		{ "/api/v1/video/probe", &http_servlet::routeVideoProbe },
		{ "/api/v1/video/resume/save", &http_servlet::routeVideoResumeSet },
		{ "/api/v1/folders/create", &http_servlet::routeFolderCreate },
		{ "/api/v1/folders/rename", &http_servlet::routeFolderRename },
		{ "/api/v1/folders/move", &http_servlet::routeFolderMove },
		{ "/api/v1/folders/copy", &http_servlet::routeFolderCopy },
		{ "/api/v1/folders/delete", &http_servlet::routeFolderDelete },
		{ "/api/v1/folders/lock", &http_servlet::routeFolderLock },
		{ "/api/v1/folders/unlock", &http_servlet::routeFolderUnlock },
		{ "/api/v1/folders/lock/verify", &http_servlet::routeFolderLockVerify },
		{ "/api/v1/files/lock", &http_servlet::routeFileLock },
		{ "/api/v1/files/unlock", &http_servlet::routeFileUnlock },
		{ "/api/v1/files/lock/verify", &http_servlet::routeFileLockVerify },
		{ "/api/v1/local-disk/delete", &http_servlet::routeLocalDiskDelete },
		{ "/api/v1/local-disk/mkdir", &http_servlet::routeLocalDiskCreateDir },
		{ "/api/v1/local-disk/move", &http_servlet::routeLocalDiskMove },
		{ "/api/v1/local-disk/copy", &http_servlet::routeLocalDiskCopy },
		{ "/api/v1/local-disk/rename", &http_servlet::routeLocalDiskRename },
		{ "/api/v1/local-disk/open-trash", &http_servlet::routeLocalDiskOpenTrash },
		{ "/api/v1/local-disk/open-file", &http_servlet::routeLocalDiskOpenFile },
		{ "/api/v1/open-file", &http_servlet::routeOpenFile },
		{ "/api/v1/local-disk/import", &http_servlet::routeLocalDiskImport },
		{ "/api/v1/local-disk/import/progress", &http_servlet::routeLocalDiskImportProgress },
		{ "/api/v1/local-disk/video/convert", &http_servlet::routeLocalDiskVideoConvert },
		{ "/api/v1/local-disk/video/stream-state", &http_servlet::routeLocalDiskVideoStreamState },
		{ "/api/v1/tags/create", &http_servlet::routeTagCreate },
		{ "/api/v1/tags/rename", &http_servlet::routeTagRename },
		{ "/api/v1/tags/delete", &http_servlet::routeTagDelete },
		{ "/api/v1/tags/bind", &http_servlet::routeTagBind },
		{ "/api/v1/tags/unbind", &http_servlet::routeTagUnbind },
		{ "/api/v1/tags/lock", &http_servlet::routeTagLock },
		{ "/api/v1/tags/unlock", &http_servlet::routeTagUnlock },
		{ "/api/v1/tags/lock/verify", &http_servlet::routeTagLockVerify },
	};
	if (path != NULL) {
		std::map<std::string, route_handler>::const_iterator it = routes.find(path);
		if (it != routes.end()) {
			if (!is_auth_route(path)) {
				std::string username;
				bool admin = false;
				if (!action::auth_request_allowed(req, action::runtime_upload_dir_get(),
					username, admin))
				{
					return action::auth_send_required(req, res);
				}
				if (is_admin_route(path) && !admin) {
					acl::json json;
					acl::json_node& root = json.create_node();
					root.add_bool("ok", false);
					root.add_text("error", "admin permission required");
					return action::sendJson(res, 403, root, req.isKeepAlive());
				}
				if (request_needs_local_disk_access(path, req)) {
					std::string err;
					if (!action::local_disk_access_allowed(action::runtime_upload_dir_get(),
						admin, err))
					{
						acl::json json;
						acl::json_node& root = json.create_node();
						root.add_bool("ok", false);
						root.add_text("error", err.empty()
							? "local disk access denied" : err.c_str());
						return action::sendJson(res, err.empty() ? 403 : 500,
							root, req.isKeepAlive());
					}
				}
			}
			return (this->*(it->second))(req, res);
		}
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", "unsupported api path");
	return action::sendJson(res, 404, root, req.isKeepAlive());
}

// ────────────────────────────────────────────────────────────────
// doError
// ────────────────────────────────────────────────────────────────
bool http_servlet::doError(request_t&, response_t& res) {
	res.setStatus(400);
	res.setContentType("text/plain; charset=utf-8");
	const char* msg = "400 Bad Request\r\n";
	res.setContentLength(strlen(msg));
	res.write(msg, strlen(msg));
	res.write(NULL, 0);
	return false;
}

#include "stdafx.h"
#include "http_router.h"
#include "action/actions.h"
#include "action/action_util.h"
#include "http_service.h"
#include <map>
#ifdef _WIN32
#include "platform_compat.h"
#endif

namespace {

//typedef bool (http_router::*route_handler)(request_t& req, response_t& res);

bool is_root_route(const char *path) {
	return path != nullptr && strncmp(path, "/", 1) == 0;
}

bool is_static_route(const char *path) {
	return path != nullptr && strncmp(path, "/webcool/html/", 11) == 0;
}

bool is_auth_route(const char* path) {
	return path != nullptr && strncmp(path, "/api/v1/auth/", 13) == 0;
}

bool is_admin_route(const char* path) {
	return path != nullptr && strncmp(path, "/api/v1/admin/", 14) == 0;
}

bool is_local_disk_route(const char* path) {
	return path != nullptr && strncmp(path, "/api/v1/local-disk/", 19) == 0;
}

bool request_bool_param(request_t& req, const char* name) {
	const char* value = req.getParameter(name);
	return value != nullptr && (
		strcmp(value, "1") == 0
		|| strcasecmp(value, "true") == 0
		|| strcasecmp(value, "yes") == 0
		|| strcasecmp(value, "on") == 0);
}

bool request_needs_local_disk_access(const char* path, request_t& req) {
	if (is_local_disk_route(path)) {
		return true;
	}
	if (path == nullptr || !request_bool_param(req, "local")) {
		return false;
	}
	return strcmp(path, "/api/v1/image/save") == 0
		|| strcmp(path, "/api/v1/files/lock") == 0
		|| strcmp(path, "/api/v1/files/unlock") == 0
		|| strcmp(path, "/api/v1/files/lock/verify") == 0;
}

void add_unique_path(std::vector<std::string>& paths, const std::string& path) {
	for (const auto & i : paths) {
		if (i == path) {
			return;
		}
	}
	paths.push_back(path);
}

bool add_normalized_path(const request_t& req, const char* name,
	std::vector<std::string>& paths, bool allow_empty = false) {
	std::string value;
	std::string err;
	if (!action::normalize_relative_path(req.getParameter(name), value, err, allow_empty)) {
		return false;
	}
	if (!value.empty() || allow_empty) {
		add_unique_path(paths, value);
	}
	return true;
}

void add_sqlite_sidecar_paths(std::vector<std::string>& paths, const char* db_name) {
	add_unique_path(paths, db_name);
	add_unique_path(paths, std::string(db_name) + "-wal");
	add_unique_path(paths, std::string(db_name) + "-shm");
	add_unique_path(paths, std::string(db_name) + "-journal");
}

void add_tag_metadata_paths(std::vector<std::string>& paths) {
	add_sqlite_sidecar_paths(paths, ".tag_catalog.db");
}

void add_recycle_metadata_paths(std::vector<std::string>& paths) {
	add_sqlite_sidecar_paths(paths, ".recycle_bin.db");
}

void add_resume_metadata_paths(std::vector<std::string>& paths) {
	add_sqlite_sidecar_paths(paths, ".video_resume.db");
}

void add_lock_metadata_paths(std::vector<std::string>& paths) {
	add_unique_path(paths, ".file_locks.txt");
	add_unique_path(paths, ".folder_locks.txt");
}

void add_all_file_metadata_paths(std::vector<std::string>& paths) {
	add_tag_metadata_paths(paths);
	add_recycle_metadata_paths(paths);
	add_resume_metadata_paths(paths);
	add_lock_metadata_paths(paths);
}

std::string request_target_path_from_file_and_folder(request_t& req) {
	std::string file_path;
	std::string folder_path;
	std::string err;
	if (!action::normalize_relative_path(req.getParameter("file"), file_path, err, false)
		|| !action::normalize_relative_path(req.getParameter("folder"), folder_path, err, true))
	{
		return "";
	}
	const std::string name = action::base_name_from_relative_path(file_path);
	return folder_path.empty() ? name : (folder_path + "/" + name);
}

std::string request_target_path_from_path_and_folder(request_t& req) {
	std::string source_path;
	std::string folder_path;
	std::string err;
	if (!action::normalize_relative_path(req.getParameter("path"), source_path, err, false)
		|| !action::normalize_relative_path(req.getParameter("folder"), folder_path, err, true))
	{
		return "";
	}
	const std::string name = action::base_name_from_relative_path(source_path);
	return folder_path.empty() ? name : (folder_path + "/" + name);
}

bool request_storage_backup_events(const char* path, request_t& req,
	std::vector<std::string>& sync_paths, std::vector<std::string>& delete_paths,
	std::vector<std::string>& move_from_paths,
	std::vector<std::string>& move_to_paths,
	bool& full_sync) {

	(void) delete_paths;
	full_sync = false;
	if (path == nullptr) {
		return false;
	}
	if (strcmp(path, "/api/v1/upload") == 0) {
		return false;
	}
	if (strcmp(path, "/api/v1/image/save") == 0) {
		if (request_bool_param(req, "local")) {
			return false;
		}
		return add_normalized_path(req, "file", sync_paths, false);
	}
	if (strcmp(path, "/api/v1/video/resume/save") == 0) {
		add_resume_metadata_paths(sync_paths);
		return true;
	}
	if (strncmp(path, "/api/v1/tags/", 13) == 0) {
		if (strcmp(path, "/api/v1/tags/lock") == 0
			|| strcmp(path, "/api/v1/tags/unlock") == 0
			|| strcmp(path, "/api/v1/tags/lock/verify") == 0)
		{
			return false;
		}
		add_tag_metadata_paths(sync_paths);
		return true;
	}
	if (strcmp(path, "/api/v1/files/copy") == 0) {
		if (request_bool_param(req, "async")) {
			return false;
		}
		const std::string target = request_target_path_from_file_and_folder(req);
		if (target.empty()) {
			return false;
		}
		add_unique_path(sync_paths, target);
		return true;
	}
	if (strcmp(path, "/api/v1/files/move") == 0) {
		std::string file_path;
		std::string err;
		if (!action::normalize_relative_path(req.getParameter("file"), file_path, err, false)) {
			return false;
		}
		const std::string target = request_target_path_from_file_and_folder(req);
		if (!target.empty()) {
			move_from_paths.push_back(file_path);
			move_to_paths.push_back(target);
		}
		add_all_file_metadata_paths(sync_paths);
		return true;
	}
	if (strcmp(path, "/api/v1/files/rename") == 0) {
		std::string file_path;
		std::string new_name;
		std::string err;
		if (!action::normalize_relative_path(req.getParameter("file"), file_path, err, false)
			|| !action::normalize_relative_path(req.getParameter("name"), new_name, err, false))
		{
			return false;
		}
		const std::string parent = action::parent_relative_path(file_path);
		move_from_paths.push_back(file_path);
		move_to_paths.push_back(parent.empty() ? new_name : (parent + "/" + new_name));
		add_all_file_metadata_paths(sync_paths);
		return true;
	}
	if (strcmp(path, "/api/v1/folders/create") == 0) {
		std::string parent;
		std::string name;
		std::string err;
		if (!action::normalize_relative_path(req.getParameter("parent"), parent, err, true)
			|| !action::normalize_relative_path(req.getParameter("name"), name, err, false))
		{
			return false;
		}
		add_unique_path(sync_paths, parent.empty() ? name : (parent + "/" + name));
		return true;
	}
	if (strcmp(path, "/api/v1/folders/copy") == 0) {
		if (request_bool_param(req, "async")) {
			return false;
		}
		const std::string target = request_target_path_from_path_and_folder(req);
		if (target.empty()) {
			return false;
		}
		add_unique_path(sync_paths, target);
		return true;
	}
	if (strcmp(path, "/api/v1/folders/move") == 0) {
		std::string source_path;
		std::string err;
		if (!action::normalize_relative_path(req.getParameter("path"), source_path, err, false)) {
			return false;
		}
		const std::string target = request_target_path_from_path_and_folder(req);
		if (!target.empty()) {
			move_from_paths.push_back(source_path);
			move_to_paths.push_back(target);
		}
		add_all_file_metadata_paths(sync_paths);
		return true;
	}
	if (strcmp(path, "/api/v1/folders/rename") == 0) {
		std::string folder_path;
		std::string new_name;
		std::string err;
		if (!action::normalize_relative_path(req.getParameter("path"), folder_path, err, false)
			|| !action::normalize_relative_path(req.getParameter("name"), new_name, err, false))
		{
			return false;
		}
		const std::string parent = action::parent_relative_path(folder_path);
		move_from_paths.push_back(folder_path);
		move_to_paths.push_back(parent.empty() ? new_name : (parent + "/" + new_name));
		add_all_file_metadata_paths(sync_paths);
		return true;
	}
	if (strcmp(path, "/api/v1/files/lock") == 0
		|| strcmp(path, "/api/v1/files/unlock") == 0
		|| strcmp(path, "/api/v1/folders/lock") == 0
		|| strcmp(path, "/api/v1/folders/unlock") == 0)
	{
		add_lock_metadata_paths(sync_paths);
		return true;
	}
	if (strcmp(path, "/api/v1/delete") == 0
		|| strcmp(path, "/api/v1/folders/delete") == 0
		|| strcmp(path, "/api/v1/folders/empty") == 0
		|| strcmp(path, "/api/v1/restore") == 0)
	{
		return false;
	}
	return false;
}

} // namespace

// ────────────────────────────────────────────────────────────────
// 构造 / 析构
// ────────────────────────────────────────────────────────────────
http_router::http_router(http_service& service)
: service_(service)
{
}

void http_router::setup() const {
	service_.Get("/api/v1/auth/status", &http_router::routeAuthStatus)
		.Get("/api/v1/auth/users", &http_router::routeAuthUsers)
		.Get("/api/v1/auth/preferences", &http_router::routeAuthPreferencesGet)
		.Get("/api/v1/admin/template/reload", &http_router::routeTemplateReload)
		.Get("/api/v1/admin/storage", &http_router::routeAdminStorageInfo)
		.Get("/api/v1/admin/storage/migrate", &http_router::routeAdminStorageMigrate)
		.Get("/api/v1/admin/storage/migrate/progress", &http_router::routeAdminStorageMigrateProgress)
		.Get("/api/v1/admin/storage/migrate/resolve", &http_router::routeAdminStorageMigrateResolve)
		.Get("/api/v1/admin/storage/migrate/control", &http_router::routeAdminStorageMigrateControl)
		.Get("/api/v1/admin/storage/migrate/cleanup", &http_router::routeAdminStorageMigrateCleanup)
		.Get("/api/v1/admin/storage/backup/sync/progress", &http_router::routeAdminStorageBackupSyncProgress)
		.Get("/api/v1/admin/local-disk-settings", &http_router::routeAdminLocalDiskSettings)
		.Get("/api/v1/delete", &http_router::routeDelete)
		.Get("/api/v1/restore", &http_router::routeRestore)
		.Get("/api/v1/files/move", &http_router::routeMoveFile)
		.Get("/api/v1/files/copy", &http_router::routeCopyFile)
		.Get("/api/v1/remote-copy/progress", &http_router::routeRemoteCopyProgress)
		.Get("/api/v1/remote-copy/cancel", &http_router::routeRemoteCopyCancel)
		.Get("/api/v1/files/rename", &http_router::routeRenameFile)
		.Get("/api/v1/files", &http_router::routeFiles)
		.Get("/api/v1/download", &http_router::routeDownload)
		.Get("/api/v1/office/preview", &http_router::routeOfficePreview)
		.Get("/api/v1/open-file", &http_router::routeOpenFile)
		.Get("/api/v1/image/save", &http_router::routeImageSave)
		.Get("/api/v1/local-disk/list", &http_router::routeLocalDiskList)
		.Get("/api/v1/local-disk/download", &http_router::routeLocalDiskDownload)
		.Get("/api/v1/local-disk/office/preview", &http_router::routeLocalDiskOfficePreview)
		.Get("/api/v1/local-disk/delete", &http_router::routeLocalDiskDelete)
		.Get("/api/v1/local-disk/mkdir", &http_router::routeLocalDiskCreateDir)
		.Get("/api/v1/local-disk/move", &http_router::routeLocalDiskMove)
		.Get("/api/v1/local-disk/copy", &http_router::routeLocalDiskCopy)
		.Get("/api/v1/local-disk/copy-from-remote", &http_router::routeLocalDiskCopyFromRemote)
		.Get("/api/v1/local-disk/rename", &http_router::routeLocalDiskRename)
		.Get("/api/v1/local-disk/open-trash", &http_router::routeLocalDiskOpenTrash)
		.Get("/api/v1/local-disk/open-file", &http_router::routeLocalDiskOpenFile)
		.Get("/api/v1/local-disk/import", &http_router::routeLocalDiskImport)
		.Get("/api/v1/local-disk/import/progress", &http_router::routeLocalDiskImportProgress)
		.Get("/api/v1/local-disk/import/control", &http_router::routeLocalDiskImportControl)
		.Get("/api/v1/local-disk/video/convert", &http_router::routeLocalDiskVideoConvert)
		.Get("/api/v1/local-disk/video/audio/extract", &http_router::routeLocalDiskAudioExtract)
		.Get("/api/v1/local-disk/video/audio/extract/progress", &http_router::routeLocalDiskAudioExtractProgress)
		.Get("/api/v1/local-disk/video/audio/extract/cancel", &http_router::routeLocalDiskAudioExtractCancel)
		.Get("/api/v1/local-disk/video/properties", &http_router::routeLocalDiskVideoProperties)
		.Get("/api/v1/local-disk/video/enhance", &http_router::routeLocalDiskVideoEnhance)
		.Get("/api/v1/local-disk/video/enhance/progress", &http_router::routeLocalDiskVideoEnhanceProgress)
		.Get("/api/v1/local-disk/video/enhance/cancel", &http_router::routeLocalDiskVideoEnhanceCancel)
		.Get("/api/v1/local-disk/video/edit", &http_router::routeLocalDiskVideoEdit)
		.Get("/api/v1/local-disk/video/edit/progress", &http_router::routeLocalDiskVideoEditProgress)
		.Get("/api/v1/local-disk/video/edit/cancel", &http_router::routeLocalDiskVideoEditCancel)
		.Get("/api/v1/local-disk/video/subtitle/export", &http_router::routeLocalDiskVideoSubtitleExport)
		.Get("/api/v1/local-disk/video/subtitle/export/progress", &http_router::routeLocalDiskVideoSubtitleExportProgress)
		.Get("/api/v1/local-disk/video/subtitle/export/cancel", &http_router::routeLocalDiskVideoSubtitleExportCancel)
		.Get("/api/v1/local-disk/video/keyframes/export", &http_router::routeLocalDiskVideoKeyframeExport)
		.Get("/api/v1/local-disk/video/keyframes/export/progress", &http_router::routeLocalDiskVideoKeyframeExportProgress)
		.Get("/api/v1/local-disk/video/keyframes/export/cancel", &http_router::routeLocalDiskVideoKeyframeExportCancel)
		.Get("/api/v1/local-disk/video/ai-enhance", &http_router::routeLocalDiskAiVideoEnhance)
		.Get("/api/v1/local-disk/video/ai-enhance/progress", &http_router::routeLocalDiskAiVideoEnhanceProgress)
		.Get("/api/v1/local-disk/video/ai-enhance/cancel", &http_router::routeLocalDiskAiVideoEnhanceCancel)
		.Get("/api/v1/local-disk/video/stream", &http_router::routeLocalDiskVideoStream)
		.Get("/api/v1/local-disk/video/stream-state", &http_router::routeLocalDiskVideoStreamState)
		.Get("/api/v1/video/convert", &http_router::routeVideoConvert)
		.Get("/api/v1/video/audio/extract", &http_router::routeAudioExtract)
		.Get("/api/v1/video/audio/extract/progress", &http_router::routeAudioExtractProgress)
		.Get("/api/v1/video/audio/extract/cancel", &http_router::routeAudioExtractCancel)
		.Get("/api/v1/video/convert/cancel", &http_router::routeVideoConvertCancel)
		.Get("/api/v1/video/convert/progress", &http_router::routeVideoConvertProgress)
		.Get("/api/v1/video/convert/tasks", &http_router::routeVideoConvertTasks)
		.Get("/api/v1/video/probe", &http_router::routeVideoProbe)
		.Get("/api/v1/video/properties", &http_router::routeVideoProperties)
		.Get("/api/v1/video/enhance", &http_router::routeVideoEnhance)
		.Get("/api/v1/video/enhance/progress", &http_router::routeVideoEnhanceProgress)
		.Get("/api/v1/video/enhance/cancel", &http_router::routeVideoEnhanceCancel)
		.Get("/api/v1/video/edit", &http_router::routeVideoEdit)
		.Get("/api/v1/video/edit/progress", &http_router::routeVideoEditProgress)
		.Get("/api/v1/video/edit/cancel", &http_router::routeVideoEditCancel)
		.Get("/api/v1/video/subtitle/export", &http_router::routeVideoSubtitleExport)
		.Get("/api/v1/video/subtitle/export/progress", &http_router::routeVideoSubtitleExportProgress)
		.Get("/api/v1/video/subtitle/export/cancel", &http_router::routeVideoSubtitleExportCancel)
		.Get("/api/v1/video/keyframes/export", &http_router::routeVideoKeyframeExport)
		.Get("/api/v1/video/keyframes/export/progress", &http_router::routeVideoKeyframeExportProgress)
		.Get("/api/v1/video/keyframes/export/cancel", &http_router::routeVideoKeyframeExportCancel)
		.Get("/api/v1/video/ai-enhance", &http_router::routeAiVideoEnhance)
		.Get("/api/v1/video/ai-enhance/progress", &http_router::routeAiVideoEnhanceProgress)
		.Get("/api/v1/video/ai-enhance/cancel", &http_router::routeAiVideoEnhanceCancel)
		.Get("/api/v1/video/resume", &http_router::routeVideoResumeGet)
		.Get("/api/v1/video/resume/save", &http_router::routeVideoResumeSet)
		.Get("/api/v1/folders", &http_router::routeFolderList)
		.Get("/api/v1/folders/create", &http_router::routeFolderCreate)
		.Get("/api/v1/folders/rename", &http_router::routeFolderRename)
		.Get("/api/v1/folders/move", &http_router::routeFolderMove)
		.Get("/api/v1/folders/copy", &http_router::routeFolderCopy)
		.Get("/api/v1/folders/delete", &http_router::routeFolderDelete)
		.Get("/api/v1/folders/empty", &http_router::routeFolderEmpty)
		.Get("/api/v1/folders/lock", &http_router::routeFolderLock)
		.Get("/api/v1/folders/unlock", &http_router::routeFolderUnlock)
		.Get("/api/v1/folders/lock/verify", &http_router::routeFolderLockVerify)
		.Get("/api/v1/files/lock", &http_router::routeFileLock)
		.Get("/api/v1/files/unlock", &http_router::routeFileUnlock)
		.Get("/api/v1/files/lock/verify", &http_router::routeFileLockVerify)
		.Get("/api/v1/tags", &http_router::routeTagList)
		.Get("/api/v1/tags/rename", &http_router::routeTagRename)
		.Get("/api/v1/tags/lock", &http_router::routeTagLock)
		.Get("/api/v1/tags/unlock", &http_router::routeTagUnlock)
		.Get("/api/v1/tags/lock/verify", &http_router::routeTagLockVerify)
		.Get("/api/v1/tag-files", &http_router::routeTagFiles)
		.Get("/api/v1/upload", &http_router::routeUpload)
		.Get("/api/v1/upload/stream", &http_router::routeUploadStreamStatus)
		.Get("/", [](request_t& req, response_t& res) {
			return action::IndexAction::run(req, res);
		});

	service_.Post("/", [](request_t& req, response_t& res) {
			return action::IndexAction::run(req, res);
		}).Post("/api/v1/auth/register", &http_router::routeAuthRegister)
		.Post("/api/v1/auth/login", &http_router::routeAuthLogin)
		.Post("/api/v1/auth/logout", &http_router::routeAuthLogout)
		.Post("/api/v1/auth/password", &http_router::routeAuthPassword)
		.Post("/api/v1/auth/preferences", &http_router::routeAuthPreferencesSet)
		.Post("/api/v1/auth/users/create", &http_router::routeAuthUserCreate)
		.Post("/api/v1/auth/users/update", &http_router::routeAuthUserUpdate)
		.Post("/api/v1/auth/users/delete", &http_router::routeAuthUserDelete)
		.Post("/api/v1/upload", &http_router::routeUpload)
		.Post("/api/v1/upload/stream", &http_router::routeUploadStream)
		.Post("/api/v1/admin/storage/migrate", &http_router::routeAdminStorageMigrate)
		.Post("/api/v1/admin/storage/migrate/resolve", &http_router::routeAdminStorageMigrateResolve)
		.Post("/api/v1/admin/storage/migrate/control", &http_router::routeAdminStorageMigrateControl)
		.Post("/api/v1/admin/storage/migrate/cleanup", &http_router::routeAdminStorageMigrateCleanup)
		.Post("/api/v1/admin/storage/backup", &http_router::routeAdminStorageBackup)
		.Post("/api/v1/admin/storage/backup/swap", &http_router::routeAdminStorageSwapBackup)
		.Post("/api/v1/admin/storage/backup/sync", &http_router::routeAdminStorageBackupSync)
		.Post("/api/v1/admin/storage/backup/sync/resolve", &http_router::routeAdminStorageBackupSyncResolve)
		.Post("/api/v1/admin/storage/backup/sync/control", &http_router::routeAdminStorageBackupSyncControl)
		.Post("/api/v1/admin/local-disk-settings", &http_router::routeAdminLocalDiskSettings)
		.Post("/api/v1/image/save", &http_router::routeImageSave)
		.Post("/api/v1/restore", &http_router::routeRestore)
		.Post("/api/v1/files/move", &http_router::routeMoveFile)
		.Post("/api/v1/files/copy", &http_router::routeCopyFile)
		.Post("/api/v1/remote-copy/progress", &http_router::routeRemoteCopyProgress)
		.Post("/api/v1/remote-copy/cancel", &http_router::routeRemoteCopyCancel)
		.Post("/api/v1/files/rename", &http_router::routeRenameFile)
		.Post("/api/v1/video/convert", &http_router::routeVideoConvert)
		.Post("/api/v1/video/audio/extract", &http_router::routeAudioExtract)
		.Post("/api/v1/video/audio/extract/progress", &http_router::routeAudioExtractProgress)
		.Post("/api/v1/video/audio/extract/cancel", &http_router::routeAudioExtractCancel)
		.Post("/api/v1/video/convert/cancel", &http_router::routeVideoConvertCancel)
		.Post("/api/v1/video/convert/progress", &http_router::routeVideoConvertProgress)
		.Post("/api/v1/video/convert/tasks", &http_router::routeVideoConvertTasks)
		.Post("/api/v1/video/probe", &http_router::routeVideoProbe)
		.Post("/api/v1/video/properties", &http_router::routeVideoProperties)
		.Post("/api/v1/video/enhance", &http_router::routeVideoEnhance)
		.Post("/api/v1/video/enhance/progress", &http_router::routeVideoEnhanceProgress)
		.Post("/api/v1/video/enhance/cancel", &http_router::routeVideoEnhanceCancel)
		.Post("/api/v1/video/edit", &http_router::routeVideoEdit)
		.Post("/api/v1/video/edit/progress", &http_router::routeVideoEditProgress)
		.Post("/api/v1/video/edit/cancel", &http_router::routeVideoEditCancel)
		.Post("/api/v1/video/subtitle/export", &http_router::routeVideoSubtitleExport)
		.Post("/api/v1/video/subtitle/export/progress", &http_router::routeVideoSubtitleExportProgress)
		.Post("/api/v1/video/subtitle/export/cancel", &http_router::routeVideoSubtitleExportCancel)
		.Post("/api/v1/video/keyframes/export", &http_router::routeVideoKeyframeExport)
		.Post("/api/v1/video/keyframes/export/progress", &http_router::routeVideoKeyframeExportProgress)
		.Post("/api/v1/video/keyframes/export/cancel", &http_router::routeVideoKeyframeExportCancel)
		.Post("/api/v1/video/ai-enhance", &http_router::routeAiVideoEnhance)
		.Post("/api/v1/video/ai-enhance/progress", &http_router::routeAiVideoEnhanceProgress)
		.Post("/api/v1/video/ai-enhance/cancel", &http_router::routeAiVideoEnhanceCancel)
		.Post("/api/v1/video/resume/save", &http_router::routeVideoResumeSet)
		.Post("/api/v1/folders", &http_router::routeFolderList)
		.Post("/api/v1/folders/create", &http_router::routeFolderCreate)
		.Post("/api/v1/folders/rename", &http_router::routeFolderRename)
		.Post("/api/v1/folders/move", &http_router::routeFolderMove)
		.Post("/api/v1/folders/copy", &http_router::routeFolderCopy)
		.Post("/api/v1/folders/delete", &http_router::routeFolderDelete)
		.Post("/api/v1/folders/empty", &http_router::routeFolderEmpty)
		.Post("/api/v1/folders/lock", &http_router::routeFolderLock)
		.Post("/api/v1/folders/unlock", &http_router::routeFolderUnlock)
		.Post("/api/v1/folders/lock/verify", &http_router::routeFolderLockVerify)
		.Post("/api/v1/files/lock", &http_router::routeFileLock)
		.Post("/api/v1/files/unlock", &http_router::routeFileUnlock)
		.Post("/api/v1/files/lock/verify", &http_router::routeFileLockVerify)
		.Post("/api/v1/local-disk/delete", &http_router::routeLocalDiskDelete)
		.Post("/api/v1/local-disk/mkdir", &http_router::routeLocalDiskCreateDir)
		.Post("/api/v1/local-disk/move", &http_router::routeLocalDiskMove)
		.Post("/api/v1/local-disk/copy", &http_router::routeLocalDiskCopy)
		.Post("/api/v1/local-disk/copy-from-remote", &http_router::routeLocalDiskCopyFromRemote)
		.Post("/api/v1/local-disk/rename", &http_router::routeLocalDiskRename)
		.Post("/api/v1/local-disk/open-trash", &http_router::routeLocalDiskOpenTrash)
		.Post("/api/v1/local-disk/open-file", &http_router::routeLocalDiskOpenFile)
		.Post("/api/v1/open-file", &http_router::routeOpenFile)
		.Post("/api/v1/local-disk/import", &http_router::routeLocalDiskImport)
		.Post("/api/v1/local-disk/import/control", &http_router::routeLocalDiskImportControl)
		.Post("/api/v1/local-disk/import/progress", &http_router::routeLocalDiskImportProgress)
		.Post("/api/v1/local-disk/video/convert", &http_router::routeLocalDiskVideoConvert)
		.Post("/api/v1/local-disk/video/audio/extract", &http_router::routeLocalDiskAudioExtract)
		.Post("/api/v1/local-disk/video/audio/extract/progress", &http_router::routeLocalDiskAudioExtractProgress)
		.Post("/api/v1/local-disk/video/audio/extract/cancel", &http_router::routeLocalDiskAudioExtractCancel)
		.Post("/api/v1/local-disk/video/properties", &http_router::routeLocalDiskVideoProperties)
		.Post("/api/v1/local-disk/video/enhance", &http_router::routeLocalDiskVideoEnhance)
		.Post("/api/v1/local-disk/video/enhance/progress", &http_router::routeLocalDiskVideoEnhanceProgress)
		.Post("/api/v1/local-disk/video/enhance/cancel", &http_router::routeLocalDiskVideoEnhanceCancel)
		.Post("/api/v1/local-disk/video/edit", &http_router::routeLocalDiskVideoEdit)
		.Post("/api/v1/local-disk/video/edit/progress", &http_router::routeLocalDiskVideoEditProgress)
		.Post("/api/v1/local-disk/video/edit/cancel", &http_router::routeLocalDiskVideoEditCancel)
		.Post("/api/v1/local-disk/video/subtitle/export", &http_router::routeLocalDiskVideoSubtitleExport)
		.Post("/api/v1/local-disk/video/subtitle/export/progress", &http_router::routeLocalDiskVideoSubtitleExportProgress)
		.Post("/api/v1/local-disk/video/subtitle/export/cancel", &http_router::routeLocalDiskVideoSubtitleExportCancel)
		.Post("/api/v1/local-disk/video/keyframes/export", &http_router::routeLocalDiskVideoKeyframeExport)
		.Post("/api/v1/local-disk/video/keyframes/export/progress", &http_router::routeLocalDiskVideoKeyframeExportProgress)
		.Post("/api/v1/local-disk/video/keyframes/export/cancel", &http_router::routeLocalDiskVideoKeyframeExportCancel)
		.Post("/api/v1/local-disk/video/ai-enhance", &http_router::routeLocalDiskAiVideoEnhance)
		.Post("/api/v1/local-disk/video/ai-enhance/progress", &http_router::routeLocalDiskAiVideoEnhanceProgress)
		.Post("/api/v1/local-disk/video/ai-enhance/cancel", &http_router::routeLocalDiskAiVideoEnhanceCancel)
		.Post("/api/v1/local-disk/video/stream-state", &http_router::routeLocalDiskVideoStreamState)
		.Post("/api/v1/tags/create", &http_router::routeTagCreate)
		.Post("/api/v1/tags/rename", &http_router::routeTagRename)
		.Post("/api/v1/tags/delete", &http_router::routeTagDelete)
		.Post("/api/v1/tags/move", &http_router::routeTagMove)
		.Post("/api/v1/tags/bind", &http_router::routeTagBind)
		.Post("/api/v1/tags/unbind", &http_router::routeTagUnbind)
		.Post("/api/v1/tags/lock", &http_router::routeTagLock)
		.Post("/api/v1/tags/unlock", &http_router::routeTagUnlock)
		.Post("/api/v1/tags/lock/verify", &http_router::routeTagLockVerify);

	service_.Default(&http_router::routeDefault);
	service_.CheckPerm(&http_router::checkPerm);
	service_.AfterHandle(&http_router::afterHandler);
}

bool http_router::checkPerm(const char* path, request_t& req, response_t& res, bool& ok) {
	ok = false; // Sanity set.

	if (is_auth_route(path) || is_static_route(path) || is_root_route(path)) {
		ok = true;
		return true;
	}

	std::string username;
	bool admin = false;
	if (!action::auth_request_allowed(req, action::runtime_upload_dir_get(),
		 username, admin)) {
		return action::auth_send_required(req, res);
	}

	if (is_admin_route(path) && !admin) {
		acl::json json;
		acl::json_node &root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "admin permission required");
		return action::sendJson(res, 403, root, req.isKeepAlive());
	}

	if (request_needs_local_disk_access(path, req)) {
		std::string err;
		if (!action::local_disk_access_allowed(action::runtime_upload_dir_get(),
				admin, err)) {
			acl::json json;
			acl::json_node &root = json.create_node();
			root.add_bool("ok", false);
			root.add_text("error",
				err.empty() ? "local disk access denied" : err.c_str());
			return action::sendJson(res, err.empty() ? 403 : 500,
				root, req.isKeepAlive());
		}
	}
	ok = true;
	return true;
}

void http_router::afterHandler(const char* path, request_t &req) {
	std::vector<std::string> sync_paths;
	std::vector<std::string> delete_paths;
	std::vector<std::string> move_from_paths;
	std::vector<std::string> move_to_paths;
	bool full_sync = false;

	if (request_storage_backup_events(path, req, sync_paths,
		delete_paths, move_from_paths, move_to_paths, full_sync))
	{
		std::string sync_err;
		std::string backup_upload_dir = action::runtime_upload_dir_get();
		std::string dir_err;
		(void) action::authenticated_user_upload_dir(req,
			action::runtime_upload_dir_get(), backup_upload_dir, dir_err);
		if (full_sync) {
			(void) action::storage_backup_sync_now(backup_upload_dir, sync_err);
		} else {
			(void) action::storage_backup_sync_path_moves(
				backup_upload_dir, sync_paths, delete_paths,
				move_from_paths, move_to_paths, sync_err);
		}
	}
}

bool http_router::routeDefault(const char*, request_t& req, response_t& res) {
	acl::http_method_t method = req.getMethod();
	if (method == acl::HTTP_METHOD_GET) {
			return action::IndexAction::run(req, res);
	}

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", "unsupported api path");
	logger_error("unsupported api path: %s", req.getPathInfo());
	return action::sendJson(res, 404, root, req.isKeepAlive());
}

bool http_router::routeAuthStatus(request_t& req, response_t& res) {
	return action::AuthStatusAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeAuthRegister(request_t& req, response_t& res) {
	return action::AuthRegisterAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeAuthLogin(request_t& req, response_t& res) {
	return action::AuthLoginAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeAuthLogout(request_t& req, response_t& res) {
	return action::AuthLogoutAction::run(req, res);
}

bool http_router::routeAuthPassword(request_t& req, response_t& res) {
	return action::AuthPasswordAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeAuthPreferencesGet(request_t& req, response_t& res) {
	return action::AuthPreferencesGetAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeAuthPreferencesSet(request_t& req, response_t& res) {
	return action::AuthPreferencesSetAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeAuthUsers(request_t& req, response_t& res) {
	return action::AuthUsersAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeAuthUserCreate(request_t& req, response_t& res) {
	return action::AuthUserCreateAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeAuthUserUpdate(request_t& req, response_t& res) {
	return action::AuthUserUpdateAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeAuthUserDelete(request_t& req, response_t& res) {
	return action::AuthUserDeleteAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeTemplateReload(request_t& req, response_t& res) {
	return action::TemplateReloadAction::run(req, res);
}

bool http_router::userUploadDir(request_t& req, response_t& res, std::string& dir) {
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

bool http_router::routeAdminStorageInfo(request_t& req, response_t& res) {
	return action::AdminStorageInfoAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeAdminStorageMigrate(request_t& req, response_t& res) {
	return action::AdminStorageMigrateAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeAdminStorageMigrateProgress(request_t& req, response_t& res) {
	return action::AdminStorageMigrateProgressAction::run(req, res);
}

bool http_router::routeAdminStorageMigrateResolve(request_t& req, response_t& res) {
	return action::AdminStorageMigrateResolveAction::run(req, res);
}

bool http_router::routeAdminStorageMigrateControl(request_t& req, response_t& res) {
	return action::AdminStorageMigrateControlAction::run(req, res);
}

bool http_router::routeAdminStorageMigrateCleanup(request_t& req, response_t& res) {
	return action::AdminStorageMigrateCleanupAction::run(req, res);
}

bool http_router::routeAdminStorageBackup(request_t& req, response_t& res) {
	return action::AdminStorageBackupAction::run(req, res,
		action::runtime_upload_dir_get());
}

bool http_router::routeAdminStorageSwapBackup(request_t& req, response_t& res) {
	return action::AdminStorageSwapBackupAction::run(req, res,
		action::runtime_upload_dir_get());
}

bool http_router::routeAdminStorageBackupSync(request_t& req, response_t& res) {
	return action::AdminStorageBackupSyncAction::run(req, res,
		action::runtime_upload_dir_get());
}

bool http_router::routeAdminStorageBackupSyncProgress(request_t& req, response_t& res) {
	return action::AdminStorageBackupSyncProgressAction::run(req, res);
}

bool http_router::routeAdminStorageBackupSyncResolve(request_t& req, response_t& res) {
	return action::AdminStorageBackupSyncResolveAction::run(req, res);
}

bool http_router::routeAdminStorageBackupSyncControl(request_t& req, response_t& res) {
	return action::AdminStorageBackupSyncControlAction::run(req, res);
}

bool http_router::routeAdminLocalDiskSettings(request_t& req, response_t& res) {
	return action::AdminLocalDiskSettingsAction::run(req, res,
		action::runtime_upload_dir_get());
}

bool http_router::routeDelete(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::DeleteAction::run(req, res, dir);
}

bool http_router::routeRestore(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::RestoreAction::run(req, res, dir);
}

bool http_router::routeMoveFile(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::MoveFileAction::run(req, res, dir);
}

bool http_router::routeCopyFile(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::CopyFileAction::run(req, res, dir);
}

bool http_router::routeRemoteCopyProgress(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::RemoteCopyProgressAction::run(req, res, dir);
}

bool http_router::routeRemoteCopyCancel(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::RemoteCopyCancelAction::run(req, res, dir);
}

bool http_router::routeRenameFile(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::RenameFileAction::run(req, res, dir);
}

bool http_router::routeFiles(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FilesAction::run(req, res, dir);
}

bool http_router::routeDownload(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::DownloadAction::run(req, res, dir);
}

bool http_router::routeOfficePreview(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::OfficePreviewAction::run(req, res, dir);
}

bool http_router::routeImageSave(request_t& req, response_t& res) {
	if (request_bool_param(req, "local")) {
		return action::ImageSaveAction::run(req, res, action::runtime_upload_dir_get());
	}
	std::string dir;
	return userUploadDir(req, res, dir) && action::ImageSaveAction::run(req, res, dir);
}

bool http_router::routeOpenFile(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::OpenFileAction::run(req, res, dir);
}

bool http_router::routeLocalDiskList(request_t& req, response_t& res) {
	return action::LocalDiskListAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeLocalDiskDownload(request_t& req, response_t& res) {
	return action::LocalDiskDownloadAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeLocalDiskOfficePreview(request_t& req, response_t& res) {
	return action::LocalDiskOfficePreviewAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeLocalDiskDelete(request_t& req, response_t& res) {
	return action::LocalDiskDeleteAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeLocalDiskCreateDir(request_t& req, response_t& res) {
	return action::LocalDiskCreateDirAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeLocalDiskMove(request_t& req, response_t& res) {
	return action::LocalDiskMoveAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeLocalDiskCopy(request_t& req, response_t& res) {
	return action::LocalDiskCopyAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeLocalDiskCopyFromRemote(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::LocalDiskCopyFromRemoteAction::run(req, res, dir);
}

bool http_router::routeLocalDiskRename(request_t& req, response_t& res) {
	return action::LocalDiskRenameAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeLocalDiskOpenTrash(request_t& req, response_t& res) {
	return action::LocalDiskOpenTrashAction::run(req, res);
}

bool http_router::routeLocalDiskOpenFile(request_t& req, response_t& res) {
	return action::LocalDiskOpenFileAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeLocalDiskImport(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::LocalDiskImportAction::run(req, res, dir);
}

bool http_router::routeLocalDiskImportProgress(request_t& req, response_t& res) {
	return action::LocalDiskImportProgressAction::run(req, res);
}

bool http_router::routeLocalDiskImportControl(request_t& req, response_t& res) {
	return action::LocalDiskImportControlAction::run(req, res);
}

bool http_router::routeLocalDiskVideoConvert(request_t& req, response_t& res) {
	return action::LocalDiskVideoConvertAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeLocalDiskAudioExtract(request_t& req, response_t& res) {
	return action::LocalDiskAudioExtractAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeLocalDiskAudioExtractProgress(request_t& req, response_t& res) {
	return action::LocalDiskAudioExtractAction::progress(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeLocalDiskAudioExtractCancel(request_t& req, response_t& res) {
	return action::LocalDiskAudioExtractAction::cancel(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeLocalDiskVideoStream(request_t& req, response_t& res) {
	return action::LocalDiskVideoStreamAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeLocalDiskVideoStreamState(request_t& req, response_t& res) {
	return action::LocalDiskVideoStreamStateAction::run(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeUpload(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::UploadAction::run(req, res, dir);
}

bool http_router::routeUploadStream(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::UploadStreamAction::run(req, res, dir);
}

bool http_router::routeUploadStreamStatus(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::UploadStreamAction::status(req, res, dir);
}

bool http_router::routeVideoConvert(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::VideoConvertAction::run(req, res, dir);
}

bool http_router::routeAudioExtract(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::AudioExtractAction::run(req, res, dir);
}

bool http_router::routeAudioExtractProgress(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::AudioExtractAction::progress(req, res, dir);
}

bool http_router::routeAudioExtractCancel(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::AudioExtractAction::cancel(req, res, dir);
}

bool http_router::routeVideoConvertProgress(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::VideoConvertProgressAction::run(req, res, dir);
}

bool http_router::routeVideoConvertTasks(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::VideoConvertTasksAction::run(req, res, dir);
}

bool http_router::routeVideoConvertCancel(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::VideoConvertCancelAction::run(req, res, dir);
}

bool http_router::routeVideoProbe(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::VideoProbeAction::run(req, res, dir);
}

bool http_router::routeVideoProperties(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::VideoPropertiesAction::run(req, res, dir);
}

bool http_router::routeLocalDiskVideoProperties(request_t& req, response_t& res) {
	return action::VideoPropertiesAction::runLocal(req, res, action::runtime_upload_dir_get());
}

bool http_router::routeVideoEnhance(request_t& req, response_t& res) { std::string d; return userUploadDir(req, res, d) && action::VideoEnhanceAction::run(req, res, d, false); }
bool http_router::routeVideoEnhanceProgress(request_t& req, response_t& res) { std::string d; return userUploadDir(req, res, d) && action::VideoEnhanceAction::progress(req, res, d, false); }
bool http_router::routeVideoEnhanceCancel(request_t& req, response_t& res) { std::string d; return userUploadDir(req, res, d) && action::VideoEnhanceAction::cancel(req, res, d, false); }
bool http_router::routeLocalDiskVideoEnhance(request_t& req, response_t& res) { return action::VideoEnhanceAction::run(req, res, action::runtime_upload_dir_get(), true); }
bool http_router::routeLocalDiskVideoEnhanceProgress(request_t& req, response_t& res) { return action::VideoEnhanceAction::progress(req, res, action::runtime_upload_dir_get(), true); }
bool http_router::routeLocalDiskVideoEnhanceCancel(request_t& req, response_t& res) { return action::VideoEnhanceAction::cancel(req, res, action::runtime_upload_dir_get(), true); }
bool http_router::routeVideoEdit(request_t& req, response_t& res) { std::string d; return userUploadDir(req, res, d) && action::VideoEditAction::run(req, res, d, false); }
bool http_router::routeVideoEditProgress(request_t& req, response_t& res) { std::string d; return userUploadDir(req, res, d) && action::VideoEditAction::progress(req, res, d, false); }
bool http_router::routeVideoEditCancel(request_t& req, response_t& res) { std::string d; return userUploadDir(req, res, d) && action::VideoEditAction::cancel(req, res, d, false); }
bool http_router::routeLocalDiskVideoEdit(request_t& req, response_t& res) {
	std::string user_dir;
	return userUploadDir(req, res, user_dir)
		&& action::VideoEditAction::run(req, res,
			action::runtime_upload_dir_get(), true, user_dir);
}
bool http_router::routeLocalDiskVideoEditProgress(request_t& req, response_t& res) { return action::VideoEditAction::progress(req, res, action::runtime_upload_dir_get(), true); }
bool http_router::routeLocalDiskVideoEditCancel(request_t& req, response_t& res) { return action::VideoEditAction::cancel(req, res, action::runtime_upload_dir_get(), true); }
bool http_router::routeVideoSubtitleExport(request_t& req, response_t& res) { std::string d; return userUploadDir(req, res, d) && action::VideoEditAction::exportSubtitle(req, res, d, false); }
bool http_router::routeLocalDiskVideoSubtitleExport(request_t& req, response_t& res) { return action::VideoEditAction::exportSubtitle(req, res, action::runtime_upload_dir_get(), true); }
bool http_router::routeVideoSubtitleExportProgress(request_t& req, response_t& res) { std::string d; return userUploadDir(req, res, d) && action::VideoEditAction::subtitleProgress(req, res, d, false); }
bool http_router::routeVideoSubtitleExportCancel(request_t& req, response_t& res) { std::string d; return userUploadDir(req, res, d) && action::VideoEditAction::subtitleCancel(req, res, d, false); }
bool http_router::routeLocalDiskVideoSubtitleExportProgress(request_t& req, response_t& res) { return action::VideoEditAction::subtitleProgress(req, res, action::runtime_upload_dir_get(), true); }
bool http_router::routeLocalDiskVideoSubtitleExportCancel(request_t& req, response_t& res) { return action::VideoEditAction::subtitleCancel(req, res, action::runtime_upload_dir_get(), true); }
bool http_router::routeVideoKeyframeExport(request_t& req, response_t& res) { std::string d; return userUploadDir(req, res, d) && action::VideoEditAction::exportKeyframes(req, res, d, false); }
bool http_router::routeLocalDiskVideoKeyframeExport(request_t& req, response_t& res) { return action::VideoEditAction::exportKeyframes(req, res, action::runtime_upload_dir_get(), true); }
bool http_router::routeVideoKeyframeExportProgress(request_t& req, response_t& res) { std::string d; return userUploadDir(req, res, d) && action::VideoEditAction::keyframeProgress(req, res, d, false); }
bool http_router::routeVideoKeyframeExportCancel(request_t& req, response_t& res) { std::string d; return userUploadDir(req, res, d) && action::VideoEditAction::keyframeCancel(req, res, d, false); }
bool http_router::routeLocalDiskVideoKeyframeExportProgress(request_t& req, response_t& res) { return action::VideoEditAction::keyframeProgress(req, res, action::runtime_upload_dir_get(), true); }
bool http_router::routeLocalDiskVideoKeyframeExportCancel(request_t& req, response_t& res) { return action::VideoEditAction::keyframeCancel(req, res, action::runtime_upload_dir_get(), true); }
bool http_router::routeAiVideoEnhance(request_t& req, response_t& res) { std::string d; return userUploadDir(req, res, d) && action::AiVideoEnhanceAction::run(req, res, d, false); }
bool http_router::routeAiVideoEnhanceProgress(request_t& req, response_t& res) { std::string d; return userUploadDir(req, res, d) && action::AiVideoEnhanceAction::progress(req, res, d, false); }
bool http_router::routeAiVideoEnhanceCancel(request_t& req, response_t& res) { std::string d; return userUploadDir(req, res, d) && action::AiVideoEnhanceAction::cancel(req, res, d, false); }
bool http_router::routeLocalDiskAiVideoEnhance(request_t& req, response_t& res) { return action::AiVideoEnhanceAction::run(req, res, action::runtime_upload_dir_get(), true); }
bool http_router::routeLocalDiskAiVideoEnhanceProgress(request_t& req, response_t& res) { return action::AiVideoEnhanceAction::progress(req, res, action::runtime_upload_dir_get(), true); }
bool http_router::routeLocalDiskAiVideoEnhanceCancel(request_t& req, response_t& res) { return action::AiVideoEnhanceAction::cancel(req, res, action::runtime_upload_dir_get(), true); }

bool http_router::routeVideoResumeGet(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::VideoResumeGetAction::run(req, res, dir);
}

bool http_router::routeVideoResumeSet(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::VideoResumeSetAction::run(req, res, dir);
}

bool http_router::routeFolderList(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderListAction::run(req, res, dir);
}

bool http_router::routeFolderCreate(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderCreateAction::run(req, res, dir);
}

bool http_router::routeFolderRename(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderRenameAction::run(req, res, dir);
}

bool http_router::routeFolderMove(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderMoveAction::run(req, res, dir);
}

bool http_router::routeFolderCopy(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderCopyAction::run(req, res, dir);
}

bool http_router::routeFolderDelete(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderDeleteAction::run(req, res, dir);
}

bool http_router::routeFolderEmpty(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderEmptyAction::run(req, res, dir);
}

bool http_router::routeFolderLock(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderLockAction::run(req, res, dir);
}

bool http_router::routeFolderUnlock(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderUnlockAction::run(req, res, dir);
}

bool http_router::routeFolderLockVerify(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::FolderLockVerifyAction::run(req, res, dir);
}

bool http_router::routeFileLock(request_t& req, response_t& res) {
	if (request_bool_param(req, "local")) {
		return action::FileLockAction::run(req, res, action::runtime_upload_dir_get());
	}
	std::string dir;
	return userUploadDir(req, res, dir) && action::FileLockAction::run(req, res, dir);
}

bool http_router::routeFileUnlock(request_t& req, response_t& res) {
	if (request_bool_param(req, "local")) {
		return action::FileUnlockAction::run(req, res, action::runtime_upload_dir_get());
	}
	std::string dir;
	return userUploadDir(req, res, dir) && action::FileUnlockAction::run(req, res, dir);
}

bool http_router::routeFileLockVerify(request_t& req, response_t& res) {
	if (request_bool_param(req, "local")) {
		return action::FileLockVerifyAction::run(req, res, action::runtime_upload_dir_get());
	}
	std::string dir;
	return userUploadDir(req, res, dir) && action::FileLockVerifyAction::run(req, res, dir);
}

bool http_router::routeTagList(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagListAction::run(req, res, dir);
}

bool http_router::routeTagCreate(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagCreateAction::run(req, res, dir);
}

bool http_router::routeTagRename(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagRenameAction::run(req, res, dir);
}

bool http_router::routeTagDelete(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagDeleteAction::run(req, res, dir);
}

bool http_router::routeTagMove(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagMoveAction::run(req, res, dir);
}

bool http_router::routeTagBind(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagBindAction::run(req, res, dir);
}

bool http_router::routeTagUnbind(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagUnbindAction::run(req, res, dir);
}

bool http_router::routeTagLock(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagLockAction::run(req, res, dir);
}

bool http_router::routeTagUnlock(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagUnlockAction::run(req, res, dir);
}

bool http_router::routeTagLockVerify(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagLockVerifyAction::run(req, res, dir);
}

bool http_router::routeTagFiles(request_t& req, response_t& res) {
	std::string dir;
	return userUploadDir(req, res, dir) && action::TagFilesAction::run(req, res, dir);
}

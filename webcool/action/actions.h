#pragma once

#include "../stdafx.h"
#include <map>
#include <string>

namespace action {

typedef ::request_t request_t;
typedef ::response_t response_t;

bool init_video_resume_db(const std::string& upload_dir, std::string& err);
bool init_category_folder_db(const std::string& upload_dir, std::string& err);
bool init_tag_db(const std::string& upload_dir, std::string& err);
bool init_recycle_bin_db(const std::string& upload_dir, std::string& err);
void runtime_sqlite_lib_set(const std::string& sqlite_lib_path);
std::string runtime_sqlite_lib_get();
void runtime_ffmpeg_path_set(const std::string& ffmpeg_path);
std::string runtime_ffmpeg_path_get();
void runtime_upload_dir_init(const std::string& upload_dir);
std::string runtime_upload_dir_get();
void runtime_upload_dir_set(const std::string& upload_dir);
bool storage_prepare_startup_primary(const std::string& upload_dir,
	bool upload_dir_specified, std::string& err);
bool storage_backup_sync_now(const std::string& upload_dir, std::string& err);
bool storage_backup_sync_paths(const std::string& upload_dir,
	const std::vector<std::string>& sync_paths,
	const std::vector<std::string>& delete_paths, std::string& err);
bool storage_backup_sync_path_moves(const std::string& upload_dir,
	const std::vector<std::string>& sync_paths,
	const std::vector<std::string>& delete_paths,
	const std::vector<std::string>& move_from_paths,
	const std::vector<std::string>& move_to_paths, std::string& err);
bool storage_backup_upload_auto_sync_enabled(const std::string& upload_dir,
	std::string& err);
bool local_disk_access_allowed(const std::string& upload_dir, bool admin,
	std::string& err);
bool auth_system_initialized(const std::string& upload_dir);
bool auth_request_allowed(const request_t& req, const std::string& upload_dir,
	std::string& username, bool& admin);
bool auth_current_user(const request_t& req, const std::string& upload_dir,
	std::string& username, bool& admin);
bool authenticated_user_upload_dir(const request_t& req,
	const std::string& upload_dir, std::string& user_upload_dir,
	std::string& err);
bool auth_send_required(const request_t& req, response_t& res);
struct user_prefs_t {
	std::string ui_language;
	std::string font_size;
};
user_prefs_t default_user_prefs();
bool valid_username_for_prefs(const std::string& username, std::string& err);
bool load_user_prefs(const std::string& upload_dir, const std::string& username,
	user_prefs_t& prefs, std::string& err);
bool save_user_prefs(const std::string& upload_dir, const std::string& username,
	const user_prefs_t& prefs, std::string& err);
bool delete_user_prefs(const std::string& upload_dir, const std::string& username,
	std::string& err);
bool rename_user_prefs(const std::string& upload_dir,
	const std::string& old_username, const std::string& new_username,
	std::string& err);
void add_user_prefs_json(acl::json_node& root, const user_prefs_t& prefs);
bool append_authenticated_user_prefs(acl::json_node& root,
	const std::string& upload_dir, const std::string& username);
bool recycle_bin_insert_record(const std::string& upload_dir,
	const std::string& recycle_rel, const std::string& original_path,
	std::string& err);
bool folder_lock_path_allows(const std::string& upload_dir,
	const std::string& relative_path, const std::string& password,
	bool& allowed, std::string& locked_path, std::string& err);
bool folder_lock_path_has_lock(const std::string& upload_dir,
	const std::string& relative_path, bool& locked, std::string& err);
bool folder_lock_rename_prefix(const std::string& upload_dir,
	const std::string& old_prefix, const std::string& new_prefix,
	std::string& err);
bool file_lock_path_allows(const std::string& upload_dir,
	const std::string& file_key, const std::string& password,
	bool& allowed, std::string& err);
bool file_lock_path_has_lock(const std::string& upload_dir,
	const std::string& file_key, bool& locked, std::string& err);
bool file_lock_rename_key(const std::string& upload_dir,
	const std::string& old_key, const std::string& new_key, std::string& err);
bool file_lock_rename_prefix(const std::string& upload_dir,
	const std::string& old_prefix, const std::string& new_prefix,
	std::string& err);
bool local_dir_lock_path_allows(const std::string& upload_dir,
	const std::string& path, const std::string& password,
	bool& allowed, std::string& locked_path, std::string& err);
bool local_dir_lock_path_has_lock(const std::string& upload_dir,
	const std::string& path, bool& locked, std::string& err);
bool named_lock_set(const std::string& upload_dir,
	const std::string& key, const std::string& password, std::string& err);
bool named_lock_remove(const std::string& upload_dir,
	const std::string& key, const std::string& password, std::string& err);
bool named_lock_verify(const std::string& upload_dir,
	const std::string& key, const std::string& password, bool& allowed,
	std::string& err);
bool folder_bind_file(const std::string& upload_dir, const std::string& file_name,
	long long folder_id, std::string& err);
bool folder_unbind_file(const std::string& upload_dir,
	const std::string& file_name, std::string& err);
bool tag_unbind_file(const std::string& upload_dir,
	const std::string& file_name, std::string& err);
bool tag_rename_file(const std::string& upload_dir,
	const std::string& old_file_name, const std::string& new_file_name,
	std::string& err);
bool tag_rename_folder_prefix(const std::string& upload_dir,
	const std::string& old_prefix, const std::string& new_prefix,
	std::string& err);
bool video_resume_rename_file(const std::string& upload_dir,
	const std::string& old_file_name, const std::string& new_file_name,
	std::string& err);
bool video_resume_rename_folder_prefix(const std::string& upload_dir,
	const std::string& old_prefix, const std::string& new_prefix,
	std::string& err);
bool folder_load_file_bindings(const std::string& upload_dir,
	std::map<std::string, long long>& file_to_folder_id,
	std::map<long long, std::string>& folder_id_to_name,
	std::string& err);

class IndexAction {
public:
	static bool run(const request_t& req, response_t& res);
	static void set_static_home_path(const char* path);
};

class TemplateReloadAction {
public:
	static bool run(const request_t& req, response_t& res);
};

class AdminStorageInfoAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class AdminStorageMigrateAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class AdminStorageMigrateProgressAction {
public:
	static bool run(request_t& req, response_t& res);
};

class AdminStorageMigrateResolveAction {
public:
	static bool run(request_t& req, response_t& res);
};

class AdminStorageMigrateControlAction {
public:
	static bool run(request_t& req, response_t& res);
};

class AdminStorageMigrateCleanupAction {
public:
	static bool run(request_t& req, response_t& res);
};

class AdminStorageBackupAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class AdminStorageSwapBackupAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class AdminStorageBackupSyncAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class AdminStorageBackupSyncProgressAction {
public:
	static bool run(request_t& req, response_t& res);
};

class AdminStorageBackupSyncResolveAction {
public:
	static bool run(request_t& req, response_t& res);
};

class AdminStorageBackupSyncControlAction {
public:
	static bool run(request_t& req, response_t& res);
};

class AdminLocalDiskSettingsAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class AuthStatusAction {
public:
	static bool run(const request_t& req, response_t& res,
		const std::string& upload_dir);
};

class AuthRegisterAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class AuthLoginAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class AuthLogoutAction {
public:
	static bool run(const request_t& req, response_t& res);
};

class AuthPasswordAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class AuthUsersAction {
public:
	static bool run(const request_t& req, response_t& res,
		const std::string& upload_dir);
};

class AuthUserCreateAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class AuthUserUpdateAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class AuthUserDeleteAction {
public:
	static bool run(const request_t& req, response_t& res,
		const std::string& upload_dir);
};

class AuthPreferencesGetAction {
public:
	static bool run(const request_t& req, response_t& res,
		const std::string& upload_dir);
};

class AuthPreferencesSetAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class FilesAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class DeleteAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class RestoreAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class MoveFileAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class CopyFileAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class RemoteCopyProgressAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class RemoteCopyCancelAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class RenameFileAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class DownloadAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class OfficePreviewAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class OpenFileAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class ImageSaveAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class LocalDiskListAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class LocalDiskDownloadAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class LocalDiskOfficePreviewAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class LocalDiskDeleteAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class LocalDiskCreateDirAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class LocalDiskMoveAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class LocalDiskCopyAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class LocalDiskCopyFromRemoteAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class LocalDiskRenameAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class LocalDiskOpenTrashAction {
public:
	static bool run(request_t& req, response_t& res);
};

class LocalDiskOpenFileAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class LocalDiskImportAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class LocalDiskImportProgressAction {
public:
	static bool run(request_t& req, response_t& res);
};

class LocalDiskImportControlAction {
public:
	static bool run(request_t& req, response_t& res);
};

class LocalDiskVideoConvertAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class LocalDiskVideoStreamAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class LocalDiskVideoStreamStateAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class UploadAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);

private:
	static bool readBody(const request_t& req, long long content_length,
		acl::ofstream& fp, acl::http_mime& mime);

	static bool saveFiles(const acl::http_mime& mime, const std::string& upload_dir,
		const std::string& tmp_path, acl::json_node& files_array, int& saved_count,
		const std::string& folder_path, std::vector<std::string>& saved_paths);
};

class UploadStreamAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);

	static bool status(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class VideoConvertAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class AudioExtractAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
	static bool progress(request_t& req, response_t& res,
		const std::string& upload_dir);
	static bool cancel(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class LocalDiskAudioExtractAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
	static bool progress(request_t& req, response_t& res,
		const std::string& upload_dir);
	static bool cancel(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class VideoConvertProgressAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class VideoConvertTasksAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class VideoConvertCancelAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class VideoProbeAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class VideoPropertiesAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
	static bool runLocal(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class VideoEnhanceAction {
public:
	static bool run(request_t& req, response_t& res, const std::string& upload_dir, bool local);
	static bool progress(request_t& req, response_t& res, const std::string& upload_dir, bool local);
	static bool cancel(request_t& req, response_t& res, const std::string& upload_dir, bool local);
};

class VideoEditAction {
public:
	static bool run(request_t& req, response_t& res, const std::string& upload_dir, bool local);
	static bool progress(request_t& req, response_t& res, const std::string& upload_dir, bool local);
	static bool cancel(request_t& req, response_t& res, const std::string& upload_dir, bool local);
};

class AiVideoEnhanceAction {
public:
	static bool run(request_t& req, response_t& res, const std::string& upload_dir, bool local);
	static bool progress(request_t& req, response_t& res, const std::string& upload_dir, bool local);
	static bool cancel(request_t& req, response_t& res, const std::string& upload_dir, bool local);
};

class VideoResumeGetAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class VideoResumeSetAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class FolderListAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class FolderCreateAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class FolderRenameAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class FolderMoveAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class FolderCopyAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class FolderDeleteAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class FolderEmptyAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class FolderLockAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class FolderUnlockAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class FolderLockVerifyAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class FileLockAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class FileUnlockAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class FileLockVerifyAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class TagListAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class TagCreateAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class TagRenameAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class TagDeleteAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class TagMoveAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class TagBindAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class TagUnbindAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class TagLockAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class TagUnlockAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class TagLockVerifyAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

class TagFilesAction {
public:
	static bool run(request_t& req, response_t& res,
		const std::string& upload_dir);
};

} // namespace action

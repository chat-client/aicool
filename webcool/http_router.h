#pragma once
#include "stdafx.h"
#include <string>

/**
 * HTTP 文件上传处理 Servlet
 *
 * 支持：
 *  - GET /                       : 返回前端页面
 *  - GET /api/v1/files           : 返回已上传文件 JSON 列表
 *  - GET /api/v1/download?file=  : 下载文件（二进制）
 *  - GET /api/v1/delete?file=    : 删除文件，返回 JSON
 *  - GET /api/v1/admin/template/reload : 清空模板缓存，返回 JSON
 *  - POST /api/v1/upload         : multipart 上传，返回 JSON
 */

class http_service;
class http_router {
public:
	http_router(http_service& service);
	~http_router() = default;

	void setup() const;

private:
	http_service& service_;

	static bool checkPerm(const char* path, request_t& req, response_t& res, bool& ok);
	static void afterHandler(const char* path, request_t& req);

	static bool routeDefault(const char* path, request_t& req, response_t& res);
	static bool userUploadDir(request_t& req, response_t& res, std::string& dir);
	//bool routeStaticPage(request_t& req, response_t& res);
	static bool routeAuthStatus(request_t& req, response_t& res);
	static bool routeAuthRegister(request_t& req, response_t& res);
	static bool routeAuthLogin(request_t& req, response_t& res);
	static bool routeAuthLogout(request_t& req, response_t& res);
	static bool routeAuthPassword(request_t& req, response_t& res);
	static bool routeAuthPreferencesGet(request_t& req, response_t& res);
	static bool routeAuthPreferencesSet(request_t& req, response_t& res);
	static bool routeAuthUsers(request_t& req, response_t& res);
	static bool routeAuthUserCreate(request_t& req, response_t& res);
	static bool routeAuthUserUpdate(request_t& req, response_t& res);
	static bool routeAuthUserDelete(request_t& req, response_t& res);
	static bool routeTemplateReload(request_t& req, response_t& res);
	static bool routeAdminStorageInfo(request_t& req, response_t& res);
	static bool routeAdminStorageMigrate(request_t& req, response_t& res);
	static bool routeAdminStorageMigrateProgress(request_t& req, response_t& res);
	static bool routeAdminStorageMigrateResolve(request_t& req, response_t& res);
	static bool routeAdminStorageMigrateControl(request_t& req, response_t& res);
	static bool routeAdminStorageMigrateCleanup(request_t& req, response_t& res);
	static bool routeAdminStorageBackup(request_t& req, response_t& res);
	static bool routeAdminStorageSwapBackup(request_t& req, response_t& res);
	static bool routeAdminStorageBackupSync(request_t& req, response_t& res);
	static bool routeAdminStorageBackupSyncProgress(request_t& req, response_t& res);
	static bool routeAdminStorageBackupSyncResolve(request_t& req, response_t& res);
	static bool routeAdminStorageBackupSyncControl(request_t& req, response_t& res);
	static bool routeAdminLocalDiskSettings(request_t& req, response_t& res);
	static bool routeDelete(request_t& req, response_t& res);
	static bool routeRestore(request_t& req, response_t& res);
	static bool routeMoveFile(request_t& req, response_t& res);
	static bool routeCopyFile(request_t& req, response_t& res);
	static bool routeRemoteCopyProgress(request_t& req, response_t& res);
	static bool routeRemoteCopyCancel(request_t& req, response_t& res);
	static bool routeRenameFile(request_t& req, response_t& res);
	static bool routeFiles(request_t& req, response_t& res);
	static bool routeDownload(request_t& req, response_t& res);
	static bool routeOfficePreview(request_t& req, response_t& res);
	static bool routeOpenFile(request_t& req, response_t& res);
	static bool routeImageSave(request_t& req, response_t& res);
	static bool routeImageEnhance(request_t& req, response_t& res);
	static bool routeImageEnhanceProgress(request_t& req, response_t& res);
	static bool routeImageEnhanceCancel(request_t& req, response_t& res);
	static bool routeLocalDiskList(request_t& req, response_t& res);
	static bool routeLocalDiskDownload(request_t& req, response_t& res);
	static bool routeLocalDiskOfficePreview(request_t& req, response_t& res);
	static bool routeLocalDiskDelete(request_t& req, response_t& res);
	static bool routeLocalDiskCreateDir(request_t& req, response_t& res);
	static bool routeLocalDiskMove(request_t& req, response_t& res);
	static bool routeLocalDiskCopy(request_t& req, response_t& res);
	static bool routeLocalDiskCopyFromRemote(request_t& req, response_t& res);
	static bool routeLocalDiskRename(request_t& req, response_t& res);
	static bool routeLocalDiskOpenTrash(request_t& req, response_t& res);
	static bool routeLocalDiskOpenFile(request_t& req, response_t& res);
	static bool routeLocalDiskImageEnhance(request_t& req, response_t& res);
	static bool routeLocalDiskImageEnhanceProgress(request_t& req, response_t& res);
	static bool routeLocalDiskImageEnhanceCancel(request_t& req, response_t& res);
	static bool routeLocalDiskImport(request_t& req, response_t& res);
	static bool routeLocalDiskImportProgress(request_t& req, response_t& res);
	static bool routeLocalDiskImportControl(request_t& req, response_t& res);
	static bool routeLocalDiskVideoConvert(request_t& req, response_t& res);
	static bool routeLocalDiskAudioExtract(request_t& req, response_t& res);
	static bool routeLocalDiskAudioExtractProgress(request_t& req, response_t& res);
	static bool routeLocalDiskAudioExtractCancel(request_t& req, response_t& res);
	static bool routeLocalDiskVideoStream(request_t& req, response_t& res);
	static bool routeLocalDiskVideoStreamState(request_t& req, response_t& res);
	static bool routeVideoConvert(request_t& req, response_t& res);
	static bool routeAudioExtract(request_t& req, response_t& res);
	static bool routeAudioExtractProgress(request_t& req, response_t& res);
	static bool routeAudioExtractCancel(request_t& req, response_t& res);
	static bool routeVideoConvertProgress(request_t& req, response_t& res);
	static bool routeVideoConvertTasks(request_t& req, response_t& res);
	static bool routeVideoConvertCancel(request_t& req, response_t& res);
	static bool routeVideoProbe(request_t& req, response_t& res);
	static bool routeVideoProperties(request_t& req, response_t& res);
	static bool routeLocalDiskVideoProperties(request_t& req, response_t& res);
	static bool routeVideoEnhance(request_t& req, response_t& res);
	static bool routeVideoEnhanceProgress(request_t& req, response_t& res);
	static bool routeVideoEnhanceCancel(request_t& req, response_t& res);
	static bool routeLocalDiskVideoEnhance(request_t& req, response_t& res);
	static bool routeLocalDiskVideoEnhanceProgress(request_t& req, response_t& res);
	static bool routeLocalDiskVideoEnhanceCancel(request_t& req, response_t& res);
	static bool routeVideoEdit(request_t& req, response_t& res);
	static bool routeVideoEditProgress(request_t& req, response_t& res);
	static bool routeVideoEditCancel(request_t& req, response_t& res);
	static bool routeLocalDiskVideoEdit(request_t& req, response_t& res);
	static bool routeLocalDiskVideoEditProgress(request_t& req, response_t& res);
	static bool routeLocalDiskVideoEditCancel(request_t& req, response_t& res);
	static bool routeVideoSubtitleExport(request_t& req, response_t& res);
	static bool routeLocalDiskVideoSubtitleExport(request_t& req, response_t& res);
	static bool routeVideoSubtitleExportProgress(request_t& req, response_t& res);
	static bool routeVideoSubtitleExportCancel(request_t& req, response_t& res);
	static bool routeLocalDiskVideoSubtitleExportProgress(request_t& req, response_t& res);
	static bool routeLocalDiskVideoSubtitleExportCancel(request_t& req, response_t& res);
	static bool routeVideoKeyframeExport(request_t& req, response_t& res);
	static bool routeLocalDiskVideoKeyframeExport(request_t& req, response_t& res);
	static bool routeVideoKeyframeExportProgress(request_t& req, response_t& res);
	static bool routeVideoKeyframeExportCancel(request_t& req, response_t& res);
	static bool routeLocalDiskVideoKeyframeExportProgress(request_t& req, response_t& res);
	static bool routeLocalDiskVideoKeyframeExportCancel(request_t& req, response_t& res);
	static bool routeAiVideoEnhance(request_t& req, response_t& res);
	static bool routeAiVideoEnhanceProgress(request_t& req, response_t& res);
	static bool routeAiVideoEnhanceCancel(request_t& req, response_t& res);
	static bool routeLocalDiskAiVideoEnhance(request_t& req, response_t& res);
	static bool routeLocalDiskAiVideoEnhanceProgress(request_t& req, response_t& res);
	static bool routeLocalDiskAiVideoEnhanceCancel(request_t& req, response_t& res);
	static bool routeVideoResumeGet(request_t& req, response_t& res);
	static bool routeVideoResumeSet(request_t& req, response_t& res);
	static bool routeFolderList(request_t& req, response_t& res);
	static bool routeFolderCreate(request_t& req, response_t& res);
	static bool routeFolderRename(request_t& req, response_t& res);
	static bool routeFolderMove(request_t& req, response_t& res);
	static bool routeFolderCopy(request_t& req, response_t& res);
	static bool routeFolderDelete(request_t& req, response_t& res);
	static bool routeFolderEmpty(request_t& req, response_t& res);
	static bool routeFolderLock(request_t& req, response_t& res);
	static bool routeFolderUnlock(request_t& req, response_t& res);
	static bool routeFolderLockVerify(request_t& req, response_t& res);
	static bool routeFileLock(request_t& req, response_t& res);
	static bool routeFileUnlock(request_t& req, response_t& res);
	static bool routeFileLockVerify(request_t& req, response_t& res);
	static bool routeTagList(request_t& req, response_t& res);
	static bool routeTagCreate(request_t& req, response_t& res);
	static bool routeTagRename(request_t& req, response_t& res);
	static bool routeTagDelete(request_t& req, response_t& res);
	static bool routeTagMove(request_t& req, response_t& res);
	static bool routeTagBind(request_t& req, response_t& res);
	static bool routeTagUnbind(request_t& req, response_t& res);
	static bool routeTagLock(request_t& req, response_t& res);
	static bool routeTagUnlock(request_t& req, response_t& res);
	static bool routeTagLockVerify(request_t& req, response_t& res);
	static bool routeUpload(request_t& req, response_t& res);
	static bool routeUploadStream(request_t& req, response_t& res);
	static bool routeUploadStreamStatus(request_t& req, response_t& res);
	static bool routeTagFiles(request_t& req, response_t& res);
};

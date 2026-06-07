// Shared helpers, state, and DOM references. Loaded first.

function t(text) {
        if (window.WebCoolI18n && typeof window.WebCoolI18n.t === 'function') {
          return window.WebCoolI18n.t(text);
        }
        return String(text == null ? '' : text);
      }

function selectRenameInputText(input) {
        if (!input || typeof input.select !== 'function') {
          return;
        }
        input.focus();
        input.select();
        input.setAttribute('data-rename-auto-selected', '1');
        const clearAutoSelection = function () {
          if (!input || input.getAttribute('data-rename-auto-selected') !== '1') {
            return;
          }
          window.setTimeout(function () {
            if (!input || input.getAttribute('data-rename-auto-selected') !== '1') {
              return;
            }
            if (document.activeElement === input && typeof input.setSelectionRange === 'function') {
              const value = String(input.value || '');
              if (input.selectionStart === 0 && input.selectionEnd === value.length) {
                input.setSelectionRange(value.length, value.length);
              }
            }
            input.removeAttribute('data-rename-auto-selected');
          }, 0);
        };
        input.addEventListener('pointerdown', clearAutoSelection, { once: true });
        input.addEventListener('keydown', function () {
          input.removeAttribute('data-rename-auto-selected');
        }, { once: true });
      }

      var api = {
        files: '/api/v1/files',
        folders: '/api/v1/folders',
        folderCreate: '/api/v1/folders/create',
        folderRename: '/api/v1/folders/rename',
        folderMove: '/api/v1/folders/move',
        folderCopy: '/api/v1/folders/copy',
        folderDelete: '/api/v1/folders/delete',
        folderLock: '/api/v1/folders/lock',
        folderUnlock: '/api/v1/folders/unlock',
        folderLockVerify: '/api/v1/folders/lock/verify',
        fileMove: '/api/v1/files/move',
        fileCopy: '/api/v1/files/copy',
        remoteCopyProgress: '/api/v1/remote-copy/progress',
        remoteCopyCancel: '/api/v1/remote-copy/cancel',
        fileRename: '/api/v1/files/rename',
        fileLock: '/api/v1/files/lock',
        fileUnlock: '/api/v1/files/unlock',
        fileLockVerify: '/api/v1/files/lock/verify',
        tags: '/api/v1/tags',
        tagCreate: '/api/v1/tags/create',
        tagRename: '/api/v1/tags/rename',
        tagDelete: '/api/v1/tags/delete',
        tagBind: '/api/v1/tags/bind',
        tagUnbind: '/api/v1/tags/unbind',
        tagLock: '/api/v1/tags/lock',
        tagUnlock: '/api/v1/tags/unlock',
        tagLockVerify: '/api/v1/tags/lock/verify',
        tagFiles: '/api/v1/tag-files',
        upload: '/api/v1/upload',
        del: '/api/v1/delete',
        restore: '/api/v1/restore',
        download: '/api/v1/download',
        imageSave: '/api/v1/image/save',
        localDiskList: '/api/v1/local-disk/list',
        localDiskDownload: '/api/v1/local-disk/download',
        localDiskDelete: '/api/v1/local-disk/delete',
        localDiskMkdir: '/api/v1/local-disk/mkdir',
        localDiskMove: '/api/v1/local-disk/move',
        localDiskCopy: '/api/v1/local-disk/copy',
        localDiskRename: '/api/v1/local-disk/rename',
        localDiskOpenTrash: '/api/v1/local-disk/open-trash',
        localDiskOpenFile: '/api/v1/local-disk/open-file',
        localDiskImport: '/api/v1/local-disk/import',
        localDiskImportProgress: '/api/v1/local-disk/import/progress',
        localDiskVideoConvert: '/api/v1/local-disk/video/convert',
        localDiskVideoStream: '/api/v1/local-disk/video/stream',
        localDiskVideoStreamState: '/api/v1/local-disk/video/stream-state',
        reloadTpl: '/api/v1/admin/template/reload',
        authStatus: '/api/v1/auth/status',
        authRegister: '/api/v1/auth/register',
        authLogin: '/api/v1/auth/login',
        authLogout: '/api/v1/auth/logout',
        authPassword: '/api/v1/auth/password',
        authUsers: '/api/v1/auth/users',
        authUserCreate: '/api/v1/auth/users/create',
        authUserUpdate: '/api/v1/auth/users/update',
        authUserDelete: '/api/v1/auth/users/delete',
        adminStorage: '/api/v1/admin/storage',
        adminStorageMigrate: '/api/v1/admin/storage/migrate',
        adminStorageMigrateProgress: '/api/v1/admin/storage/migrate/progress',
        adminStorageMigrateResolve: '/api/v1/admin/storage/migrate/resolve',
        adminStorageMigrateControl: '/api/v1/admin/storage/migrate/control',
        adminStorageMigrateCleanup: '/api/v1/admin/storage/migrate/cleanup',
        convertVideo: '/api/v1/video/convert',
        convertCancel: '/api/v1/video/convert/cancel',
        convertProgress: '/api/v1/video/convert/progress',
        convertTasks: '/api/v1/video/convert/tasks',
        probeVideo: '/api/v1/video/probe',
        videoResume: '/api/v1/video/resume',
        videoResumeSave: '/api/v1/video/resume/save'
      };

      var uploadForm = document.getElementById('upload-form');

      var reloadBtn = document.getElementById('reload-template-btn');

      var adminStorageTab = document.getElementById('admin-storage-tab');

      var adminUsersTab = document.getElementById('admin-users-tab');

      var adminLanguageTab = document.getElementById('admin-language-tab');

      var adminStorageView = document.getElementById('admin-storage-view');

      var adminUsersView = document.getElementById('admin-users-view');

      var adminLanguageView = document.getElementById('admin-language-view');

      var adminUserCreateForm = document.getElementById('admin-user-create-form');

      var adminNewUsername = document.getElementById('admin-new-username');

      var adminNewPassword = document.getElementById('admin-new-password');

      var adminUsersList = document.getElementById('admin-users-list');

      var authGate = document.getElementById('auth-gate');

      var authForm = document.getElementById('auth-form');

      var authTitle = document.getElementById('auth-title');

      var authDesc = document.getElementById('auth-desc');

      var authUsername = document.getElementById('auth-username');

      var authPassword = document.getElementById('auth-password');

      var authError = document.getElementById('auth-error');

      var authSubmitBtn = document.getElementById('auth-submit-btn');

      var authUserChip = document.getElementById('auth-user-chip');

      var authCurrentUser = document.getElementById('auth-current-user');

      var authLogoutBtn = document.getElementById('auth-logout-btn');

      var accountPasswordForm = document.getElementById('account-password-form');

      var accountOldPassword = document.getElementById('account-old-password');

      var accountNewPassword = document.getElementById('account-new-password');

      var accountConfirmPassword = document.getElementById('account-confirm-password');

      var adminLanguageSelect = document.getElementById('admin-language-select');

      var adminLanguageApplyBtn = document.getElementById('admin-language-apply-btn');

      var adminStoragePath = document.getElementById('admin-storage-path');

      var adminStorageBrowseBtn = document.getElementById('admin-storage-browse-btn');

      var adminStorageChooseBtn = document.getElementById('admin-storage-choose-btn');

      var adminStorageProgress = document.getElementById('admin-storage-progress');

      var adminStorageProgressFill = document.getElementById('admin-storage-progress-fill');

      var adminStorageProgressText = document.getElementById('admin-storage-progress-text');

      var adminStorageProgressMessage = document.getElementById('admin-storage-progress-message');

      var adminStorageProgressPauseBtn = document.getElementById('admin-storage-progress-pause');

      var adminStorageProgressResumeBtn = document.getElementById('admin-storage-progress-resume');

      var adminStorageProgressCancelBtn = document.getElementById('admin-storage-progress-cancel');

      var adminStoragePickerDialog = document.getElementById('admin-storage-picker-dialog');

      var adminStoragePickerTree = document.getElementById('admin-storage-picker-tree');

      var adminStoragePickerEmpty = document.getElementById('admin-storage-picker-empty');

      var adminStoragePickerPath = document.getElementById('admin-storage-picker-path');

      var adminStoragePickerRootBtn = document.getElementById('admin-storage-picker-root');

      var adminStoragePickerHomeBtn = document.getElementById('admin-storage-picker-home');

      var adminStoragePickerCancelBtn = document.getElementById('admin-storage-picker-cancel');

      var adminStoragePickerConfirmBtn = document.getElementById('admin-storage-picker-confirm');

      var statusBox = document.getElementById('status');

      var uploadProgress = document.getElementById('upload-progress');

      var uploadProgressFill = document.getElementById('upload-progress-fill');

      var uploadProgressText = document.getElementById('upload-progress-text');

      var uploadFolderPathInput = document.getElementById('upload-folder-path');

      var shell = document.querySelector('.shell');

      var sidebarToggleBtn = document.getElementById('sidebar-toggle-btn');

      var fileList = document.getElementById('file-list');

      var fileTable = document.getElementById('file-table');

      var fileEmpty = document.getElementById('file-empty');

      var fileCounter = document.getElementById('file-counter');

      var fileListTitle = document.getElementById('file-list-title');

      var tagViewModeBtns = document.getElementById('tag-view-mode-btns');

      var tagViewListBtn = document.getElementById('tag-view-list-btn');

      var tagViewPreviewBtn = document.getElementById('tag-view-preview-btn');

      var tagImagePreviewWrap = document.getElementById('tag-image-preview-wrap');

      var fileSelectAll = document.getElementById('file-select-all');

      var fileBulkTagAction = document.getElementById('file-bulk-tag-action');

      var fileBulkAction = document.getElementById('file-bulk-action');

      var fileBulkDeleteAction = document.getElementById('file-bulk-delete-action');

      var fileViewContext = document.getElementById('file-view-context');

      var remoteDiskShowHidden = document.getElementById('remote-disk-show-hidden');

      var localDiskContext = document.getElementById('local-disk-context');

      var localDiskHomeBtn = document.getElementById('local-disk-home-btn');

      var localDiskRootBtn = document.getElementById('local-disk-root-btn');

      var localDiskTrashBtn = document.getElementById('local-disk-trash-btn');

      var localDiskUpBtn = document.getElementById('local-disk-up-btn');

      var localDiskViewTableBtn = document.getElementById('local-disk-view-table-btn');

      var localDiskViewSplitBtn = document.getElementById('local-disk-view-split-btn');

      var localDiskImportBtn = document.getElementById('local-disk-import-btn');

      var localDiskShowHidden = document.getElementById('local-disk-show-hidden');

      var localDiskTableWrap = document.getElementById('local-disk-table-wrap');

      var localDiskTable = document.getElementById('local-disk-table');

      var localDiskList = document.getElementById('local-disk-list');

      var localDiskTableSelectAll = document.getElementById('local-disk-table-select-all');

      var localDiskTableBulkRemoveBtn = document.getElementById('local-disk-table-bulk-remove-btn');

      var localDiskExplorer = document.getElementById('local-disk-explorer');

      var localDiskDirResize = document.getElementById('local-disk-dir-resize');

      var localDiskDirList = document.getElementById('local-disk-dir-list');

      var localDiskDirEmpty = document.getElementById('local-disk-dir-empty');

      var localDiskSplitTable = document.getElementById('local-disk-split-table');

      var localDiskSplitList = document.getElementById('local-disk-split-list');

      var localDiskSplitEmpty = document.getElementById('local-disk-split-empty');

      var localDiskSelectAll = document.getElementById('local-disk-select-all');

      var localDiskBulkTagBtn = document.getElementById('local-disk-bulk-tag-btn');

      var localDiskBulkRemoveBtn = document.getElementById('local-disk-bulk-remove-btn');

      var localDiskTableBulkTagBtn = document.getElementById('local-disk-table-bulk-tag-btn');

      var localDiskEmpty = document.getElementById('local-disk-empty');

      var localSortButtons = Array.from(document.querySelectorAll('.local-sort-btn[data-local-sort-key]'));

      var explorerShell = document.querySelector('.explorer-shell');

      var folderBrowser = document.querySelector('.folder-browser');

      var folderTree = document.getElementById('folder-tree');

      var folderTreeEmpty = document.getElementById('folder-tree-empty');

      var folderCurrentPath = document.getElementById('folder-current-path');

      var folderCreateBtn = document.getElementById('folder-create-btn');

      var folderDeleteBtn = document.getElementById('folder-delete-btn');

      var folderRestoreBtn = document.getElementById('folder-restore-btn');

      var sortKey = document.getElementById('sort-key');

      var sortOrder = document.getElementById('sort-order');

      var sortButtons = Array.from(document.querySelectorAll('.sort-btn[data-sort-key]'));

      var leftTagTreeSection = document.querySelector('.left-tag-tree-section');

      var filesTagToggleBtn = document.getElementById('files-tag-toggle');

      var tagManager = document.getElementById('tag-manager');

      var tagDialog = document.getElementById('tag-dialog');

      var tagDialogForm = document.getElementById('tag-dialog-form');

      var tagDialogTitle = document.getElementById('tag-dialog-title');

      var tagDialogDesc = document.getElementById('tag-dialog-desc');

      var tagDialogLabel = document.getElementById('tag-dialog-label');

      var tagDialogInput = document.getElementById('tag-dialog-input');

      var tagDialogCancelBtn = document.getElementById('tag-dialog-cancel');

      var lockDialog = document.getElementById('lock-dialog');

      var lockDialogForm = document.getElementById('lock-dialog-form');

      var lockDialogTitle = document.getElementById('lock-dialog-title');

      var lockDialogDesc = document.getElementById('lock-dialog-desc');

      var lockDialogInput = document.getElementById('lock-dialog-input');

      var lockDialogError = document.getElementById('lock-dialog-error');

      var lockDialogCancelBtn = document.getElementById('lock-dialog-cancel');

      var lockDialogConfirmBtn = document.getElementById('lock-dialog-confirm');

      var confirmDialog = document.getElementById('confirm-dialog');

      var confirmDialogTitle = document.getElementById('confirm-dialog-title');

      var confirmDialogDesc = document.getElementById('confirm-dialog-desc');

      var confirmDialogCancelBtn = document.getElementById('confirm-dialog-cancel');

      var confirmDialogExtraBtn = document.getElementById('confirm-dialog-extra');

      var confirmDialogExtra2Btn = document.getElementById('confirm-dialog-extra2');

      var confirmDialogExtra3Btn = document.getElementById('confirm-dialog-extra3');

      var confirmDialogConfirmBtn = document.getElementById('confirm-dialog-confirm');

      var localImportDialog = document.getElementById('local-import-dialog');

      var localImportTree = document.getElementById('local-import-tree');

      var localImportEmpty = document.getElementById('local-import-empty');

      var localImportCancelBtn = document.getElementById('local-import-cancel');

      var localImportConfirmBtn = document.getElementById('local-import-confirm');

      var localImportProgressDialog = document.getElementById('local-import-progress-dialog');

      var localImportProgressFill = document.getElementById('local-import-progress-fill');

      var localImportProgressText = document.getElementById('local-import-progress-text');

      var localImportProgressFiles = document.getElementById('local-import-progress-files');

      var localImportProgressCancel = document.getElementById('local-import-progress-cancel');

      var localImportProgressClose = document.getElementById('local-import-progress-close');

      var localImportProgressMinimize = document.getElementById('local-import-progress-minimize');

      var localImportProgressRestore = document.getElementById('local-import-progress-restore');

      var previewLayer = document.getElementById('preview-layer');

      var menuButtons = Array.from(document.querySelectorAll('.menu-btn[data-panel]'));

      var panels = Array.from(document.querySelectorAll('.panel'));

      var SIDEBAR_COLLAPSED_STORAGE_KEY = 'webcool:sidebar-collapsed:v1';

      var TAG_TREE_STORAGE_KEY = 'webcool:file-tags:v1';

      var FOLDER_UNLOCK_SESSION_STORAGE_KEY = 'webcool:folder-unlocks:v1';

      var FILE_UNLOCK_SESSION_STORAGE_KEY = 'webcool:file-unlocks:v1';

      var LANGUAGE_STORAGE_KEY = 'webcool:language:v1';

      var UI_LANG = (document.documentElement.getAttribute('lang') || 'zh-CN').toLowerCase().indexOf('en') === 0
        ? 'en'
        : 'zh';

      var authState = {
        initialized: false,
        authenticated: false,
        username: '',
        admin: false,
        token: ''
      };

      var TAG_MAX_LEVEL = 3;

      var AUDIO_PLAY_MODE_LABELS = {
        random: t('随机播放'),
        sequential: t('顺序播放'),
        loop: t('循环播放')
      };

      var AUDIO_PLAY_MODE_ICONS = {
        random: '⤮',
        sequential: '⇥',
        loop: '↻'
      };

      var RECYCLE_FOLDER_NAME = '回收站';

      var allFiles = [];

      var activeSourceFiles = [];

      var currentFiles = [];

      var folderTreeData = [];

      var activeFolderPath = '';

      var activeLocalDiskPath = '';

      var activeLocalDiskParentPath = '/';

      var activeLocalDiskHomePath = '';

      var activeLocalDiskTrashPath = '';

      var activeLocalDiskItems = [];

      var localDiskSortKey = 'name';

      var localDiskSortOrder = 'asc';

      var localDiskViewMode = 'split';

      var selectedLocalDiskPaths = new Set();

      var activeLocalDiskDragPaths = [];

      var activeLocalDiskRenamePath = '';

      var localDiskRenameClickTimer = null;

      var localDiskClipboardPath = '';

      var localDiskClipboardDirectory = false;

      var localDiskClipboardPaths = [];

      var localDiskClipboardDirectoryFlags = [];

      var remoteDiskClipboardPath = '';

      var remoteDiskClipboardDirectory = false;

      var remoteDiskClipboardPaths = [];

      var remoteDiskClipboardDirectoryFlags = [];

      var activeLocalDiskTreeRootPath = '';

      var localImportTargetFolderPath = '';

      var localImportOverridePaths = null;

      var activeRemoteCopyTaskId = '';

      var activeRemoteCopyCancelRequested = false;

      var localImportProgressWindowMode = '';

      var localImportProgressMinimized = false;

      var localImportExpandedFolderPaths = new Set(['']);

      var localDiskTreeCache = new Map();

      var expandedLocalDiskTreePaths = new Set();

      var activeDropFolderPath = null;

      var activeFolderAutoExpandPath = '';

      var folderAutoExpandTimer = null;

      var activeFolderRenamePath = '';

      var folderRenameRequestPath = '';

      var activeFileRenamePath = '';

      var fileRenameRequestPath = '';

      var fileRenameClickTimer = null;

      var activeFolderContextMenu = null;

      var activeFileContextMenu = null;

      var unlockedFolderPasswords = new Map();

      var unlockedFilePasswords = new Map();

      var selectedFileNames = new Set();

      var selectedFolderPaths = new Set();

      var lastSelectedFolderPath = '';

      var expandedFolderPaths = new Set(['']);

      var tagTree = [];

      var activeFilterTagId = '';

      var tagFileViewMode = 'list';

      var currentAdminStoragePath = '';

      var adminStorageProgressTimer = null;

      var activeAdminStorageMigrateTaskId = '';

      var adminStoragePickerRootPath = '';

      var adminStoragePickerSelectedPath = '';

      var activeAdminStorageHomePath = '';

      var adminStoragePickerCache = new Map();

      var adminStoragePickerExpandedPaths = new Set();

      var expandedTagNodeIds = new Set();

      var activeTagMenuId = '';

      var activeDropTagNode = null;

      var activeTagRenameId = '';

      var tagRenameRequestId = '';

      var previewZ = 900;

      var activeTagPreviewImages = [];

      var activeDrag = null;

      var activeTagDialogResolver = null;

      var activeLockDialogState = null;

      var activeConfirmDialogResolver = null;

      var activeAudioTagContextMenu = null;

      var activeFileTagMenu = null;

      var openedPreviewWindows = new Map();

      var transcodeProgressTimers = new Map();

      var videoResumeSaveTimers = new Map();

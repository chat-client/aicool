function localDirLockIconHtml(path, locked) {
        const dirPath = String(path || '');
        if (!locked) {
          return '';
        }
        const unlocked = !!getLocalDirPassword(dirPath);
        return '<span class="folder-lock-icon file-lock-inline local-dir-lock-inline' + (unlocked ? ' unlocked' : '') + '" title="' + escapeHtml(t(unlocked ? '点击重新加锁' : '点击解锁')) + '" aria-label="' + escapeHtml(t(unlocked ? '点击重新加锁' : '点击解锁')) + '"><span class="folder-lock-shackle"></span><span class="folder-lock-body"></span></span>';
      }

      function askTagName(options) {
        if (!tagDialog || !tagDialogTitle || !tagDialogDesc || !tagDialogInput) {
          return Promise.resolve(null);
        }

        if (activeTagDialogResolver) {
          closeTagDialog(null);
        }

        const opts = options || {};
        tagDialogTitle.textContent = String(opts.title || t('新建标签'));
        tagDialogDesc.textContent = String(opts.description || t('请输入标签名称。'));
        if (tagDialogLabel) {
          tagDialogLabel.textContent = String(opts.label || t('标签名称'));
        }
        tagDialogInput.value = String(opts.initialValue || '');
        tagDialogInput.placeholder = String(opts.placeholder || t('请输入标签名称'));
        tagDialog.hidden = false;
        document.body.style.overflow = 'hidden';

        requestAnimationFrame(function () {
          tagDialogInput.focus();
          tagDialogInput.select();
        });

        return new Promise(function (resolve) {
          activeTagDialogResolver = resolve;
        });
      }

      function removeFileRefsFromAllTags(fileName) {
        return 0;
      }

      function buildLocalImportFolderTreeHtml(nodes, level) {
        const list = Array.isArray(nodes) ? nodes : [];
        return list.map(function (node) {
          if (!node) {
            return '';
          }
          const path = String(node.path || '');
          if (!path) {
            return buildLocalImportFolderTreeHtml(node.children, level);
          }
          if (isRecycleFolderPath(path)) {
            return '';
          }
          const expanded = localImportExpandedFolderPaths.has(path);
          const hasChildren = Array.isArray(node.children) && node.children.length > 0;
          const isActive = localImportTargetFolderPath === path;
          const padding = 10 + (Math.max(0, Number(level) || 0) * 18);
          const childHtml = hasChildren && expanded
            ? '<div class="folder-tree-children">' + buildLocalImportFolderTreeHtml(node.children, (level || 0) + 1) + '</div>'
            : '';
          const lockHtml = folderLockIconHtml(node);
          const encodedPath = escapeHtml(path);
          const createTitle = escapeHtml(t('新建子目录'));
          return (
            '<div class="folder-tree-node' + (isActive ? ' active' : '') + '" data-local-import-folder="' + encodedPath + '">' +
              '<div class="folder-tree-line" style="padding-left:' + padding + 'px;">' +
                (hasChildren
                  ? '<button type="button" class="folder-tree-toggle" data-local-import-toggle="' + encodedPath + '">' + (expanded ? '▾' : '▸') + '</button>'
                  : '<span class="folder-tree-toggle placeholder">•</span>') +
                '<div class="folder-tree-entry" data-local-import-select="' + encodedPath + '">' +
                  '<span class="folder-tree-name">' + escapeHtml(node.name || '') + '</span>' +
                  lockHtml +
                '</div>' +
                '<button type="button" class="local-import-create-inline" data-local-import-create="' + encodedPath + '" title="' + createTitle + '" aria-label="' + createTitle + '">+</button>' +
              '</div>' +
              childHtml +
            '</div>'
          );
        }).join('');
      }

      async function createLocalImportSubfolder(parentPath) {
        const targetPath = String(parentPath || '');
        if (!(await ensureFolderUnlocked(targetPath))) {
          return;
        }
        const name = await askTagName({
          title: t('新建子目录'),
          description: t('请输入要创建在「') + getFolderLabel(targetPath) + t('」下的子目录名称。'),
          label: t('目录名称'),
          placeholder: t('请输入目录名称')
        });
        if (name === null) {
          return;
        }
        const cleanName = String(name || '').trim();
        if (!cleanName) {
          showStatus(t('文件夹名称不能为空'), 'err');
          return;
        }
        await fetchJson(
          withFolderPassword(
            api.folderCreate + '?parent=' + encodeURIComponent(targetPath) + '&name=' + encodeURIComponent(cleanName),
            targetPath
          ),
          { method: 'POST' }
        );
        const newPath = targetPath ? (targetPath + '/' + cleanName) : cleanName;
        localImportTargetFolderPath = newPath;
        ensureFolderPathExpanded(newPath);
        ensureLocalImportFolderPathExpanded(newPath);
        await loadFolderTreeState();
        renderFolderTree();
        renderLocalImportTree();
        showStatus(t('已创建文件夹：') + cleanName, 'ok');
      }

      function handleFileNameClick(filePath, local) {
        const file = String(filePath || '');
        if (!file) {
          return;
        }
        const alreadySelected = selectedFileNames.has(file);
        if (!alreadySelected) {
          clearFileRenameClickTimer();
          selectedFileNames.clear();
          selectedFileNames.add(file);
          activeFileRenamePath = '';
          fileRenameRequestPath = '';
          refreshRenderedFileSelection();
          return;
        }
        clearFileRenameClickTimer();
        fileRenameClickTimer = window.setTimeout(function () {
          fileRenameClickTimer = null;
          startFileRename(file);
        }, 230);
      }

      async function submitLocalDiskFileRename(input) {
        if (!input || input.disabled) {
          return;
        }
        const oldPath = decodeURIComponent(input.getAttribute('data-local-disk-rename-path') || '');
        const nextName = String(input.value || '').trim();
        const item = getLocalDiskItemByPath(oldPath);
        if (!oldPath || !item || item.directory) {
          cancelLocalDiskFileRename();
          return;
        }
        const currentName = String(item.name || localBaseName(oldPath) || '');
        if (!nextName || nextName === currentName) {
          cancelLocalDiskFileRename();
          return;
        }
        input.disabled = true;
        resetStatus();
        try {
          let url = api.localDiskRename + '?path=' + encodeURIComponent(oldPath) + '&name=' + encodeURIComponent(nextName);
          url = appendLocalDirPassword(appendFilePassword(url, oldPath, true), localDiskParentPath(oldPath));
          const result = await fetchJson(url, { method: 'POST' });
          const newPath = String((result && result.path) || '');
          selectedLocalDiskPaths.delete(oldPath);
          if (newPath) {
            selectedLocalDiskPaths.add(newPath);
          }
          activeLocalDiskRenamePath = '';
          await loadLocalDisk(activeLocalDiskPath || localDiskParentPath(oldPath), {
            resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, activeLocalDiskPath || localDiskParentPath(oldPath))
          });
          showStatus(t('文件已改名：') + currentName + ' -> ' + nextName, 'ok');
        } catch (err) {
          input.disabled = false;
          showStatus(t('文件改名失败：') + err.message, 'err');
          selectRenameInputText(input);
        }
      }

      function applyLanguageSelection(select, showSavedStatus) {
        if (!select) {
          return;
        }
        const nextLang = select.value === 'en' ? 'en' : 'zh';
        if (!authState.authenticated) {
          try {
            localStorage.setItem(LANGUAGE_STORAGE_KEY, nextLang);
          } catch (_) {}
        }
        if (nextLang === UI_LANG) {
          if (showSavedStatus) {
            showStatus(t('语言设置已保存'), 'ok');
          }
          if (window.WebCoolI18n && typeof window.WebCoolI18n.apply === 'function') {
            window.WebCoolI18n.apply(document);
          }
          if (typeof updateRemoteUploadTargetHints === 'function') {
            updateRemoteUploadTargetHints();
          }
          if (typeof renderRemoteUploadStagingPanel === 'function') {
            renderRemoteUploadStagingPanel();
          }
          return;
        }
        window.location.href = localizedPageUrl(nextLang);
      }

      function normalizeUiLanguage(value) {
        return value === 'en' ? 'en' : 'zh';
      }

      function parseUserPrefsPayload(data) {
        return {
          ui_language: normalizeUiLanguage(data && data.ui_language),
          font_size: normalizeFontSize(data && data.font_size)
        };
      }

      function readCachedUserPrefs(username) {
        try {
          const raw = sessionStorage.getItem(USER_PREFS_SESSION_KEY);
          if (!raw) {
            return null;
          }
          const cached = JSON.parse(raw);
          if (!cached || cached.username !== String(username || '').trim()) {
            return null;
          }
          return parseUserPrefsPayload(cached);
        } catch (_) {}
        return null;
      }

      function cacheUserPrefsSession(username, prefs) {
        const user = String(username || '').trim();
        if (!user) {
          try {
            sessionStorage.removeItem(USER_PREFS_SESSION_KEY);
          } catch (_) {}
          return;
        }
        const normalized = parseUserPrefsPayload(prefs);
        try {
          sessionStorage.setItem(USER_PREFS_SESSION_KEY, JSON.stringify({
            username: user,
            ui_language: normalized.ui_language,
            font_size: normalized.font_size
          }));
        } catch (_) {}
      }

      function getCurrentUserPrefs() {
        if (authState.userPrefs) {
          return authState.userPrefs;
        }
        const cached = readCachedUserPrefs(authState.username);
        if (cached) {
          return cached;
        }
        return {
          ui_language: UI_LANG,
          font_size: 'sm'
        };
      }

      function applyUserPreferences(prefs, options) {
        const normalized = parseUserPrefsPayload(prefs);
        authState.userPrefs = normalized;
        applyFontSizePreference(normalized.font_size);
        if (adminLanguageSelect) {
          adminLanguageSelect.value = normalized.ui_language;
        }
        if (authLanguageSelect) {
          authLanguageSelect.value = normalized.ui_language;
        }
        if (authState.authenticated) {
          try {
            localStorage.setItem(LANGUAGE_STORAGE_KEY, normalized.ui_language);
          } catch (_) {}
        }
        const opts = options || {};
        if (opts.redirectLanguage && normalized.ui_language !== UI_LANG) {
          window.location.replace(localizedPageUrl(normalized.ui_language));
          return true;
        }
        return false;
      }

      async function saveUserPreferencesToServer(partial) {
        const params = new URLSearchParams();
        if (partial.ui_language) {
          params.set('ui_language', normalizeUiLanguage(partial.ui_language));
        }
        if (partial.font_size) {
          params.set('font_size', normalizeFontSize(partial.font_size));
        }
        const url = api.authPreferences + '?' + params.toString();
        const data = await fetchJson(url, { method: 'POST' });
        const prefs = parseUserPrefsPayload(data);
        authState.userPrefs = prefs;
        cacheUserPrefsSession(authState.username, prefs);
        return prefs;
      }

      function applyUserPreferencesFromAuthData(data) {
        if (!data || !authState.authenticated) {
          authState.userPrefs = null;
          return;
        }
        const prefs = parseUserPrefsPayload(data);
        cacheUserPrefsSession(authState.username, prefs);
        applyUserPreferences(prefs, { redirectLanguage: true });
      }

      async function applyLanguageSetting() {
        if (!adminLanguageSelect) {
          return;
        }
        const nextLang = adminLanguageSelect.value === 'en' ? 'en' : 'zh';
        try {
          if (authState.authenticated) {
            await saveUserPreferencesToServer({ ui_language: nextLang });
          }
        } catch (err) {
          showStatus(t('保存语言设置失败：') + err.message, 'err');
          return;
        }
        applyLanguageSelection(adminLanguageSelect, true);
      }

      function normalizeFontSize(value) {
        return value === 'md' || value === 'lg' ? value : 'sm';
      }

      function fontScaleForSize(size) {
        if (size === 'lg') {
          return 1.24;
        }
        if (size === 'md') {
          return 1.12;
        }
        return 1;
      }

      function loadFontSizePreference(username) {
        const cached = readCachedUserPrefs(username);
        if (cached) {
          return cached.font_size;
        }
        if (authState.username === username && authState.userPrefs) {
          return authState.userPrefs.font_size;
        }
        return 'sm';
      }

      function applyFontSizePreference(size) {
        const normalized = normalizeFontSize(size);
        const scale = fontScaleForSize(normalized);
        document.documentElement.style.setProperty('--font-scale', scale.toFixed(3));
        document.documentElement.setAttribute('data-font-size', normalized);
        if (adminFontSizeSelect) {
          adminFontSizeSelect.value = normalized;
        }
      }

      function syncFontSizePreferenceForCurrentUser() {
        const prefs = getCurrentUserPrefs();
        applyFontSizePreference(prefs.font_size);
        if (adminLanguageSelect) {
          adminLanguageSelect.value = prefs.ui_language;
        }
        if (adminFontSizeSelect) {
          adminFontSizeSelect.value = prefs.font_size;
        }
      }

      function rememberCurrentUserSession(username) {
        const user = String(username || '').trim();
        try {
          if (user) {
            sessionStorage.setItem(CURRENT_USER_SESSION_KEY, user);
          } else {
            sessionStorage.removeItem(CURRENT_USER_SESSION_KEY);
          }
        } catch (_) {}
      }

      async function applyFontSizeSetting() {
        const nextSize = adminFontSizeSelect ? adminFontSizeSelect.value : 'sm';
        try {
          if (authState.authenticated) {
            await saveUserPreferencesToServer({ font_size: nextSize });
          }
          applyFontSizePreference(nextSize);
          showStatus(t('字体设置已保存'), 'ok');
        } catch (err) {
          showStatus(t('保存字体设置失败：') + err.message, 'err');
        }
      }

      function fileLockKey(path, local) {
        return (local ? 'local:' : 'remote:') + String(path || '');
      }

      function normalizeFolderMoveSources(paths) {
        const sorted = (Array.isArray(paths) ? paths : [])
          .map(function (path) { return String(path || ''); })
          .filter(Boolean)
          .sort(function (a, b) {
            if (a.length !== b.length) {
              return a.length - b.length;
            }
            return a.localeCompare(b, 'zh-CN');
          });

        const result = [];
        sorted.forEach(function (path) {
          const covered = result.some(function (picked) {
            return isSameOrChildFolderPath(picked, path);
          });
          if (!covered) {
            result.push(path);
          }
        });
        return result;
      }

      function normalizeTagNode(node, level) {
        if (!node || level > TAG_MAX_LEVEL) {
          return null;
        }

        const name = String(node.name || '').trim();
        if (!name) {
          return null;
        }

        const files = Array.isArray(node.files)
          ? node.files.map(function (it) { return String(it || ''); }).filter(Boolean)
          : [];

        const childrenInput = Array.isArray(node.children) ? node.children : [];
        const children = [];
        for (let i = 0; i < childrenInput.length; i += 1) {
          const normalized = normalizeTagNode(childrenInput[i], level + 1);
          if (normalized) {
            children.push(normalized);
          }
        }

        return {
          id: String(node.id || makeTagId()),
          name: name.slice(0, 60),
          files: Array.from(new Set(files)),
          locked: !!node.locked,
          children: children
        };
      }

      function setTranscodeTaskId(encodedName, taskId) {
        const item = statusBox.querySelector('[data-transcode-item="' + encodedName + '"]');
        if (!item) {
          return;
        }
        if (taskId) {
          item.setAttribute('data-task-id', taskId);
        } else {
          item.removeAttribute('data-task-id');
        }
      }

      function setLocalImportProgressWindowMode(mode) {
        const nextMode = String(mode || '');
        if (nextMode !== localImportProgressWindowMode) {
          localImportProgressMinimized = false;
        }
        localImportProgressWindowMode = nextMode;
        applyLocalImportProgressWindowState();
      }

      function buildImageGalleryFromFiles(files) {
        return (Array.isArray(files) ? files : []).filter(function (file) {
          return file && !file.directory && isImageName(file.name || getFilePath(file));
        }).map(function (file) {
          const rawName = getFilePath(file);
          return {
            file: rawName,
            name: String(file.name || rawName || ''),
            local: !!file.local
          };
        }).filter(function (item) {
          return !!item.file;
        });
      }

      function getSelectedLocalDiskDirPaths() {
        return getSelectedLocalDiskPaths().filter(function (path) {
          return isLocalDiskDirectoryPath(path);
        });
      }

      function pollAdminStorageMigration(taskId) {
        stopAdminStorageProgressPolling();
        activeAdminStorageProgressMode = 'migrate';
        activeAdminStorageMigrateTaskId = String(taskId || '');
        setAdminStorageProgressControls('running');
        return new Promise(function (resolve, reject) {
          let conflictPrompting = false;
          let resolvedConflictKey = '';
          let rememberedConflictAction = '';
          async function resolveStorageMigrationConflict(data) {
            const conflictKey = String(data.conflict_source || data.conflict_target || data.conflict_name || '');
            if (conflictPrompting || (conflictKey && conflictKey === resolvedConflictKey)) {
              return;
            }
            conflictPrompting = true;
            let action = rememberedConflictAction;
            const name = String(data.conflict_name || data.conflict_target || '');
            if (!action) {
              const choice = await askConfirmDialog({
                title: t('目标文件已存在'),
                description: t('目标目录下已存在同名文件：') + name,
                confirmText: t('记住覆盖'),
                extraText: t('不覆盖'),
                extraValue: 'skip',
                extraButtons: [
                  { text: t('记住不覆盖'), value: 'remember-skip' },
                  { text: t('覆盖'), value: 'overwrite' }
                ],
                cancelText: t('取消迁移'),
                wide: true,
                danger: false
              });
              if (choice === true || choice === 'remember-overwrite') {
                rememberedConflictAction = 'overwrite';
                action = 'remember-overwrite';
              } else if (choice === 'remember-skip') {
                rememberedConflictAction = 'skip';
                action = 'remember-skip';
              } else {
                action = choice === 'overwrite' ? 'overwrite' : (choice === 'skip' ? 'skip' : 'cancel');
              }
            }
            await fetchJson(
              api.adminStorageMigrateResolve + '?task_id=' + encodeURIComponent(taskId || '') + '&choice=' + encodeURIComponent(action),
              { method: 'POST' }
            );
            resolvedConflictKey = conflictKey;
            conflictPrompting = false;
          }
          async function tick() {
            try {
              const data = await fetchJson(
                api.adminStorageMigrateProgress + '?task_id=' + encodeURIComponent(taskId || '')
              );
              const state = String(data.state || '');
              setAdminStorageProgressControls(state);
              if (state !== 'conflict') {
                conflictPrompting = false;
                resolvedConflictKey = '';
              }
              const message = localizeAdminStorageMigrationMessage(data.message || '');
              const movedFiles = Number(data.moved_files || 0);
              const totalFiles = Number(data.total_files || 0);
              const detail = message + (totalFiles > 0 ? ('（' + movedFiles + '/' + totalFiles + '）') : '');
              setAdminStorageProgress(true, Number(data.progress || 0), detail);
              if (state === 'conflict') {
                const conflictPath = String(data.conflict_source || data.conflict_target || data.conflict_name || '');
                const conflictText = rememberedConflictAction === 'overwrite'
                  ? (t('正在处理同名文件(覆盖)：') + conflictPath)
                  : (rememberedConflictAction === 'skip'
                    ? (t('正在处理同名文件(跳过)：') + conflictPath)
                    : (t('正在处理同名文件：') + conflictPath));
                setAdminStorageProgress(true, Number(data.progress || 0), conflictText);
                await resolveStorageMigrationConflict(data);
              } else if (state === 'paused') {
                setAdminStorageProgress(true, Number(data.progress || 0), message || t('迁移已暂停'));
              } else if (state === 'done') {
                stopAdminStorageProgressPolling();
                setAdminStorageProgress(true, 100, t('迁移完成'));
                resolve(data);
              } else if (state === 'failed' || state === 'cancelled') {
                stopAdminStorageProgressPolling();
                reject(new Error(data.error || (state === 'cancelled' ? t('迁移已取消') : t('存储路径迁移失败'))));
              }
            } catch (err) {
              stopAdminStorageProgressPolling();
              reject(err);
            }
          }
          adminStorageProgressTimer = setInterval(tick, 800);
          tick();
        });
      }

      function pollAdminStorageBackupSync(taskId) {
        stopAdminStorageProgressPolling();
        activeAdminStorageProgressMode = 'backup';
        activeAdminStorageMigrateTaskId = String(taskId || '');
        setAdminStorageProgressControls('running');
        return new Promise(function (resolve, reject) {
          let conflictPrompting = false;
          let resolvedConflictKey = '';
          let rememberedConflictAction = '';
          async function resolveBackupSyncConflict(data) {
            const conflictKey = String(data.conflict_source || data.conflict_target || data.conflict_name || '');
            if (conflictPrompting || (conflictKey && conflictKey === resolvedConflictKey)) {
              return;
            }
            conflictPrompting = true;
            let action = rememberedConflictAction;
            const name = String(data.conflict_name || data.conflict_target || '');
            if (!action) {
              const choice = await askConfirmDialog({
                title: t('备份文件冲突'),
                description: t('备份目录下已存在同名但内容不同的文件：') + name,
                confirmText: t('覆盖'),
                extraText: t('跳过'),
                extraValue: 'skip',
                extraButtons: [
                  { text: t('记住跳过'), value: 'remember-skip' },
                  { text: t('记住覆盖'), value: 'remember-overwrite' }
                ],
                cancelText: t('停止备份'),
                wide: true,
                danger: false
              });
              if (choice === true || choice === 'overwrite') {
                action = 'overwrite';
              } else if (choice === 'remember-overwrite') {
                rememberedConflictAction = 'overwrite';
                action = 'remember-overwrite';
              } else if (choice === 'remember-skip') {
                rememberedConflictAction = 'skip';
                action = 'remember-skip';
              } else {
                action = choice === 'skip' ? 'skip' : 'cancel';
              }
            }
            await fetchJson(
              api.adminStorageBackupSyncResolve + '?task_id=' + encodeURIComponent(taskId || '') + '&choice=' + encodeURIComponent(action),
              { method: 'POST' }
            );
            resolvedConflictKey = conflictKey;
            conflictPrompting = false;
          }
          async function tick() {
            try {
              const data = await fetchJson(
                api.adminStorageBackupSyncProgress + '?task_id=' + encodeURIComponent(taskId || '')
              );
              const state = String(data.state || '');
              setAdminStorageProgressControls(state);
              if (state !== 'conflict') {
                conflictPrompting = false;
                resolvedConflictKey = '';
              }
              const message = localizeAdminStorageMigrationMessage(data.message || '');
              const movedFiles = Number(data.moved_files || 0);
              const totalFiles = Number(data.total_files || 0);
              const detail = message + (totalFiles > 0 ? ('（' + movedFiles + '/' + totalFiles + '）') : '');
              setAdminStorageProgress(true, Number(data.progress || 0), detail);
              if (state === 'conflict') {
                const conflictPath = String(data.conflict_source || data.conflict_target || data.conflict_name || '');
                setAdminStorageProgress(true, Number(data.progress || 0), t('正在处理备份文件冲突：') + conflictPath);
                await resolveBackupSyncConflict(data);
              } else if (state === 'paused') {
                setAdminStorageProgress(true, Number(data.progress || 0), message || t('备份已暂停'));
              } else if (state === 'done') {
                stopAdminStorageProgressPolling();
                setAdminStorageProgress(true, 100, t('备份同步完成'));
                resolve(data);
              } else if (state === 'failed' || state === 'cancelled') {
                stopAdminStorageProgressPolling();
                reject(new Error(data.error || (state === 'cancelled' ? t('备份已停止') : t('备份同步失败'))));
              }
            } catch (err) {
              stopAdminStorageProgressPolling();
              reject(err);
            }
          }
          adminStorageProgressTimer = setInterval(tick, 800);
          tick();
        });
      }

      function localDirLockKey(path) {
        return 'local-dir:' + String(path || '');
      }

      function getVisibleFolderPathsInOrder() {
        const result = [''];
        function walk(nodes) {
          (Array.isArray(nodes) ? nodes : []).forEach(function (node) {
            const path = String((node && node.path) || '');
            if (!path) {
              if (expandedFolderPaths.has('')) {
                walk(node && node.children);
              }
              return;
            }
            result.push(path);
            if (expandedFolderPaths.has(path)) {
              walk(node.children);
            }
          });
        }
        walk(getRootFolderTreeNodesForRender());
        return result;
      }

      async function loadTagTreeState() {
        try {
          const data = await fetchJson(api.tags);
          const parsed = Array.isArray(data.tags) ? data.tags : [];
          let source = parsed;

          if (!source.length) {
            const legacyNodes = loadLegacyTagTreeState();
            if (legacyNodes.length) {
              await migrateLegacyTagTree(legacyNodes);
              const refreshed = await fetchJson(api.tags);
              source = Array.isArray(refreshed.tags) ? refreshed.tags : [];
            }
          }

          const normalized = [];
          for (let i = 0; i < source.length; i += 1) {
            const node = normalizeTagNode(source[i], 1);
            if (node) {
              normalized.push(node);
            }
          }
          tagTree = normalized;
        } catch (_) {
          tagTree = [];
        }
      }

      function getTranscodeTaskId(encodedName) {
        const item = statusBox.querySelector('[data-transcode-item="' + encodedName + '"]');
        return item ? (item.getAttribute('data-task-id') || '') : '';
      }

      function minimizeLocalImportProgressWindow() {
        if (!isCopyTaskWindowMode(localImportProgressWindowMode)) {
          return;
        }
        localImportProgressMinimized = true;
        updateLocalImportProgressSummary(localImportProgressText ? localImportProgressText.textContent : '');
        applyLocalImportProgressWindowState();
      }

      function imageGalleryIndexOf(gallery, filePath) {
        const target = String(filePath || '');
        const index = (Array.isArray(gallery) ? gallery : []).findIndex(function (item) {
          return String((item && item.file) || '') === target;
        });
        return index >= 0 ? index : 0;
      }

      function getSelectedLocalDiskImportPaths() {
        return getSelectedLocalDiskPaths();
      }

      async function askCleanupOldStorageAfterMigration(data) {
        const sourceDir = String((data && data.source_dir) || '');
        if (!sourceDir) {
          return;
        }
        const cleanup = await askConfirmDialog({
          title: t('是否删除旧数据？'),
          description: t('迁移已完成，是否删除原存储路径下的旧数据？备份目录 .backup 会保留。'),
          confirmText: t('删除旧数据'),
          cancelText: t('不删除'),
          danger: true
        });
        if (!cleanup) {
          showStatus(t('已保留旧数据：') + sourceDir, 'warn');
          return;
        }
        await fetchJson(api.adminStorageMigrateCleanup + '?task_id=' + encodeURIComponent(String((data && data.task_id) || '')), { method: 'POST' });
        showStatus(t('已删除旧数据：') + sourceDir, 'ok');
      }

      function deleteUnlockedLocalDirPassword(path) {
        unlockedFilePasswords.delete(localDirLockKey(path));
        saveUnlockedFilePasswords();
      }

      function setActivePanel(panelId) {
        panels.forEach(function (panel) {
          panel.classList.toggle('active', panel.id === panelId);
        });
        menuButtons.forEach(function (btn) {
          btn.classList.toggle('active', btn.getAttribute('data-panel') === panelId);
        });
        if (leftTagTreeSection) {
          leftTagTreeSection.hidden = false;
        }
      }

      function buildQuickTagTreeHtml(nodes, fileNames, level) {
        const list = Array.isArray(nodes) ? nodes : [];
        const names = Array.isArray(fileNames) ? fileNames : [];
        const safeLevel = Math.max(1, Math.min(TAG_MAX_LEVEL, level || 1));
        return list.map(function (node) {
          const tagId = String((node && node.id) || '');
          const name = String((node && node.name) || '');
          const displayName = getRestrictedRootTagType(node, safeLevel) ? t(name) : name;
          const check = canBindFilesToTagOnClient(tagId, names);
          const children = Array.isArray(node && node.children) ? node.children : [];
          const childHtml = children.length
            ? '<div class="quick-tag-children">' + buildQuickTagTreeHtml(children, names, safeLevel + 1) + '</div>'
            : '';
          const createChildHtml = safeLevel < TAG_MAX_LEVEL
            ? '<button type="button" class="quick-tag-create-child" data-quick-tag-create-child="' + escapeHtml(tagId) + '" data-quick-tag-name="' + escapeHtml(displayName) + '" title="' + escapeHtml(t('新建子标签并加入')) + '" aria-label="' + escapeHtml(t('新建子标签并加入')) + '">+</button>'
            : '<span class="quick-tag-create-placeholder"></span>';
          return (
            '<div class="quick-tag-node">' +
              '<div class="quick-tag-row">' +
                '<button type="button" class="quick-tag-item' + (check.ok ? '' : ' disabled') + '" data-quick-tag-id="' + escapeHtml(tagId) + '"' + (check.ok ? '' : ' disabled title="' + escapeHtml(check.message) + '"') + ' style="padding-left:' + (8 + ((safeLevel - 1) * 14)) + 'px;">' +
                  '<span class="quick-tag-mark">#</span>' +
                  '<span class="quick-tag-name">' + escapeHtml(displayName) + '</span>' +
                '</button>' +
                createChildHtml +
              '</div>' +
              childHtml +
            '</div>'
          );
        }).join('');
      }

      async function cancelManualTranscode(encodedName) {
        const taskId = getTranscodeTaskId(encodedName);
        if (!taskId) {
          dismissTranscodeItem(encodedName);
          return;
        }

        try {
          setTranscodeButtons(encodedName, { cancelDisabled: true });
          await fetchJson(api.convertCancel + '?task_id=' + encodeURIComponent(taskId), {
            method: 'POST'
          });
          setTranscodeVisualState(encodedName, 'cancelled');
          updateTranscodeProgress(encodedName, 0, t('取消中'));
          setTranscodeReason(encodedName, t('状态：已发送取消请求，等待后台停止'));
        } catch (err) {
          setTranscodeVisualState(encodedName, 'failed');
          setTranscodeReason(encodedName, t('状态：取消失败，') + err.message);
          setTranscodeButtons(encodedName, { cancelDisabled: false });
        }
      }

      function setCopyTaskProgress(mode, progress, text, item) {
        const title = document.getElementById('local-import-progress-title');
        const desc = document.getElementById('local-import-progress-desc');
        setLocalImportProgressWindowMode(mode);
        if (localImportProgressDialog) {
          localImportProgressDialog.classList.add('non-modal-progress');
        }
        if (title) { title.textContent = t('粘贴中'); }
        if (desc && !localImportProgressMinimized) { desc.textContent = getCopyTaskProgressDescription(); }
        setLocalImportProgress(progress, text);
        if (localImportProgressCancel) {
          localImportProgressCancel.hidden = false;
          localImportProgressCancel.disabled = !!activeRemoteCopyCancelRequested;
          localImportProgressCancel.textContent = activeRemoteCopyCancelRequested ? t('取消中...') : t('取消');
        }
        renderLocalImportProgressFiles(item ? [item] : []);
      }

      function buildLocalDiskImageGallery() {
        return (Array.isArray(activeLocalDiskItems) ? activeLocalDiskItems : []).slice()
          .sort(compareLocalDiskItems)
          .filter(function (item) {
            return item && !item.directory && isImageName(item.name || item.path);
          }).map(function (item) {
            const path = String(item.path || '');
            return {
              file: path,
              name: String(item.name || localDiskBaseName(path)),
              local: true
            };
          }).filter(function (item) {
            return !!item.file;
          });
      }

      function removeLocalDiskTreePaths(paths) {
        const moved = new Set((Array.isArray(paths) ? paths : []).map(function (path) {
          return String(path || '');
        }).filter(Boolean));
        if (!moved.size) {
          return;
        }
        localDiskTreeCache.forEach(function (dirs, key) {
          localDiskTreeCache.set(key, dirs.filter(function (item) {
            return !moved.has(String((item && item.path) || ''));
          }));
        });
        moved.forEach(function (path) {
          localDiskTreeCache.delete(path);
          expandedLocalDiskTreePaths.delete(path);
        });
      }

      function jumpAdminStoragePickerHome() {
        jumpAdminStoragePickerPath(activeAdminStorageHomePath || '');
      }

      function getFilePath(file) {
        return String((file && file.path) || (file && file.name) || '');
      }

      function isVideoPlayable(fileName, timeoutMs) {
        return new Promise(function (resolve) {
          const encoded = encodeURIComponent(fileName || '');
          const src = downloadUrlForFile(fileName || '', true);
          const video = document.createElement('video');
          let done = false;

          function cleanup(ok, reason) {
            if (done) {
              return;
            }
            done = true;
            video.pause();
            video.removeAttribute('src');
            video.load();
            resolve({ ok: ok, reason: reason || '' });
          }

          const timer = setTimeout(function () {
            cleanup(false, t('加载元数据超时'));
          }, timeoutMs || 8000);

          video.preload = 'metadata';
          video.muted = true;
          video.playsInline = true;

          video.onloadedmetadata = function () {
            clearTimeout(timer);
            cleanup(true);
          };

          video.onerror = function () {
            clearTimeout(timer);
            cleanup(false, t('浏览器不支持该视频编码或容器'));
          };

          video.src = src;
        });
      }

      function clearDropHighlight() {
        if (!activeDropTagNode) {
          return;
        }
        activeDropTagNode.classList.remove('drop-target');
        activeDropTagNode = null;
      }

      function formatNumber(num) {
        return String(Number(num) || 0).replace(/\B(?=(\d{3})+(?!\d))/g, ',');
      }

      function formatByteSpeed(bytesPerSecond) {
        let value = Math.max(0, Number(bytesPerSecond) || 0);
        const units = ['B/s', 'KB/s', 'MB/s', 'GB/s', 'TB/s'];
        let unitIndex = 0;
        while (value >= 1024 && unitIndex < units.length - 1) {
          value /= 1024;
          unitIndex += 1;
        }
        const digits = value >= 100 || unitIndex === 0 ? 0 : (value >= 10 ? 1 : 2);
        return value.toFixed(digits).replace(/\.0+$/, '').replace(/(\.\d*[1-9])0+$/, '$1') + ' ' + units[unitIndex];
      }

      function appendSpeedText(text, bytesPerSecond) {
        const speed = Number(bytesPerSecond) || 0;
        if (speed <= 0) {
          return String(text || '');
        }
        return String(text || '') + ' · ' + t('速度：') + formatByteSpeed(speed);
      }

      async function showFilesForTag(tagId) {
        activeFilterTagId = tagId;
        tagFileViewMode = 'list';
        setActivePanel('panel-files');
        const data = await fetchJson(appendTagPassword(api.tagFiles + '?tag_id=' + encodeURIComponent(tagId), tagId));
        renderFiles(Array.isArray(data.files) ? data.files.map(normalizeFileRecord) : []);
        renderTagTree();
      }

      function drawImageElementToCanvas(img) {
        const canvas = document.createElement('canvas');
        canvas.width = img.naturalWidth || img.width;
        canvas.height = img.naturalHeight || img.height;
        const ctx = canvas.getContext('2d');
        ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
        return canvas;
      }

      function resetLocalDiskTreeRoot(path) {
        activeLocalDiskTreeRootPath = String(path || '/');
        localDiskTreeCache.clear();
        expandedLocalDiskTreePaths.clear();
        expandedLocalDiskTreePaths.add(activeLocalDiskTreeRootPath);
      }

      function bindVideoResume(video, fileName) {
        const key = String(fileName || '');
        if (!video || !key || isMovVideoName(key)) {
          return;
        }

        video.setAttribute('data-resume-file', key);
        let restored = false;
        let lastSavedSec = -1;

        video.addEventListener('loadedmetadata', function () {
          video.setAttribute('data-resume-media-ready', '1');
          if (restored) {
            return;
          }
          restored = true;

          loadVideoResumePosition(key).then(function (resume) {
            if (!resume.found || resume.positionMs <= 0) {
              return;
            }

            const seekSec = resume.positionMs / 1000.0;
            const duration = Number(video.duration);
            if (!Number.isFinite(duration) || duration <= 1) {
              video.currentTime = seekSec;
              return;
            }

            if (seekSec >= duration - 0.6) {
              return;
            }

            video.currentTime = Math.max(0, Math.min(seekSec, duration - 0.5));
          }).catch(function () {});
        });

        video.addEventListener('timeupdate', function () {
          if (video.getAttribute('data-resume-closing') === '1'
            || video.getAttribute('data-resume-media-ready') !== '1') {
            return;
          }

          const sec = Math.floor(Number(video.currentTime) || 0);
          if (sec < 0) {
            return;
          }

          if (lastSavedSec >= 0 && Math.abs(sec - lastSavedSec) < 3) {
            return;
          }

          lastSavedSec = sec;
          scheduleSaveVideoResumePosition(key, sec * 1000);
        });

        video.addEventListener('pause', function () {
          if (video.getAttribute('data-resume-closing') === '1'
            || video.getAttribute('data-resume-media-ready') !== '1') {
            return;
          }
          const sec = Math.floor(Number(video.currentTime) || 0);
          scheduleSaveVideoResumePosition(key, Math.max(0, sec * 1000));
        });

        video.addEventListener('ended', function () {
          if (video.getAttribute('data-resume-closing') === '1'
            || video.getAttribute('data-resume-media-ready') !== '1') {
            return;
          }
          scheduleSaveVideoResumePosition(key, 0);
        });
      }

      function relocatePathAfterFolderMove(path, sourcePath, targetPath) {
        const current = String(path || '');
        const source = String(sourcePath || '');
        const target = String(targetPath || '');
        if (!current || !source || !target) {
          return current;
        }
        if (current === source) {
          return target;
        }
        if (current.indexOf(source + '/') === 0) {
          return target + current.slice(source.length);
        }
        return current;
      }

      function escapeHtml(str) {
        return String(str)
          .replace(/&/g, '&amp;')
          .replace(/</g, '&lt;')
          .replace(/>/g, '&gt;')
          .replace(/"/g, '&quot;')
          .replace(/'/g, '&#39;');
      }

      function isTagPreviewEnabled(tagId) {
        const id = String(tagId || '');
        if (!id) {
          return false;
        }
        const meta = findTagMetaById(id);
        if (!meta || !meta.node) {
          return false;
        }
        const rootName = getTagRootName(id);
        // 规则：图片标签(含子标签)可预览；标牌仅其子标签可预览（不含标牌根）。
        if (rootName === '图片') {
          return true;
        }
        if (rootName === '标牌' && Number(meta.level || 1) > 1) {
          return true;
        }
        return false;
      }

      function scheduleFolderAutoExpand(path) {
        const targetPath = String(path || '');
        if (!targetPath || expandedFolderPaths.has(targetPath)) {
          clearFolderAutoExpandTimer();
          return;
        }
        const node = findFolderNodeByPath(targetPath);
        const hasChildren = !!(node && Array.isArray(node.children) && node.children.length);
        if (!hasChildren) {
          clearFolderAutoExpandTimer();
          return;
        }
        if (activeFolderAutoExpandPath === targetPath && folderAutoExpandTimer) {
          return;
        }
        clearFolderAutoExpandTimer();
        activeFolderAutoExpandPath = targetPath;
        folderAutoExpandTimer = setTimeout(function () {
          folderAutoExpandTimer = null;
          if (!activeFolderAutoExpandPath) {
            return;
          }
          expandedFolderPaths.add(activeFolderAutoExpandPath);
          renderFolderTree();
          scrollFolderPathIntoView(activeFolderAutoExpandPath);
          activeFolderAutoExpandPath = '';
        }, 600);
      }

      function summarizeSelectedFileTypes(fileNames) {
        const names = Array.isArray(fileNames) ? fileNames : [];
        let folderCount = 0;
        let fileCount = 0;
        names.forEach(function (name) {
          const item = getFileRecordByPath(name);
          if (item && item.directory) {
            folderCount += 1;
          } else {
            fileCount += 1;
          }
        });
        const label = folderCount > 0 && fileCount > 0
          ? t('文件/文件夹')
          : (folderCount > 0 ? t('文件夹') : t('文件'));
        return {
          fileCount: fileCount,
          folderCount: folderCount,
          label: label
        };
      }

      function applyPreviewImageManualSize(win) {
        const widthInput = win ? win.querySelector('.preview-size-input[data-image-size="width"]') : null;
        const heightInput = win ? win.querySelector('.preview-size-input[data-image-size="height"]') : null;
        const width = Math.round(Number(widthInput && widthInput.value));
        const height = Math.round(Number(heightInput && heightInput.value));
        if (!width || !height || width < 1 || height < 1) {
          setImageEditHint(win, '请输入有效的图片宽度和高度。', true);
          return;
        }
        const base = previewBaseCanvas(win);
        if (base) {
          win.__imageScale = width / base.width;
        }
        renderPreviewImageFromBase(win, width, height, '已按输入尺寸调整至 ' + width + ' x ' + height + '。');
      }

      async function loadLocalDisk(path, options) {
        const opts = options || {};
        const target = typeof path === 'string' ? path : (activeLocalDiskPath || '');
        try {
          const showHidden = localDiskShowHidden && localDiskShowHidden.checked;
          let url = target
            ? (api.localDiskList + '?path=' + encodeURIComponent(target) + (showHidden ? '&show_hidden=1' : ''))
            : (api.localDiskList + (showHidden ? '?show_hidden=1' : ''));
          if (target) {
            url = appendLocalDirPassword(url, target);
          }
          const data = await fetchJson(url);
          activeLocalDiskPath = String(data.path || '/');
          activeLocalDiskParentPath = String(data.parent_path || '/');
          activeLocalDiskHomePath = String(data.home_path || activeLocalDiskHomePath || '');
          activeLocalDiskTrashPath = String(data.trash_path || activeLocalDiskTrashPath || '');
          if (opts.resetTreeRoot) {
            resetLocalDiskTreeRoot(activeLocalDiskPath);
          } else {
            ensureLocalDiskTreeRoot(activeLocalDiskPath);
          }
          cacheLocalDiskTreeNode(activeLocalDiskPath, Array.isArray(data.items) ? data.items : []);
          expandedLocalDiskTreePaths.add(activeLocalDiskPath);
          clearLocalDiskSelection();
          renderLocalDiskItems(Array.isArray(data.items) ? data.items : []);
          return true;
        } catch (err) {
          const message = String((err && err.message) || err || '');
          const lockedPath = String((err && err.data && err.data.path) || target || '');
          if (opts.fallbackToParentOnLocked !== false && lockedPath && /directory is locked/i.test(message)) {
            const parent = localDiskParentPath(lockedPath);
            if (parent && parent !== lockedPath) {
              return await loadLocalDisk(parent, { resetTreeRoot: true });
            }
          }
          if (opts.preserveOnError) {
            if (localDiskPathInput) localDiskPathInput.value = activeLocalDiskPath || '/';
            showStatus(t('跳转本地路径失败：') + message, 'err');
            return false;
          }
          if (localDiskList) {
            localDiskList.innerHTML = '';
          }
          if (localDiskTable) {
            localDiskTable.style.display = 'none';
          }
          if (localDiskEmpty) {
            localDiskEmpty.textContent = '加载本地磁盘失败：' + message;
            localDiskEmpty.style.display = 'block';
          }
          showStatus('加载本地磁盘失败：' + message, 'err');
          return false;
        }
      }

      function handleLocalDiskFileRenameKeydownEvent(e) {
        const input = e.target.closest('.local-disk-rename-input');
        if (!input) {
          return;
        }
        if (e.key === 'Enter') {
          e.preventDefault();
          submitLocalDiskFileRename(input);
        } else if (e.key === 'Escape') {
          e.preventDefault();
          cancelLocalDiskFileRename();
        }
      }

      function buildFolderListParams() {
        const entries = Array.from(unlockedFolderPasswords.entries()).filter(function (entry) {
          return entry[0] && entry[1];
        });
        const params = new URLSearchParams();
        if (remoteDiskShowHidden && remoteDiskShowHidden.checked) {
          params.set('show_hidden', '1');
        }
        if (entries.length) {
          params.set('unlock_count', String(entries.length));
          entries.forEach(function (entry, index) {
            params.set('unlock_path_' + index, entry[0]);
            params.set('unlock_password_' + index, entry[1]);
          });
        }
        return params;
      }

      function folderListUrl() {
        const params = buildFolderListParams();
        const query = params.toString();
        return api.folders + (query ? ('?' + query) : '');
      }

      function fetchFolderList(folderPath) {
        const params = buildFolderListParams();
        const folder = String(folderPath || '');
        if (folder) {
          params.set('folder', folder);
        }
        const query = params.toString();
        const hasUnlockPayload = params.has('unlock_count');
        if (hasUnlockPayload) {
          return fetchJson(api.folders, {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
            body: query
          });
        }
        return fetchJson(api.folders + (query ? ('?' + query) : ''));
      }

      function openAudioTagContextMenu(tagId, tagName, clientX, clientY, lockInfo) {
        closeAudioTagContextMenu();

        const menu = document.createElement('div');
        menu.className = 'tag-context-menu file-context-menu';

        let html =
          '<button type="button" class="tag-context-item" data-audio-tag-action="random" data-tag-id="' + escapeHtml(tagId) + '">' + AUDIO_PLAY_MODE_LABELS.random + '</button>' +
          '<button type="button" class="tag-context-item" data-audio-tag-action="sequential" data-tag-id="' + escapeHtml(tagId) + '">' + AUDIO_PLAY_MODE_LABELS.sequential + '</button>' +
          '<button type="button" class="tag-context-item" data-audio-tag-action="loop" data-tag-id="' + escapeHtml(tagId) + '">' + AUDIO_PLAY_MODE_LABELS.loop + '</button>';
        if (lockInfo) {
          html += '<hr class="tag-context-sep">';
          if (lockInfo.locked) {
            const unlocked = !!getTagPassword(tagId);
            html += '<button type="button" class="folder-context-item" data-tag-lock-action="' + (unlocked ? 'session-lock' : 'session-unlock') + '">' + t(unlocked ? '加锁' : '解锁') + '</button>';
            html += '<button type="button" class="folder-context-item" data-tag-lock-action="remove-lock">' + t('去锁') + '</button>';
          } else {
            html += '<button type="button" class="folder-context-item" data-tag-lock-action="lock">' + t('加锁') + '</button>';
          }
          menu.setAttribute('data-tag-lock-id', tagId);
          activeFileContextMenu = menu;
        }
        menu.innerHTML = html;
        menu.setAttribute('data-tag-id', tagId);
        menu.setAttribute('data-tag-name', tagName || '');
        menu.style.left = Math.round(clientX) + 'px';
        menu.style.top = Math.round(clientY) + 'px';
        document.body.appendChild(menu);
        clampFloatingMenuPosition(menu, clientX, clientY);
        activeAudioTagContextMenu = menu;
      }

      async function addTagNode(parentTagId, name) {
        const cleanName = String(name || '').trim();
        if (!cleanName) {
          return { ok: false, message: t('标签名称不能为空') };
        }

        try {
          const created = await fetchJson(
            api.tagCreate + '?name=' + encodeURIComponent(cleanName)
              + (parentTagId ? '&parent_id=' + encodeURIComponent(parentTagId) : ''),
            { method: 'POST' }
          );
          return { ok: true, id: created.id || '' };
        } catch (err) {
          return { ok: false, message: err.message };
        }
      }

      function cancelFolderRename() {
        if (!activeFolderRenamePath) {
          return;
        }
        activeFolderRenamePath = '';
        renderFolderTree();
      }

      function cancelFileRename() {
        activeFileRenamePath = '';
        fileRenameRequestPath = '';
        clearFileRenameClickTimer();
        renderFiles(activeSourceFiles);
      }

      function getLocalDiskItemByPath(path) {
        const target = String(path || '');
        return activeLocalDiskItems.find(function (item) {
          return String((item && item.path) || '') === target;
        }) || null;
      }

      function togglePreviewWindowMaximize(win) {
        if (!win) {
          return;
        }
        if (win.classList.contains('is-minimized') || win.classList.contains('is-maximized')) {
          restorePreviewWindowGeometry(win);
          return;
        }
        snapshotPreviewWindowRect(win);
        win.classList.remove('is-minimized');
        win.classList.add('is-maximized');
        win.style.transform = 'none';
        win.style.resize = 'none';
        win.style.left = '22px';
        win.style.top = '22px';
        win.style.width = 'calc(100vw - 44px)';
        win.style.height = 'calc(100vh - 44px)';
        win.style.maxHeight = 'calc(100vh - 44px)';
        syncPreviewWindowButtons(win);
        window.setTimeout(function () {
          fitPreviewImageToWindow(win);
        }, 0);
      }

      function handleLocalDiskDragLeave(e) {
        if (!localDiskExplorer || localDiskExplorer.contains(e.relatedTarget)) {
          return;
        }
        clearLocalDiskDropTarget();
      }

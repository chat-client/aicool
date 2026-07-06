function downloadUrlForFile(filePath, preview) {
        const encoded = encodeURIComponent(filePath || '');
        let url = api.download + '?' + (preview ? 'preview=1&' : '') + 'file=' + encoded;
        url = withFolderPassword(url, parentFolderPathFromFilePath(filePath));
        return appendFilePassword(url, filePath, false);
      }

      function openTagLockContextMenu(tagId, locked, clientX, clientY) {
        closeFileContextMenu();
        closeFolderContextMenu();
        closeAudioTagContextMenu();

        const id = String(tagId || '');
        if (!id) {
          return;
        }

        const menu = document.createElement('div');
        menu.className = 'folder-context-menu file-context-menu';
        menu.setAttribute('data-tag-lock-id', id);
        let html = '';
        if (locked) {
          const unlocked = !!getTagPassword(id);
          html += '<button type="button" class="folder-context-item" data-tag-lock-action="' + (unlocked ? 'session-lock' : 'session-unlock') + '">' + t(unlocked ? '加锁' : '解锁') + '</button>';
          html += '<button type="button" class="folder-context-item" data-tag-lock-action="remove-lock">' + t('去锁') + '</button>';
        } else {
          html += '<button type="button" class="folder-context-item" data-tag-lock-action="lock">' + t('加锁') + '</button>';
        }
        menu.innerHTML = html;
        document.body.appendChild(menu);
        menu.style.left = Math.round(clientX) + 'px';
        menu.style.top = Math.round(clientY) + 'px';
        clampFloatingMenuPosition(menu, clientX, clientY);
        activeFileContextMenu = menu;
      }

      async function migrateLegacyTagNode(node, parentTagId) {
        const createResult = await addTagNode(parentTagId, node.name || '');
        if (!createResult.ok || !createResult.id) {
          throw new Error(createResult.message || t('创建标签失败'));
        }

        const serverTagId = createResult.id;
        const files = Array.isArray(node.files) ? node.files : [];
        for (let i = 0; i < files.length; i += 1) {
          const fileName = String(files[i] || '');
          if (!fileName) {
            continue;
          }
          await bindFileToTag(serverTagId, fileName);
        }

        const children = Array.isArray(node.children) ? node.children : [];
        for (let i = 0; i < children.length; i += 1) {
          await migrateLegacyTagNode(children[i], serverTagId);
        }
      }

      async function submitFolderRename(input) {
        if (!input) {
          return;
        }
        const oldPath = String(input.getAttribute('data-folder-rename-input') || '');
        if (!canRenameFolderPath(oldPath) || activeFolderRenamePath !== oldPath) {
          return;
        }
        if (folderRenameRequestPath === oldPath) {
          return;
        }
        const nextName = String(input.value || '').trim();
        const oldName = folderNameFromPath(oldPath);
        if (!nextName) {
          showStatus(t('文件夹名称不能为空'), 'err');
          input.focus();
          return;
        }
        if (nextName === oldName) {
          activeFolderRenamePath = '';
          renderFolderTree();
          return;
        }

        try {
          folderRenameRequestPath = oldPath;
          const result = await fetchJson(
            withFolderPassword(api.folderRename + '?path=' + encodeURIComponent(oldPath) + '&name=' + encodeURIComponent(nextName), oldPath),
            { method: 'POST' }
          );
          const newPath = String((result && result.path) || '');
          activeFolderRenamePath = '';
          activeFolderPath = relocatePathAfterFolderMove(activeFolderPath, oldPath, newPath);
          ensureFolderParentPathExpanded(activeFolderPath);
          await loadFiles();
          showStatus(t('文件夹已改名：') + nextName, 'ok');
        } catch (err) {
          showStatus(t('文件夹改名失败：') + err.message, 'err');
          selectRenameInputText(input);
        } finally {
          folderRenameRequestPath = '';
        }
      }

      async function submitFileRename(input) {
        if (!input || input.disabled) {
          return;
        }
        const oldPath = decodeURIComponent(input.getAttribute('data-file-rename-path') || '');
        const nextName = String(input.value || '').trim();
        const record = getFileRecordByPath(oldPath);
        if (!oldPath || !record || !canRenameFileRecord(record)) {
          cancelFileRename();
          return;
        }
        const currentName = String(record.name || oldPath.split('/').pop() || '');
        if (!nextName || nextName === currentName) {
          cancelFileRename();
          return;
        }
        input.disabled = true;
        resetStatus();
        try {
          let url = api.fileRename + '?file=' + encodeURIComponent(oldPath) + '&name=' + encodeURIComponent(nextName);
          url = withFolderPassword(url, parentFolderPathFromFilePath(oldPath));
          url = appendFilePassword(url, oldPath, false);
          const result = await fetchJson(url, { method: 'POST' });
          const newPath = String((result && result.path) || '');
          selectedFileNames.delete(oldPath);
          if (newPath) {
            selectedFileNames.add(newPath);
          }
          activeFileRenamePath = '';
          fileRenameRequestPath = '';
          if (activeFilterTagId) {
            await showFilesForTag(activeFilterTagId);
          } else {
            await loadFiles();
          }
          showStatus(t('文件已改名：') + currentName + ' -> ' + nextName, 'ok');
        } catch (err) {
          input.disabled = false;
          showStatus(t('文件改名失败：') + err.message, 'err');
          selectRenameInputText(input);
        }
      }

      function getLocalDiskTreeItemByPath(path) {
        const target = String(path || '');
        let found = null;
        localDiskTreeCache.forEach(function (dirs) {
          if (found) {
            return;
          }
          found = dirs.find(function (item) {
            return String((item && item.path) || '') === target;
          }) || null;
        });
        return found;
      }

      function minimizePreviewWindow(win) {
        if (!win || win.classList.contains('is-minimized')) {
          return;
        }
        snapshotPreviewWindowRect(win);
        win.classList.remove('is-maximized');
        win.classList.add('is-minimized');
        win.style.transform = 'none';
        win.style.resize = 'none';
        win.style.height = '';
        win.style.maxHeight = '';
        clampWindowPosition(win);
        syncPreviewWindowButtons(win);
      }

      async function handleLocalDiskDrop(e) {
        const target = getLocalDiskDropTarget(e);
        if (!target || !activeLocalDiskDragPaths.length || activeLocalDiskDragPaths.indexOf(target.path) >= 0) {
          return;
        }
        e.preventDefault();
        clearLocalDiskDropTarget();
        const movePaths = activeLocalDiskDragPaths.slice();
        activeLocalDiskDragPaths = [];
        try {
          await Promise.all(movePaths.map(function (path) {
            const sourceLockPath = getLocalDirPassword(path) ? path : localDiskParentPath(path);
            return fetchJson(
              appendLocalDirPassword(
                appendLocalDirPassword(appendFilePassword(api.localDiskMove
                  + '?path=' + encodeURIComponent(path)
                  + '&target=' + encodeURIComponent(target.path), path, true), sourceLockPath),
                target.path,
                'target_local_dir_password'
              ),
              { method: 'POST' }
            );
          }));
          showStatus('已移动 ' + movePaths.length + ' 个项目到：' + target.path, 'ok');
          clearLocalDiskSelection();
          removeLocalDiskTreePaths(movePaths);
          localDiskTreeCache.delete(target.path);
          loadLocalDisk(target.path, { resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, target.path) });
        } catch (err) {
          showStatus('移动失败：' + err.message, 'err');
          loadLocalDisk(activeLocalDiskPath || '', { resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, activeLocalDiskPath || '') });
        }
      }

      function openLocalDirContextMenu(path, locked, clientX, clientY) {
        closeFileContextMenu();
        closeFolderContextMenu();
        const dirPath = String(path || '');
        if (!dirPath || dirPath === '/') {
          return;
        }
        const unlocked = !!getLocalDirPassword(dirPath);
        const menu = document.createElement('div');
        menu.className = 'folder-context-menu file-context-menu';
        menu.setAttribute('data-local-dir-path', dirPath);
        menu.setAttribute('data-local-dir-locked', locked ? '1' : '0');
        const selectedDirs = getSelectedLocalDiskDirPaths();
        const actionDirs = selectedDirs.indexOf(dirPath) >= 0 ? selectedDirs : [dirPath];
        const isMultiDir = actionDirs.length > 1;
        menu.setAttribute('data-local-dir-paths', actionDirs.join('\n'));
        let html = '';
        if (!isMultiDir && localDiskClipboardPaths.length) {
          html += '<button type="button" class="folder-context-item" data-local-dir-menu-action="paste">' + t('粘贴') + '</button>';
        }
        html += '<button type="button" class="folder-context-item" data-local-dir-menu-action="copy">' + t('拷贝') + '</button>';
        if (!isMultiDir) {
          html += '<button type="button" class="folder-context-item" data-local-dir-menu-action="create">' + t('新建子目录') + '</button>';
        }
        html += '<button type="button" class="folder-context-item" data-local-dir-menu-action="upload">' + t('上传') + '</button>';
        if (!isMultiDir) {
          html += '<button type="button" class="folder-context-item" data-local-dir-menu-action="rename">' + t('改名') + '</button>';
        }
        html += '<button type="button" class="folder-context-item" data-local-dir-menu-action="delete">' + t('删除') + '</button>';
        if (!isMultiDir && locked) {
          html += '<button type="button" class="folder-context-item" data-local-dir-menu-action="' + (unlocked ? 'session-lock' : 'session-unlock') + '">' + t(unlocked ? '加锁' : '解锁') + '</button>';
          html += '<button type="button" class="folder-context-item" data-local-dir-menu-action="remove-lock">' + t('去锁') + '</button>';
        } else if (!isMultiDir) {
          html += '<button type="button" class="folder-context-item" data-local-dir-menu-action="lock">' + t('加锁') + '</button>';
        }
        menu.innerHTML = html;
        document.body.appendChild(menu);
        menu.style.left = Math.round(clientX) + 'px';
        menu.style.top = Math.round(clientY) + 'px';
        clampFloatingMenuPosition(menu, clientX, clientY);
        activeFileContextMenu = menu;
      }

      function setLockDialogError(message) {
        if (!lockDialogError) {
          return;
        }
        const text = String(message || '');
        lockDialogError.textContent = text;
        lockDialogError.hidden = !text;
      }

      function pruneTagRefsByCurrentFiles() {
        const available = getFileNameSet();
        let changed = false;
        walkTagNodes(tagTree, function (node) {
          const before = node.files.length;
          node.files = node.files.filter(function (it) {
            return available.has(it);
          });
          if (before !== node.files.length) {
            changed = true;
          }
          return false;
        }, 1, null, tagTree);
        return changed;
      }

      function renderLocalImportTree() {
        if (!localImportTree || !localImportEmpty) {
          return;
        }
        const rootActive = localImportTargetFolderPath === '';
        const body = buildLocalImportFolderTreeHtml(getRootFolderTreeNodesForRender(), 0);
        localImportTree.innerHTML =
          '<div class="folder-tree-node' + (rootActive ? ' active' : '') + '" data-local-import-folder="">' +
            '<div class="folder-tree-line" style="padding-left:10px;">' +
              '<span class="folder-tree-toggle placeholder">•</span>' +
              '<div class="folder-tree-entry" data-local-import-select="">' +
                '<span class="folder-tree-name">' + escapeHtml(t('根目录')) + '</span>' +
              '</div>' +
              '<button type="button" class="local-import-create-inline" data-local-import-create="" title="' + escapeHtml(t('新建子目录')) + '" aria-label="' + escapeHtml(t('新建子目录')) + '">+</button>' +
            '</div>' +
          '</div>' +
          body;
        localImportEmpty.style.display = body ? 'none' : 'block';
      }

      function handleFileNameDoubleClick(filePath, local) {
        clearFileRenameClickTimer();
        activeFileRenamePath = '';
        fileRenameRequestPath = '';
        const file = String(filePath || '');
        if (file) {
          selectedFileNames.clear();
          selectedFileNames.add(file);
          refreshRenderedFileSelection();
          downloadRemoteListFile(file, !!local);
        }
      }

      function handleLocalDiskFileNameClick(path) {
        const filePath = String(path || '');
        if (!filePath) {
          return;
        }
        if (!selectedLocalDiskPaths.has(filePath)) {
          clearLocalDiskRenameClickTimer();
          selectedLocalDiskPaths.clear();
          selectedLocalDiskPaths.add(filePath);
          activeLocalDiskRenamePath = '';
          refreshRenderedLocalDiskSelection();
          return;
        }
        clearLocalDiskRenameClickTimer();
        localDiskRenameClickTimer = window.setTimeout(function () {
          localDiskRenameClickTimer = null;
          startLocalDiskFileRename(filePath);
        }, 230);
      }

      function redirectToSavedLanguageIfNeeded() {
        let savedLang = '';
        let hasUserPrefs = false;
        try {
          const user = sessionStorage.getItem(CURRENT_USER_SESSION_KEY) || '';
          const raw = sessionStorage.getItem(USER_PREFS_SESSION_KEY);
          if (user && raw) {
            const cached = JSON.parse(raw);
            if (cached && cached.username === user
              && (cached.ui_language === 'en' || cached.ui_language === 'zh')) {
              savedLang = cached.ui_language;
              hasUserPrefs = true;
            }
          }
          if (!hasUserPrefs) {
            savedLang = localStorage.getItem(LANGUAGE_STORAGE_KEY) || '';
          }
        } catch (_) {}
        if (savedLang !== 'en' && savedLang !== 'zh') {
          return;
        }
        const path = window.location.pathname || '';
        if (savedLang !== UI_LANG) {
          window.location.replace(localizedPageUrl(savedLang));
        }
      }

function saveUnlockedFolderPasswords() {
        try {
          const entries = Array.from(unlockedFolderPasswords.entries()).filter(function (entry) {
            return entry[0] && entry[1];
          });
          sessionStorage.setItem(FOLDER_UNLOCK_SESSION_STORAGE_KEY, JSON.stringify(entries));
        } catch (_) {}
      }

      function openFolderContextMenu(path, clientX, clientY) {
        closeFolderContextMenu();
        const folderPath = String(path || '');
        const isRoot = folderPath === '';
        const isSharedRoot = isSharedRootFolderPath(folderPath);
        const isReservedFixed = isReservedFixedFolderPath(folderPath);
        const isRecycleRoot = isRecycleRootFolderPath(folderPath);
        const node = isRoot ? { locked: false }
          : (findFolderNodeByPath(folderPath) || (isReservedFixed ? { locked: false } : null));
        if (!node && !isRecycleRoot && !isSharedRoot && !isReservedFixed) {
          return;
        }
        const menu = document.createElement('div');
        menu.className = 'folder-context-menu';
        menu.setAttribute('data-folder-path', folderPath);
        if (isRoot || isSharedRoot) {
          menu.innerHTML =
            (isRoot && remoteDiskClipboardPath ? '<button type="button" class="folder-context-item" data-folder-menu-action="paste">' + t('粘贴') + '</button>' : '') +
            '<button type="button" class="folder-context-item" data-folder-menu-action="create">' + t('新建子目录') + '</button>';
          document.body.appendChild(menu);
          menu.style.left = Math.round(clientX) + 'px';
          menu.style.top = Math.round(clientY) + 'px';
          clampFloatingMenuPosition(menu, clientX, clientY);
          activeFolderContextMenu = menu;
          return;
        }
        if (isRecycleRoot) {
          menu.innerHTML = '<button type="button" class="folder-context-item danger" data-folder-menu-action="empty-recycle">' + t('清空') + '</button>';
          document.body.appendChild(menu);
          menu.style.left = Math.round(clientX) + 'px';
          menu.style.top = Math.round(clientY) + 'px';
          clampFloatingMenuPosition(menu, clientX, clientY);
          activeFolderContextMenu = menu;
          return;
        }
        if (!canRenameFolderPath(folderPath) && !isReservedFixed) {
          return;
        }
        const lockActionsHtml = node.locked
          ? '<button type="button" class="folder-context-item" data-folder-menu-action="' + (unlockedFolderPasswords.has(folderPath) ? 'session-lock' : 'session-unlock') + '">' + t(unlockedFolderPasswords.has(folderPath) ? '加锁' : '解锁') + '</button>' +
            '<button type="button" class="folder-context-item" data-folder-menu-action="remove-lock">' + t('去锁') + '</button>'
          : '<button type="button" class="folder-context-item" data-folder-menu-action="lock">' + t('加锁') + '</button>';
        menu.innerHTML =
          (remoteDiskClipboardPath ? '<button type="button" class="folder-context-item" data-folder-menu-action="paste">' + t('粘贴') + '</button>' : '') +
          '<button type="button" class="folder-context-item" data-folder-menu-action="copy">' + t('拷贝') + '</button>' +
          '<button type="button" class="folder-context-item" data-folder-menu-action="create">' + t('新建子目录') + '</button>' +
          (isReservedFixed ? '' : '<button type="button" class="folder-context-item" data-folder-menu-action="delete">' + t('删除') + '</button>' +
            '<button type="button" class="folder-context-item" data-folder-menu-action="rename">' + t('改名') + '</button>') +
          lockActionsHtml;
        document.body.appendChild(menu);
        menu.style.left = Math.round(clientX) + 'px';
        menu.style.top = Math.round(clientY) + 'px';
        clampFloatingMenuPosition(menu, clientX, clientY);
        activeFolderContextMenu = menu;
      }

      function closeLockDialog(value) {
        if (!lockDialog) {
          return;
        }
        lockDialog.hidden = true;
        document.body.style.overflow = '';
        setLockDialogError('');
        const state = activeLockDialogState;
        activeLockDialogState = null;
        if (state && state.resolve) {
          state.resolve(value);
        }
      }

      function isAudioSplitChoiceCandidate(item) {
        return !!(item && item.allowAudioSplitChoice);
      }

      async function openLocalImportDialog(pathsOverride) {
        pendingBrowserUploadFiles = [];
        const paths = Array.isArray(pathsOverride)
          ? pathsOverride.map(function (path) { return String(path || ''); }).filter(Boolean)
          : getSelectedLocalDiskImportPaths();
        if (!paths.length) {
          showStatus(t('请先选择要上传的本地文件或文件夹'), 'err');
          return;
        }
        localImportOverridePaths = Array.isArray(pathsOverride) ? paths : null;
        try {
          await loadFolderTreeState();
        } catch (err) {
          localImportOverridePaths = null;
          showStatus(t('加载远程目录失败：') + err.message, 'err');
          return;
        }
        localImportTargetFolderPath = activeFolderPath || '';
        ensureLocalImportFolderPathExpanded(localImportTargetFolderPath);
        const title = document.getElementById('local-import-title');
        const desc = document.getElementById('local-import-desc');
        if (title) {
          title.textContent = t('上传到虚拟磁盘');
        }
        if (desc) {
          desc.textContent = t('选择远程目标目录。');
        }
        renderLocalImportTree();
        if (localImportDialog) {
          localImportDialog.hidden = false;
        }
      }

      function renderFiles(files) {
        activeSourceFiles = (Array.isArray(files) ? files : []).map(normalizeFileRecord);
        if (activeFilterTagId) {
          currentFiles = activeSourceFiles.slice();
        } else {
          currentFiles = activeSourceFiles.filter(function (file) {
            return String(file.folder_path || '') === String(activeFolderPath || '');
          });
        }
        syncSelectedFilesByCurrentView();
        updateExplorerLayout();
        updateFileViewContext();
        updateFileSelectAllState();
        updateFileBulkActionButton();
        fileCounter.textContent = currentFiles.length + t(' 个文件');

        if (!currentFiles.length) {
          fileList.innerHTML = '';
          fileTable.style.display = 'none';
          if (tagImagePreviewWrap) tagImagePreviewWrap.hidden = true;
          fileEmpty.textContent = activeFilterTagId ? t('当前标签下没有文件。') : t('当前目录没有文件。');
          fileEmpty.style.display = 'block';
          return;
        }

        const key = sortKey.value || 'name';
        const order = sortOrder.value || 'asc';
        const isTagFilterMode = !!activeFilterTagId;
        const sorted = currentFiles.slice().sort((a, b) => compareFiles(a, b, key, order));
        updateSortIndicator();
        const isRecycleMode = !isTagFilterMode && isRecycleFolderPath(activeFolderPath);
        if (fileTable && fileTable.classList) {
          fileTable.classList.toggle('recycle-mode', isRecycleMode);
        }

        fileEmpty.style.display = 'none';

        if (tagFileViewMode === 'preview' && isActiveTagPreviewEnabled()) {
          fileTable.style.display = 'none';
          renderTagImagePreview(sorted);
          updateFileSelectAllState();
          updateFileBulkActionButton();
          return;
        }

        if (tagImagePreviewWrap) tagImagePreviewWrap.hidden = true;
        fileTable.style.display = 'table';
        fileList.innerHTML = sorted.map(file => {
          const name = escapeHtml(file.name || '');
          const rawName = getFilePath(file);
          const encodedPath = encodeURIComponent(rawName);
          const isLocalTaggedFile = !!file.local;
          const isDir = !!file.directory;
          const fileLocked = !!file.locked;
          const lockIcon = fileLocked
            ? '<span class="folder-lock-icon file-lock-inline' + (getFilePassword(rawName, isLocalTaggedFile) ? ' unlocked' : '') + '" title="' + escapeHtml(t(getFilePassword(rawName, isLocalTaggedFile) ? '点击重新加锁' : '点击解锁')) + '" aria-label="' + escapeHtml(t(getFilePassword(rawName, isLocalTaggedFile) ? '点击重新加锁' : '点击解锁')) + '"><span class="folder-lock-shackle"></span><span class="folder-lock-body"></span></span>'
            : '';
          const pathMeta = file.folder_path
            ? '<div class="file-path-meta">' + escapeHtml(file.folder_path) + '</div>'
            : '';
          const size = safeSize(file);
          const uploaded = escapeHtml(file.uploaded_time || '-');
          const checked = selectedFileNames.has(rawName) ? ' checked' : '';
          const selectedClass = selectedFileNames.has(rawName) ? ' selected-file-row' : '';
          const previewBtn = !isDir && isImageName(file.name)
            ? (isLocalTaggedFile
              ? '<button class="local-preview-btn preview-btn" data-kind="image" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(rawName) + '">' + t('预览') + '</button>'
              : '<button class="preview-btn" data-preview-file="' + encodedPath + '" data-preview-name="' + escapeHtml(rawName) + '">' + t('预览') + '</button>')
            : '';
          const videoBtn = !isDir && isVideoName(file.name)
            ? (isLocalTaggedFile
              ? '<button class="local-preview-btn video-btn" data-kind="video" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(rawName) + '">' + t('观影') + '</button>'
              : '<button class="video-btn" data-video-file="' + encodedPath + '" data-video-name="' + escapeHtml(rawName) + '">' + t('观影') + '</button>')
            : '';
          const audioBtn = !isDir && isAudioName(file.name)
            ? (isLocalTaggedFile
              ? '<button class="local-preview-btn audio-btn" data-kind="audio" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(rawName) + '">' + t('听音') + '</button>'
              : '<button class="audio-btn" data-audio-file="' + encodedPath + '" data-audio-name="' + escapeHtml(rawName) + '">' + t('听音') + '</button>')
            : '';
          const textBtn = !isDir && isTextName(file.name)
            ? (isLocalTaggedFile
              ? '<button class="local-preview-btn text-btn" data-kind="text" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(rawName) + '">' + t('查看') + '</button>'
              : '<button class="text-btn" data-text-file="' + encodedPath + '" data-text-name="' + escapeHtml(rawName) + '">' + t('查看') + '</button>')
            : '';
          const pdfBtn = !isDir && isPdfName(file.name)
            ? (isLocalTaggedFile
              ? '<button class="local-preview-btn preview-btn" data-kind="pdf" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(rawName) + '">' + t('预览') + '</button>'
              : '<button class="pdf-btn preview-btn" data-pdf-file="' + encodedPath + '" data-pdf-name="' + escapeHtml(rawName) + '">' + t('预览') + '</button>')
            : '';
          const officeBtn = !isDir && !isLocalTaggedFile && isOfficeName(file.name)
            ? '<button class="office-btn preview-btn" data-office-file="' + encodedPath + '" data-office-name="' + escapeHtml(rawName) + '">' + t('预览') + '</button>'
            : '';
          const primaryActionBtn = isTagFilterMode
            ? '<button class="delete-btn" data-file="' + encodedPath + '" data-name="' + escapeHtml(rawName) + '"' + (isLocalTaggedFile ? ' data-local-tag-file="1"' : '') + '>' + t('移除') + '</button>'
            : (isRecycleMode
              ? ('<button class="restore-btn" data-file="' + encodedPath + '" data-name="' + escapeHtml(rawName) + '">' + t('恢复') + '</button>')
              : '<button class="delete-btn" data-file="' + encodedPath + '" data-name="' + escapeHtml(rawName) + '">' + t('删除') + '</button>');
          const permanentDeleteBtn = isRecycleMode
            ? '<button class="delete-btn" data-file="' + encodedPath + '" data-name="' + escapeHtml(rawName) + '">' + t('彻底删除') + '</button>'
            : '';
          const isRenamingFile = !isDir && activeFileRenamePath === rawName;
          const renameInput = isRenamingFile
            ? '<input class="file-rename-input" type="text" draggable="false" value="' + escapeHtml(file.name || '') + '" data-file-rename-path="' + encodedPath + '" aria-label="' + escapeHtml(t('改名文件')) + '">'
            : '';
          const nameContent = isDir
            ? '<span class="file-name local-folder-link"><span class="local-folder-icon">📁</span>' + name + '</span>'
            : (renameInput || '<button type="button" class="file-name file-name-action" draggable="false" data-file-name-click="' + encodedPath + '" data-file-local="' + (isLocalTaggedFile ? '1' : '0') + '">' + name + '</button>');
          const tagQuickBtn = isDir
            ? ''
            : '<button class="file-tag-quick-btn" type="button" data-tag-file="' + encodedPath + '" title="' + escapeHtml(t('加入标签')) + '" aria-label="' + escapeHtml(t('加入标签')) + '">🏷</button>';
          return (
            '<tr class="draggable-file-row' + selectedClass + '" draggable="' + ((isDir || isRenamingFile) ? 'false' : 'true') + '" data-drag-file="' + encodedPath + '"' + (isDir ? '' : ' data-file-context="' + encodedPath + '"') + ' data-file-local="' + (isLocalTaggedFile ? '1' : '0') + '" data-file-locked="' + (fileLocked ? '1' : '0') + '" data-file-video="' + (!isDir && isVideoName(file.name) ? '1' : '0') + '">' +
              '<td class="file-select-cell"><div class="file-select-tools"><input class="file-select-input" type="checkbox" data-select-file="' + encodedPath + '" aria-label="' + escapeHtml(t('选择') + (isDir ? t('目录 ') : t('文件 ')) + rawName) + '"' + checked + '>' + tagQuickBtn + '</div></td>' +
              '<td' + (isDir ? '' : ' data-file-context="' + encodedPath + '"') + ' data-file-local="' + (isLocalTaggedFile ? '1' : '0') + '" data-file-locked="' + (fileLocked ? '1' : '0') + '" data-file-video="' + (!isDir && isVideoName(file.name) ? '1' : '0') + '">' + nameContent + lockIcon + pathMeta + '</td>' +
              '<td>' + (isDir ? t('文件夹') : (formatNumber(size) + t(' 字节'))) + '</td>' +
              '<td>' + uploaded + '</td>' +
              '<td class="actions-cell"><div class="actions">' + previewBtn + videoBtn + audioBtn + textBtn + pdfBtn + officeBtn + '</div></td>' +
              '<td class="row-danger-action"><div class="danger-actions">' + primaryActionBtn + '</div></td>' +
              '<td class="row-permanent-action"><div class="permanent-actions">' + permanentDeleteBtn + '</div></td>' +
            '</tr>'
          );
        }).join('');
        updateFileSelectAllState();
        updateFileBulkActionButton();
      }

      function handleLocalDiskFileNameDoubleClick(path) {
        const filePath = String(path || '');
        if (!filePath) {
          return;
        }
        clearLocalDiskRenameClickTimer();
        activeLocalDiskRenamePath = '';
        selectedLocalDiskPaths.clear();
        selectedLocalDiskPaths.add(filePath);
        refreshRenderedLocalDiskSelection();
        downloadRemoteListFile(filePath, true);
      }

      function setAdminStorageProgress(visible, percent, message) {
        if (!adminStorageProgress) {
          return;
        }
        adminStorageProgress.hidden = !visible;
        if (adminStorageProgressTitle) {
          adminStorageProgressTitle.textContent = activeAdminStorageProgressMode === 'backup'
            ? t('正在同步备份')
            : t('正在移动存储文件');
        }
        const safePercent = Math.max(0, Math.min(100, Number(percent || 0)));
        if (adminStorageProgressFill) {
          adminStorageProgressFill.style.width = safePercent.toFixed(1) + '%';
        }
        if (adminStorageProgressText) {
          adminStorageProgressText.textContent = safePercent.toFixed(1).replace(/\.0$/, '') + '%';
        }
        if (adminStorageProgressMessage) {
          adminStorageProgressMessage.textContent = String(message || '');
        }
      }

      function tagLockKey(tagId) {
        return 'tag:' + String(tagId || '');
      }

      function clearSelectedFolders() {
        selectedFolderPaths.clear();
        lastSelectedFolderPath = '';
      }

      function saveTagTreeState() {
      }

      function setTranscodeButtons(encodedName, options) {
        const opts = options || {};
        const startBtns = statusBox.querySelectorAll('[data-transcode-file="' + encodedName + '"]');
        const cancelBtn = statusBox.querySelector('[data-cancel-file="' + encodedName + '"]');
        const confirmBtn = statusBox.querySelector('[data-confirm-transcode="' + encodedName + '"]');
        if (typeof opts.startDisabled === 'boolean') {
          startBtns.forEach(function (startBtn) {
            startBtn.disabled = opts.startDisabled;
          });
        }
        if (cancelBtn && typeof opts.cancelDisabled === 'boolean') {
          cancelBtn.disabled = opts.cancelDisabled;
        }
        if (confirmBtn && typeof opts.confirmVisible === 'boolean') {
          confirmBtn.hidden = !opts.confirmVisible;
        }
        if (cancelBtn) {
          cancelBtn.textContent = getTranscodeTaskId(encodedName) ? t('取消转码') : t('不转换');
        }
      }

      function restoreLocalImportProgressWindow() {
        localImportProgressMinimized = false;
        const desc = document.getElementById('local-import-progress-desc');
        if (desc && isCopyTaskWindowMode(localImportProgressWindowMode)) {
          desc.textContent = getCopyTaskProgressDescription();
        }
        applyLocalImportProgressWindowState();
      }

      function getSortedCurrentFiles() {
        const key = sortKey.value || 'name';
        const order = sortOrder.value || 'asc';
        return (Array.isArray(currentFiles) ? currentFiles : []).slice().sort(function (a, b) {
          return compareFiles(a, b, key, order);
        });
      }

      function getVisibleLocalDiskFilePaths() {
        return activeLocalDiskItems.filter(function (item) {
          return item && !item.directory;
        }).map(function (item) {
          return String(item.path || '');
        }).filter(Boolean);
      }

      async function applyAdminStoragePathChange() {
        if (!adminStoragePath || !adminStorageChooseBtn) {
          return;
        }
        const nextPath = String(adminStoragePath.value || '').trim();
        if (!nextPath) {
          showStatus(t('存储路径不能为空'), 'err');
          adminStoragePath.value = currentAdminStoragePath;
          return;
        }
        if (nextPath === currentAdminStoragePath) {
          showStatus(t('存储路径未改变'), 'warn');
          return;
        }
        const storageChangeChoice = await askConfirmDialog({
          title: t('确认修改存储路径'),
          description: t('是否将当前存储路径下的文件移动到目标目录？'),
          confirmText: t('是，开始移动'),
          extraText: t('否，只修改路径'),
          extraValue: 'no-migrate',
          cancelText: t('取消'),
          danger: false
        });
        if (storageChangeChoice === false) {
          adminStoragePath.value = currentAdminStoragePath;
          setAdminStorageProgress(false, 0, '');
          showStatus(t('已取消，存储路径未修改'), 'warn');
          return;
        }
        if (storageChangeChoice === 'no-migrate') {
          adminStorageChooseBtn.disabled = true;
          adminStoragePath.disabled = true;
          setAdminStorageProgress(false, 0, '');
          try {
            await fetchJson(
              api.adminStorageMigrate + '?path=' + encodeURIComponent(nextPath) + '&migrate=0',
              { method: 'POST' }
            );
            await loadAdminStoragePath();
            loadFiles();
            showStatus(t('存储路径已修改，未迁移文件：') + currentAdminStoragePath, 'ok');
          } catch (err) {
            adminStoragePath.value = currentAdminStoragePath;
            showStatus(t('只修改存储路径失败：') + err.message, 'err');
          } finally {
            adminStorageChooseBtn.disabled = false;
            adminStoragePath.disabled = false;
          }
          return;
        }
        adminStorageChooseBtn.disabled = true;
        adminStoragePath.disabled = true;
        setAdminStorageProgress(true, 0, t('正在提交移动任务...'));
        try {
          const startData = await fetchJson(
            api.adminStorageMigrate + '?path=' + encodeURIComponent(nextPath),
            { method: 'POST' }
          );
          const migrateData = await pollAdminStorageMigration(String(startData.task_id || ''));
          await loadAdminStoragePath();
          loadFiles();
          showStatus(t('存储路径已修改：') + currentAdminStoragePath, 'ok');
          await askCleanupOldStorageAfterMigration(migrateData);
        } catch (err) {
          adminStoragePath.value = currentAdminStoragePath;
          setAdminStorageProgress(true, 0, '移动失败：' + err.message);
          showStatus('修改存储路径失败：' + err.message, 'err');
        } finally {
          adminStorageChooseBtn.disabled = false;
          adminStoragePath.disabled = false;
        }
      }

      async function applyAdminStorageBackupPathChange(pathValue) {
        if (!adminStorageBackupPath || !adminStorageBackupSetBtn) {
          return;
        }
        const nextPath = String(pathValue !== undefined ? pathValue : adminStorageBackupPath.value || '').trim();
        if (!nextPath) {
          showStatus(t('备份路径不能为空'), 'err');
          adminStorageBackupPath.value = '';
          return;
        }
        const confirmed = await askConfirmDialog({
          title: t('确认添加备份路径'),
          description: t('将添加为备份路径。需要立即拷贝主存储数据时，请点击主存储旁边的同步按钮。'),
          confirmText: t('确认'),
          cancelText: t('取消'),
          danger: false
        });
        if (!confirmed) {
          adminStorageBackupPath.value = '';
          return;
        }
        adminStorageBackupSetBtn.disabled = true;
        try {
          await fetchJson(api.adminStorageBackup + '?path=' + encodeURIComponent(nextPath), { method: 'POST' });
          await loadAdminStoragePath();
          showStatus(t('备份路径已添加：') + nextPath, 'ok');
        } catch (err) {
          adminStorageBackupPath.value = '';
          showStatus(t('设置备份路径失败：') + err.message, 'err');
        } finally {
          adminStorageBackupSetBtn.disabled = false;
        }
      }

      async function removeAdminStorageBackupPath(path) {
        const backupPath = String(path || '');
        if (!backupPath) {
          return;
        }
        const confirmed = await askConfirmDialog({
          title: t('移除备份路径'),
          description: t('移除后不会删除该备份目录，只是不再同步新的虚拟磁盘数据。'),
          confirmText: t('确认'),
          cancelText: t('取消'),
          danger: false
        });
        if (!confirmed) {
          return;
        }
        try {
          await fetchJson(api.adminStorageBackup + '?remove=' + encodeURIComponent(backupPath), { method: 'POST' });
          await loadAdminStoragePath();
          showStatus(t('已移除备份路径：') + backupPath, 'ok');
        } catch (err) {
          showStatus(t('移除备份路径失败：') + err.message, 'err');
        }
      }

      async function toggleAdminStorageBackupPath(path) {
        const backupPath = String(path || '');
        if (!backupPath) {
          return;
        }
        try {
          await fetchJson(api.adminStorageBackup + '?toggle=' + encodeURIComponent(backupPath), { method: 'POST' });
          await loadAdminStoragePath();
          showStatus(t('备份路径状态已更新：') + backupPath, 'ok');
        } catch (err) {
          showStatus(t('切换备份路径状态失败：') + err.message, 'err');
        }
      }

      function renderAdminStorageUploadAutoSync() {
        if (adminStorageUploadSyncState) {
          adminStorageUploadSyncState.textContent = t(currentAdminStorageUploadAutoSync
            ? '上传数据自动同步：已启用'
            : '上传数据自动同步：已禁用');
        }
        if (adminStorageUploadSyncToggleBtn) {
          adminStorageUploadSyncToggleBtn.textContent = t(currentAdminStorageUploadAutoSync
            ? '禁用上传自动同步'
            : '启用上传自动同步');
        }
      }

      async function toggleAdminStorageUploadAutoSync() {
        if (!adminStorageUploadSyncToggleBtn) {
          return;
        }
        const nextEnabled = !currentAdminStorageUploadAutoSync;
        adminStorageUploadSyncToggleBtn.disabled = true;
        try {
          await fetchJson(api.adminStorageBackup + '?upload_auto_sync='
            + encodeURIComponent(nextEnabled ? '1' : '0'), { method: 'POST' });
          currentAdminStorageUploadAutoSync = nextEnabled;
          renderAdminStorageUploadAutoSync();
          showStatus(t(nextEnabled
            ? '上传数据自动同步已启用'
            : '上传数据自动同步已禁用'), 'ok');
          await loadAdminStoragePath();
        } catch (err) {
          showStatus(t('切换上传数据自动同步失败：') + err.message, 'err');
        } finally {
          adminStorageUploadSyncToggleBtn.disabled = false;
        }
      }

      async function swapAdminStorageBackupPath(path) {
        const backupPath = String(path || currentAdminStorageBackupPath || '');
        if (!backupPath) {
          showStatus(t('请先设置备份路径'), 'err');
          return;
        }
        const confirmed = await askConfirmDialog({
          title: t('切换主备目录'),
          description: t('将把当前备份目录设为主存储目录，并把原主存储目录设为新的备份目录。'),
          confirmText: t('确认切换'),
          cancelText: t('取消'),
          danger: false
        });
        if (!confirmed) {
          return;
        }
        try {
          await fetchJson(api.adminStorageBackupSwap + '?path=' + encodeURIComponent(backupPath), { method: 'POST' });
          await loadAdminStoragePath();
          await Promise.all([loadFiles(), loadAdminStoragePath()]);
          activateAdminView('storage');
          showStatus(t('主备目录已切换：') + currentAdminStoragePath, 'ok');
        } catch (err) {
          showStatus(t('切换主备目录失败：') + err.message, 'err');
        }
      }

      async function syncAdminStorageBackupPaths() {
        const enabledBackups = currentAdminStorageBackupPaths.filter(function (item) {
          return item && item.enabled !== false;
        });
        if (!enabledBackups.length) {
          showStatus(t('没有启用的备份路径'), 'err');
          return;
        }
        const confirmed = await askConfirmDialog({
          title: t('同步备份'),
          description: t('将把主存储上的数据拷贝到所有启用的备份存储。遇到同名但内容不同的文件时会暂停并提示。'),
          confirmText: t('开始同步'),
          cancelText: t('取消'),
          danger: false
        });
        if (!confirmed) {
          return;
        }
        if (adminStorageSyncBtn) {
          adminStorageSyncBtn.disabled = true;
        }
        activeAdminStorageProgressMode = 'backup';
        setAdminStorageProgress(true, 0, t('正在提交备份同步任务...'));
        try {
          const startData = await fetchJson(api.adminStorageBackupSync, { method: 'POST' });
          const syncData = await pollAdminStorageBackupSync(String(startData.task_id || ''));
          await loadAdminStoragePath();
          showStatus(t('备份同步完成'), 'ok');
          return syncData;
        } catch (err) {
          setAdminStorageProgress(true, 0, t('备份同步失败：') + err.message);
          showStatus(t('备份同步失败：') + err.message, 'err');
        } finally {
          if (adminStorageSyncBtn) {
            adminStorageSyncBtn.disabled = false;
          }
        }
      }

      function renderAdminStorageBackupList() {
        if (!adminStorageBackupList) {
          return;
        }
        if (!currentAdminStorageBackupPaths.length) {
          adminStorageBackupList.innerHTML = '<p class="admin-storage-backup-empty">' + escapeHtml(t('暂无备份路径')) + '</p>';
          return;
        }
        adminStorageBackupList.innerHTML = currentAdminStorageBackupPaths.map(function (item) {
          const path = String((item && item.path) || item || '');
          const enabled = !item || item.enabled !== false;
          const encoded = encodeURIComponent(path);
          return '<div class="admin-storage-backup-item' + (enabled ? '' : ' disabled') + '">' +
            '<span class="admin-storage-backup-path">' + escapeHtml(path) + '</span>' +
            '<button type="button" class="tag-dialog-btn secondary" data-storage-backup-swap="' + encoded + '">' + escapeHtml(t('设为主存储')) + '</button>' +
            '<button type="button" class="tag-dialog-btn secondary" data-storage-backup-remove="' + encoded + '">' + escapeHtml(t('移除')) + '</button>' +
            '<button type="button" class="tag-dialog-btn secondary" data-storage-backup-toggle="' + encoded + '">' + escapeHtml(t(enabled ? '禁用' : '启用')) + '</button>' +
          '</div>';
        }).join('');
      }

      function getTagPassword(tagId) {
        return unlockedFilePasswords.get(tagLockKey(tagId)) || '';
      }

      function activatePanel(panelId, options) {
        if ((panelId === 'panel-admin' || panelId === 'panel-users') && !authState.admin) {
          panelId = 'panel-files';
        }
        setActivePanel(panelId);

        if (panelId === 'panel-files' && !(options && options.skipLoadFiles)) {
          loadFiles();
        } else if (panelId === 'panel-local-disk') {
          if (authState.localDiskAllowed) {
            updateRemoteUploadTargetHints();
            loadLocalDisk(activeLocalDiskPath || '');
          } else {
            activatePanel('panel-files');
          }
        } else if (panelId === 'panel-admin') {
          activateAdminView('storage');
        }
      }

      async function openFileTagMenu(button, fileName) {
        return openFilesTagMenu(button, [fileName], {});
      }

      async function startManualTranscode(encodedName, mode) {
        setTranscodeButtons(encodedName, { startDisabled: true, cancelDisabled: true });

        const fileName = decodeURIComponent(encodedName);
        const requestedMode = mode === 'audio_only' || mode === 'audio_split' || mode === 'full_mp4' ? mode : 'auto';
        try {
          setTranscodeVisualState(encodedName, 'running');
          updateTranscodeProgress(encodedName, 0, t('任务创建中...'));
          setTranscodeReason(encodedName, requestedMode === 'audio_split'
            ? t('状态：正在请求后台启动转码任务（拆分视频并转音频）')
            : (requestedMode === 'audio_only'
              ? t('状态：正在请求后台启动转码任务（只转音频）')
            : (requestedMode === 'full_mp4'
              ? t('状态：正在请求后台启动转码任务（音视频都转）')
              : t('状态：正在请求后台启动转码任务'))));
          let url = api.convertVideo + '?file=' + encodeURIComponent(fileName);
          if (requestedMode !== 'auto') {
            url += '&mode=' + encodeURIComponent(requestedMode);
          }
          const data = await fetchJson(url, {
            method: 'POST'
          });

          if (data.completed) {
            setTranscodeVisualState(encodedName, 'done');
            updateTranscodeProgress(encodedName, 100, t('无需转码'));
            setTranscodeReason(encodedName, t('状态：文件已经可直接播放'));
            setTranscodeButtons(encodedName, { startDisabled: true, cancelDisabled: true, confirmVisible: true });
            return;
          }

          if (!data.task_id) {
            throw new Error('missing task id');
          }

          setTranscodeTaskId(encodedName, String(data.task_id));
          setTranscodeReason(encodedName, t('状态：后台任务已启动，任务号 ') + String(data.task_id));
          updateTranscodeProgress(encodedName, Number(data.progress || 0), t('已启动'));
          setTranscodeButtons(encodedName, { startDisabled: true, cancelDisabled: false });
          stopTranscodePolling(encodedName);
          const timer = setInterval(function () {
            pollTranscodeProgress(encodedName, data.task_id);
          }, 1000);
          transcodeProgressTimers.set(encodedName, timer);
          await pollTranscodeProgress(encodedName, data.task_id);
        } catch (err) {
          stopTranscodePolling(encodedName);
          setTranscodeTaskId(encodedName, '');
          setTranscodeVisualState(encodedName, 'failed');
          updateTranscodeProgress(encodedName, 0, t('失败：') + err.message);
          setTranscodeReason(encodedName, t('状态：失败，') + err.message);
          setTranscodeButtons(encodedName, { startDisabled: false, cancelDisabled: true });
        }
      }

      function setRemoteCopyProgress(progress, text, item) {
        setCopyTaskProgress('remote-copy', progress, text, item);
      }

      function openLocalDiskImagePreview(path, displayName) {
        const rawPath = String(path || '');
        if (!rawPath) {
          return;
        }
        const gallery = buildLocalDiskImageGallery();
        const index = imageGalleryIndexOf(gallery, rawPath);
        const current = gallery[index] || { file: rawPath, name: displayName || rawPath, local: true };
        openPreview('image', encodeURIComponent(rawPath), displayName || current.name || rawPath, {
          local: true,
          url: localDiskDownloadUrl(rawPath),
          previewKey: 'local-image-gallery:' + String(activeLocalDiskPath || localDiskParentPath(rawPath) || 'root'),
          gallery: gallery.length ? gallery : [current],
          galleryIndex: index
        });
      }

      function setLocalDiskDirLockedState(path, locked) {
        const target = String(path || '');
        if (!target) {
          return;
        }
        activeLocalDiskItems.forEach(function (item) {
          if (item && item.directory && String(item.path || '') === target) {
            item.locked = !!locked;
          }
        });
        localDiskTreeCache.forEach(function (dirs) {
          dirs.forEach(function (item) {
            if (item && String(item.path || '') === target) {
              item.locked = !!locked;
            }
          });
        });
      }

      async function openAdminStoragePickerDialog() {
        if (!adminStoragePickerDialog) {
          return;
        }
        try {
          await jumpAdminStoragePickerPath('/');
          adminStoragePickerDialog.hidden = false;
          document.body.style.overflow = 'hidden';
        } catch (err) {
          showStatus(t('加载本地目录树失败：') + err.message, 'err');
        }
      }

      function openAdminStorageBackupPickerDialog() {
        adminStoragePickerTarget = 'backup';
        openAdminStoragePickerDialog();
      }

      function getFileLabel(file) {
        return String((file && file.display_path) || getFilePath(file) || '');
      }

      async function assessVideoPlaybackCompatibility(name) {
        const fileName = String(name || '');
        if (!fileName) {
          return { needConvert: false, reason: '', allowAudioSplitChoice: false };
        }
        if (isRealMediaVideoName(fileName)) {
          return {
            needConvert: true,
            reason: t('RM/RMVB视频为旧格式，为了兼容浏览器播放，建议转换为MP4'),
            allowAudioSplitChoice: false
          };
        }

        const probe = await isVideoPlayable(fileName, 8000);
        const audioProbe = await checkVideoAudio(fileName);

        let needConvert = !probe.ok;
        let reason = probe.reason || t('无法解析视频');
        const allowAudioSplitChoice = !!(audioProbe && audioProbe.ok
          && audioProbe.has_audio && audioProbe.browser_audio_supported === false);

        if (allowAudioSplitChoice) {
          needConvert = true;
          reason = t('音频编码 ') + (audioProbe.audio_codec || 'unknown') + t(' 浏览器不支持');
        }

        return { needConvert: needConvert, reason: reason, allowAudioSplitChoice: allowAudioSplitChoice };
      }

      async function verifyUploadedVideos(uploadResult) {
        const items = Array.isArray(uploadResult) ? uploadResult : [];
        const videoNames = items
          .filter(function (it) {
            return it && it.saved === true && isVideoName(it.name || '');
          })
          .map(function (it) {
            return String(it.path || it.name || '');
          });

        if (!videoNames.length) {
          return [];
        }

        const failed = [];
        for (const name of videoNames) {
          const candidate = await assessVideoPlaybackCompatibility(name);
          if (candidate.needConvert) {
            failed.push({
              name: name,
              reason: candidate.reason,
              allowAudioSplitChoice: candidate.allowAudioSplitChoice
            });
          }
        }

        return failed;
      }

      function walkTagNodes(list, visitor, level, parent, parentList) {
        const nodes = Array.isArray(list) ? list : [];
        for (let i = 0; i < nodes.length; i += 1) {
          const node = nodes[i];
          const stop = visitor(node, level, parent, parentList, i);
          if (stop) {
            return true;
          }
          if (walkTagNodes(node.children, visitor, level + 1, node, node.children)) {
            return true;
          }
        }
        return false;
      }

      function normalizeFolderNode(node) {
        const item = node && typeof node === 'object' ? node : {};
        const rawChildren = Array.isArray(item.children) ? item.children.map(normalizeFolderNode) : [];
        const children = sortSharedFolderChildren(String(item.path || ''), rawChildren);
        return {
          name: String(item.name || ''),

          path: String(item.path || ''),
          parent_path: String(item.parent_path || ''),
          file_count: Number(item.file_count || 0),
          folder_count: Number(item.folder_count || 0),
          locked: !!item.locked,
          children: children
        };
      }

      function sortSharedFolderChildren(parentPath, children) {
        let list = Array.isArray(children) ? children.slice() : [];
        if (!String(parentPath || '')) {
          list = ensureRootFixedFolderNodes(list);
        }
        if (!isFixedFolderParentPath(parentPath)) {
          return list;
        }
        return list.sort(function (a, b) {
          const left = sharedFixedFolderOrder((a && a.path) || (a && a.name) || '');
          const right = sharedFixedFolderOrder((b && b.path) || (b && b.name) || '');
          if (left !== right) {
            return left - right;
          }
          return 0;
        });
      }

      function clearTagFileFilter() {
        activeFilterTagId = '';
        renderFiles(allFiles);
        renderTagTree();
      }

      function cloneCanvas(source) {
        const canvas = document.createElement('canvas');
        canvas.width = source.width;
        canvas.height = source.height;
        const ctx = canvas.getContext('2d');
        ctx.drawImage(source, 0, 0);
        return canvas;
      }

      function ensureLocalDiskTreeRoot(path) {
        if (!activeLocalDiskTreeRootPath) {
          activeLocalDiskTreeRootPath = String(path || '/');
          expandedLocalDiskTreePaths.add(activeLocalDiskTreeRootPath);
        }
      }

      async function loadFiles() {
        try {
          let filesUrl = api.files;
          let activePassword = getFolderPasswordForPath(activeFolderPath);
          const remoteShowHidden = !!(remoteDiskShowHidden && remoteDiskShowHidden.checked);
          if (!remoteShowHidden && String(activeFolderPath || '').split('/').some(function (part) { return part.charAt(0) === '.'; })) {
            activeFolderPath = '';
            activePassword = '';
          }
          if (activeFolderPath && getFolderLockAncestorPath(activeFolderPath) && !activePassword) {
            activeFolderPath = '';
            activePassword = '';
          }
          if (activeFolderPath) {
            filesUrl += '?folder=' + encodeURIComponent(activeFolderPath);
            if (activePassword) {
              filesUrl += '&folder_password=' + encodeURIComponent(activePassword);
            }
          }
          if (remoteShowHidden) {
            filesUrl += (filesUrl.indexOf('?') >= 0 ? '&' : '?') + 'show_hidden=1';
          }
          const results = await Promise.all([
            fetchJson(filesUrl),
            fetchFolderList(activeFolderPath)
          ]);
          allFiles = Array.isArray(results[0].files) ? results[0].files.map(normalizeFileRecord) : [];
          const folderRows = Array.isArray(results[1].folders) ? results[1].folders : [];
          if (activeFolderPath) {
            if (!replaceFolderChildren(activeFolderPath, folderRows)) {
              await loadFolderTreeState();
            }
          } else {
            folderTreeData = ensureRootFixedFolderNodes(folderRows).map(normalizeFolderNode);
          }
          ensureFolderParentPathExpanded(activeFolderPath);
          await loadTagTreeState();
          renderFolderTree();
          renderTagTree();
          if (activeFilterTagId) {
            try {
              await showFilesForTag(activeFilterTagId);
            } catch (_) {
              clearTagFileFilter();
            }
          } else {
            renderFiles(allFiles);
          }
          await recoverRunningTranscodeTasks();
        } catch (err) {
          allFiles = [];
          folderTreeData = [];
          fileList.innerHTML = '';
          fileTable.style.display = 'none';
          fileEmpty.textContent = '加载虚拟磁盘失败：' + err.message;
          fileEmpty.style.display = 'block';
          renderFolderTree();
          renderTagTree();
          showStatus('加载列表失败：' + err.message, 'err');
        }
      }

      function canRenameFolderPath(path) {
        const text = String(path || '');
        return !!text && !isRecycleFolderPath(text)
          && !isSharedRootFolderPath(text)
          && !isReservedFixedFolderPath(text);
      }

      function closeAudioTagContextMenu() {
        if (!activeAudioTagContextMenu) {
          return;
        }
        if (activeAudioTagContextMenu.parentNode) {
          activeAudioTagContextMenu.parentNode.removeChild(activeAudioTagContextMenu);
        }
        activeAudioTagContextMenu = null;
      }

      function isActiveTagPreviewEnabled() {
        return isTagPreviewEnabled(activeFilterTagId);
      }

      function syncFolderActionButtons() {
        if (folderDeleteBtn) {
          folderDeleteBtn.disabled = !activeFolderPath
            || isRecycleRootFolderPath(activeFolderPath)
            || isSharedRootFolderPath(activeFolderPath)
            || isReservedFixedFolderPath(activeFolderPath);
        }
        if (folderRestoreBtn) {
          folderRestoreBtn.disabled = !activeFolderPath || !isRecycleFolderPath(activeFolderPath) || isRecycleRootFolderPath(activeFolderPath);
        }
        if (folderCurrentPath) {
          folderCurrentPath.textContent = getFolderLabel(activeFolderPath);
        }
        setUploadTargetFolder(activeFolderPath);
      }

      function isCurrentFileLocal(fileName) {
        const target = String(fileName || '');
        const found = getFileRecordByPath(target);
        return !!(found && found.local);
      }

      function setPreviewCropMode(win, enabled) {
        if (!win) {
          return;
        }
        win.__imageCropMode = !!enabled;
        const shell = win.querySelector('.preview-image-shell');
        const cropRect = win.querySelector('.preview-crop-rect');
        if (shell) {
          shell.classList.toggle('crop-mode', !!enabled);
        }
        if (cropRect && !enabled && !win.__imageCropRect) {
          cropRect.hidden = true;
          cropRect.removeAttribute('style');
        }
        setImageEditHint(win, enabled ? '在图片上拖拽选择剪切区域，然后点击“应用剪切”。' : '');
      }

      async function toggleLocalDiskTreePath(path) {
        const target = String(path || '');
        if (!target) {
          return;
        }
        if (expandedLocalDiskTreePaths.has(target) && localDiskTreeCache.has(target)) {
          expandedLocalDiskTreePaths.delete(target);
          renderLocalDiskItems(activeLocalDiskItems);
          return;
        }

        try {
          const showHidden = localDiskShowHidden && localDiskShowHidden.checked;
          const url = api.localDiskList
            + '?path=' + encodeURIComponent(target)
            + (showHidden ? '&show_hidden=1' : '');
          const data = await fetchJson(appendLocalDirPassword(url, target));
          cacheLocalDiskTreeNode(String(data.path || target), Array.isArray(data.items) ? data.items : []);
          expandedLocalDiskTreePaths.add(String(data.path || target));
          renderLocalDiskItems(activeLocalDiskItems);
        } catch (err) {
          showStatus('展开目录失败：' + err.message, 'err');
        }
      }

      function handleLocalDiskFileRenameFocusoutEvent(e) {
        const input = e.target.closest('.local-disk-rename-input');
        if (!input) {
          return;
        }
        window.setTimeout(function () {
          if (document.activeElement !== input) {
            submitLocalDiskFileRename(input);
          }
        }, 0);
      }

function isRecycleFolderPath(path) {
        const text = String(path || '');
        return text === RECYCLE_FOLDER_NAME || text.indexOf(RECYCLE_FOLDER_NAME + '/') === 0;
      }

      function isPdfName(name) {
        return /\.pdf$/i.test(String(name || ''));
      }

      function getTagRootMeta(tagId) {
        let meta = findTagMetaById(tagId);
        if (!meta) {
          return null;
        }
        while (meta.parent) {
          meta = findTagMetaById(meta.parent.id);
          if (!meta) {
            return null;
          }
        }
        return meta;
      }

      function findFolderNodeByPath(path, nodes) {
        const target = String(path || '');
        const list = Array.isArray(nodes) ? nodes : folderTreeData;
        for (let i = 0; i < list.length; i += 1) {
          const node = list[i];
          if (String((node && node.path) || '') === target) {
            return node;
          }
          const found = findFolderNodeByPath(target, node && node.children);
          if (found) {
            return found;
          }
        }
        return null;
      }

      function replaceFolderChildren(path, children) {
        const target = String(path || '');
        const sourceChildren = !target ? ensureRootFixedFolderNodes(children) : children;
        const mappedChildren = Array.isArray(sourceChildren) ? sourceChildren.map(normalizeFolderNode) : [];
        const normalizedChildren = sortSharedFolderChildren(target, mappedChildren);
        if (!target) {
            folderTreeData = normalizedChildren;
          return true;
        }
        const node = findFolderNodeByPath(target);
        if (!node) {
          return false;
        }
        node.children = normalizedChildren;
        node.folder_count = normalizedChildren.length;
        return true;
      }

      async function loadFolderChildren(path) {
        const target = String(path || '');
        const data = await fetchFolderList(target);
        replaceFolderChildren(target, Array.isArray(data.folders) ? data.folders : []);
      }

      function updateExplorerLayout() {
        const isTagFilterMode = !!activeFilterTagId;
        const isPreviewEnabledTag = isTagFilterMode && isActiveTagPreviewEnabled();
        if (fileListTitle) {
          fileListTitle.textContent = isTagFilterMode ? t('当前标签文件') : t('当前目录文件');
        }
        if (tagViewModeBtns) {
          tagViewModeBtns.hidden = !isPreviewEnabledTag;
        }
        if (!isPreviewEnabledTag) {
          tagFileViewMode = 'list';
          if (tagViewListBtn) tagViewListBtn.classList.add('active');
          if (tagViewPreviewBtn) tagViewPreviewBtn.classList.remove('active');
          if (tagImagePreviewWrap) tagImagePreviewWrap.hidden = true;
        }
        if (explorerShell) {
          explorerShell.classList.toggle('tag-filter-mode', isTagFilterMode);
        }
        if (folderBrowser) {
          folderBrowser.hidden = isTagFilterMode;
        }
      }

      function previewBaseCanvas(win) {
        return (win && win.__imageBaseCanvas) || capturePreviewBaseCanvas(win);
      }

      function renderLocalDiskItems(items) {
        activeLocalDiskItems = Array.isArray(items) ? items.slice() : [];
        const list = activeLocalDiskItems.slice().sort(compareLocalDiskItems);
        updateLocalDiskSortIndicator();
        if (localDiskContext) {
          localDiskContext.textContent = '当前路径：' + activeLocalDiskPath;
        }

        if (localDiskViewMode === 'split') {
          renderLocalDiskSplitView(list);
        } else {
          renderLocalDiskTableView(list);
        }
      }

      function setLocalDiskViewMode(mode) {
        localDiskViewMode = mode === 'split' ? 'split' : 'table';
        const isTable = localDiskViewMode === 'table';
        if (localDiskViewTableBtn) {
          localDiskViewTableBtn.classList.toggle('active', isTable);
        }
        if (localDiskViewSplitBtn) {
          localDiskViewSplitBtn.classList.toggle('active', !isTable);
        }
        if (localDiskTableWrap) {
          localDiskTableWrap.hidden = !isTable;
        }
        if (localDiskExplorer) {
          localDiskExplorer.hidden = isTable;
        }
        renderLocalDiskItems(activeLocalDiskItems);
      }

      function parentFolderPathFromFilePath(path) {
        const text = String(path || '');
        const index = text.lastIndexOf('/');
        return index >= 0 ? text.slice(0, index) : '';
      }

      function clampFloatingMenuPosition(menuEl, clientX, clientY) {
        if (!menuEl) {
          return;
        }
        const rect = menuEl.getBoundingClientRect();
        const left = Math.max(8, Math.min(clientX, window.innerWidth - rect.width - 8));
        const top = Math.max(8, Math.min(clientY, window.innerHeight - rect.height - 8));
        menuEl.style.left = Math.round(left) + 'px';
        menuEl.style.top = Math.round(top) + 'px';
      }

      function canRenameTagNode(node, level) {
        return !!(node && node.id) && !isProtectedRestrictedRootTag(node, level);
      }

      function buildFolderTreeHtml(nodes, level) {
        const list = Array.isArray(nodes) ? nodes : [];
        return list.map(function (node) {
          const path = String(node.path || '');
          const expanded = expandedFolderPaths.has(path);
          const hasChildren = (Array.isArray(node.children) && node.children.length > 0) || Number(node.folder_count || 0) > 0;
          const isActive = activeFolderPath === path;
          const isSelected = selectedFolderPaths.has(path);
          const isRenaming = activeFolderRenamePath === path && canRenameFolderPath(path);
          const padding = 10 + (Math.max(0, Number(level) || 0) * 18);
          let folderIconHtml = '';
          if (isRecycleRootFolderPath(path)) {
            folderIconHtml = '<span class="folder-tree-icon recycle" aria-hidden="true">🗑</span>';
          } else if (isSharedRootFolderPath(path)) {
            folderIconHtml = '<span class="folder-tree-icon shared" aria-hidden="true">⇄</span>';
          } else if (isReservedFixedFolderPath(path)) {
            folderIconHtml = getSharedFixedFolderIconHtml(path);
          } else if (!path) {
            folderIconHtml = '<span class="folder-tree-icon root" aria-hidden="true">⌂</span>';
          }
          const childHtml = Array.isArray(node.children) && node.children.length > 0 && expanded
            ? '<div class="folder-tree-children">' + buildFolderTreeHtml(node.children, (level || 0) + 1) + '</div>'
            : '';
          const displayName = getSharedFolderDisplayName(path, node.name || '');
          const nameHtml = isRenaming
            ? '<input class="folder-rename-input" draggable="false" data-folder-rename-input="' + escapeHtml(path) + '" value="' + escapeHtml(node.name || '') + '" maxlength="120">'
            : '<span class="folder-tree-name">' + folderIconHtml + escapeHtml(displayName) + '</span>';
          const lockHtml = folderLockIconHtml(node);
          const countHtml = path ? String(Number(node.file_count || 0)) : '';
          return (
            '<div class="folder-tree-node' + (isActive ? ' active' : '') + (isSelected ? ' selected' : '') + (activeDropFolderPath === path ? ' drop-target' : '') + '" data-folder-path="' + escapeHtml(path) + '">' +
              '<div class="folder-tree-line" style="padding-left:' + padding + 'px;">' +
                (hasChildren
                  ? '<button type="button" class="folder-tree-toggle" data-folder-toggle="' + escapeHtml(path) + '">' + (expanded ? '▾' : '▸') + '</button>'
                  : '<span class="folder-tree-toggle placeholder">•</span>') +
                '<div class="folder-tree-entry" data-folder-select="' + escapeHtml(path) + '" data-drag-folder="' + escapeHtml(path) + '" draggable="' + (isRenaming ? 'false' : 'true') + '">' +
                  nameHtml +
                  lockHtml +
                  '<span class="folder-tree-count">' + countHtml + '</span>' +
                '</div>' +
              '</div>' +
              childHtml +
            '</div>'
          );
        }).join('');
      }

      function updateFileBulkActionButton() {
        if (!fileBulkAction && !fileBulkDeleteAction && !fileBulkTagAction) {
          return;
        }

        const selectedCount = getSelectedVisibleFileNames().length;
        const isTagFilterMode = !!activeFilterTagId;
        const isRecycleMode = !isTagFilterMode && isRecycleFolderPath(activeFolderPath);
        if (fileBulkAction) {
          const label = isTagFilterMode ? t('移除') : (isRecycleMode ? t('恢复') : t('删除'));
          const title = isTagFilterMode
            ? t('批量从当前标签移除')
            : (isRecycleMode ? t('批量恢复文件到原路径') : t('批量删除文件（移入回收站）'));
          fileBulkAction.textContent = label;
          fileBulkAction.title = title;
          fileBulkAction.setAttribute('aria-label', title);
          fileBulkAction.disabled = selectedCount === 0;
        }

        if (fileBulkDeleteAction) {
          const title = t('批量彻底删除文件/文件夹（仅回收站）');
          fileBulkDeleteAction.textContent = t('彻底删除');
          fileBulkDeleteAction.title = title;
          fileBulkDeleteAction.setAttribute('aria-label', title);
          fileBulkDeleteAction.disabled = !isRecycleMode || selectedCount === 0;
        }

        if (fileBulkTagAction) {
          fileBulkTagAction.disabled = selectedCount === 0;
          fileBulkTagAction.title = selectedCount > 0 ? (t('给选中的 ') + selectedCount + t(' 个文件加标签')) : t('给选中文件加标签');
          fileBulkTagAction.setAttribute('aria-label', fileBulkTagAction.title);
        }
      }

      function cancelPreviewImageCrop(win) {
        if (!win) {
          return;
        }
        win.__imageCropRect = null;
        const cropRect = win.querySelector('.preview-crop-rect');
        if (cropRect) {
          cropRect.hidden = true;
          cropRect.removeAttribute('style');
        }
        setPreviewCropMode(win, false);
      }

      function closePreviewWindow(win, key) {
        if (!win) {
          return;
        }
        const media = win.querySelector('video, audio');
        if (media) {
          if (typeof win.__saveStreamState === 'function') {
            win.__saveStreamState();
          }
          if (win.__streamStateTimer) {
            clearInterval(win.__streamStateTimer);
            win.__streamStateTimer = null;
          }
          if (win.__streamProgressTimer) {
            clearInterval(win.__streamProgressTimer);
            win.__streamProgressTimer = null;
          }
          const resumeFile = media.getAttribute('data-resume-file') || '';
          if (resumeFile && media.getAttribute('data-resume-media-ready') === '1') {
            clearScheduledVideoResumeSave(resumeFile);
            const ms = Math.max(0, Math.round((Number(media.currentTime) || 0) * 1000));
            saveVideoResumePosition(resumeFile, ms).catch(function () {});
          } else if (resumeFile) {
            clearScheduledVideoResumeSave(resumeFile);
          }
          media.setAttribute('data-resume-closing', '1');
          media.pause();
          media.src = '';
        }
        if (key) {
          openedPreviewWindows.delete(key);
        } else {
          openedPreviewWindows.forEach(function (value, k) {
            if (value === win) {
              openedPreviewWindows.delete(k);
            }
          });
        }
        if (win.parentNode) {
          win.parentNode.removeChild(win);
        }
      }

      function clearLocalDiskDropTarget() {
        if (!localDiskExplorer) {
          return;
        }
        localDiskExplorer.querySelectorAll('.local-disk-drop-target').forEach(function (node) {
          node.classList.remove('local-disk-drop-target');
        });
      }

      function getFolderLockAncestorPath(path) {
        const text = String(path || '');
        if (!text) {
          return '';
        }
        const parts = text.split('/');
        for (let i = parts.length; i > 0; i -= 1) {
          const candidate = parts.slice(0, i).join('/');
          const node = findFolderNodeByPath(candidate);
          if (node && node.locked) {
            return candidate;
          }
        }
        return '';
      }

      function shuffleList(items) {
        const list = Array.isArray(items) ? items.slice() : [];
        for (let i = list.length - 1; i > 0; i -= 1) {
          const j = Math.floor(Math.random() * (i + 1));
          const temp = list[i];
          list[i] = list[j];
          list[j] = temp;
        }
        return list;
      }

      function canLockTagNode(node, level) {
        return !!(node && node.id) && !isProtectedRestrictedRootTag(node, level);
      }

      function getRootFolderTreeNodesForRender() {
        const list = Array.isArray(folderTreeData) ? folderTreeData : [];
        const fixedNodes = [];
        const recycleNodes = [];
        const sharedNodes = [];
        const otherNodes = [];
        list.forEach(function (node) {
          if (node && isRootFixedFolderPath(node.path)) {
            fixedNodes.push(node);
          } else if (node && isRecycleFolderPath(node.path)) {
            recycleNodes.push(node);
          } else if (node && isSharedRootFolderPath(node.path)) {
            sharedNodes.push(node);
          } else {
            otherNodes.push(node);
          }
        });
        fixedNodes.sort(function (a, b) {
          return sharedFixedFolderOrder((a && a.path) || '') - sharedFixedFolderOrder((b && b.path) || '');
        });
        const rootChildren = fixedNodes.concat(otherNodes);
        return recycleNodes
          .concat(sharedNodes)
          .concat([{
            name: t('根目录'),
            path: '',
            file_count: '',
            children: rootChildren,
            folder_count: rootChildren.length,
            fixed_root: true
          }]);
      }

      async function bindFilesToTag(tagId, fileNames, options) {
        const names = Array.isArray(fileNames) ? fileNames : [];
        const opts = options || {};
        let boundCount = 0;
        for (let i = 0; i < names.length; i += 1) {
          const fileName = String(names[i] || '');
          if (!fileName) {
            continue;
          }
          const result = await bindFileToTag(tagId, fileName, opts);
          if (!result.ok) {
            return {
              ok: false,
              message: result.message,
              boundCount: boundCount
            };
          }
          boundCount += 1;
        }
        return { ok: true, boundCount: boundCount };
      }

      async function savePreviewImageEdits(win) {
        const img = win ? win.querySelector('.preview-image') : null;
        const item = currentPreviewImageItem(win);
        if (!img || !item || !item.file) {
          return;
        }
        const mime = imageEditMimeForFile(item.file);
        if (!mime) {
          setImageEditHint(win, 'GIF 动图暂不支持编辑保存，请先转换为 PNG/JPG。', true);
          return;
        }
        if (!win.__imageDirty) {
          setImageEditHint(win, '图片尚未编辑，无需保存。');
          return;
        }
        try {
          const canvas = drawImageElementToCanvas(img);
          const blob = await canvasToBlob(canvas, mime, 0.92);
          const form = new FormData();
          form.append('image', blob, localDiskBaseName(item.file));
          let url = api.imageSave + '?file=' + encodeURIComponent(item.file);
          if (item.local) {
            url += '&local=1';
            url = appendLocalDirPassword(appendFilePassword(url, item.file, true), localDiskParentPath(item.file));
          } else {
            url = appendFilePassword(withFolderPassword(url, parentFolderPathFromFilePath(item.file)), item.file, false);
          }
          setImageEditHint(win, '正在保存...');
          await fetchJson(url, { method: 'POST', body: form });
          win.__imageDirty = false;
          win.__imageBaseCanvas = null;
          win.__imageScale = 1;
          img.src = imagePreviewUrlForItem(item);
          showStatus('图片编辑结果已保存：' + item.file, 'ok');
          setImageEditHint(win, '已保存。');
        } catch (err) {
          setImageEditHint(win, '保存失败：' + err.message, true);
          showStatus('保存图片失败：' + err.message, 'err');
        }
      }

      function bringToFront(win) {
        if (!win) {
          return;
        }
        previewZ += 1;
        win.style.zIndex = String(previewZ);
      }

      function getLocalDiskDropTarget(e) {
        const item = e.target.closest('[data-local-drop-target]');
        if (!item || !localDiskExplorer || !localDiskExplorer.contains(item)) {
          return null;
        }
        const path = decodeURIComponent(item.getAttribute('data-local-drop-target') || '');
        return path ? { element: item, path: path } : null;
      }

      function folderLockIconHtml(node) {
        if (!node || !node.locked) {
          return '';
        }
        const path = String(node.path || '');
        if (unlockedFolderPasswords.has(path)) {
          return '<span class="folder-lock-icon unlocked" title="' + escapeHtml(t('点击重新加锁')) + '" aria-label="' + escapeHtml(t('点击重新加锁')) + '" data-folder-lock-toggle="' + escapeHtml(path) + '"><span class="folder-lock-shackle"></span><span class="folder-lock-body"></span></span>';
        }
        return '<span class="folder-lock-icon" title="' + escapeHtml(t('已加锁')) + '" aria-label="' + escapeHtml(t('已加锁')) + '"><span class="folder-lock-shackle"></span><span class="folder-lock-body"></span></span>';
      }

      function openAudioPlaylistWindow(tagId, tagName, mode, files) {
        const playlistKey = 'audio-playlist:' + String(tagId || '');
        const existed = openedPreviewWindows.get(playlistKey);
        if (existed && existed.isConnected) {
          closePreviewWindow(existed, playlistKey);
        }

        const displayFiles = sortAudioFilesForPlaylist(files);
        if (!displayFiles.length) {
          throw new Error(t('该标签下没有可播放的音频文件'));
        }

        const win = document.createElement('div');
        win.className = 'floating-preview audio-playlist-preview';
        previewZ += 1;
        win.style.zIndex = String(previewZ);

        win.innerHTML =
          '<div class="preview-head">' +
            '<div class="preview-title">' + escapeHtml((AUDIO_PLAY_MODE_LABELS[mode] || t('音频播放')) + t('：') + (tagName || t('音频标签'))) + '</div>' +
            '<div class="preview-head-actions">' +
              '<button class="preview-window-btn" type="button" data-window-action="minimize" title="' + escapeHtml(t('最小化')) + '" aria-label="' + escapeHtml(t('最小化')) + '">−</button>' +
              '<button class="preview-window-btn" type="button" data-window-action="maximize" title="' + escapeHtml(t('最大化')) + '" aria-label="' + escapeHtml(t('最大化')) + '">□</button>' +
              '<button class="preview-close" type="button" title="关闭" aria-label="关闭">×</button>' +
            '</div>' +
          '</div>' +
          '<div class="preview-body audio-playlist-body">' +
            '<div class="audio-playlist-meta">' +
              '<div class="audio-playlist-mode-group">' + renderAudioPlayModeButtons(mode) + '</div>' +
              '<div class="audio-playlist-current-name"></div>' +
            '</div>' +
            '<audio class="preview-audio audio-playlist-player" controls preload="metadata"></audio>' +
            '<div class="audio-playlist-panel">' +
              '<div class="audio-playlist-panel-head">' +
                '<div class="audio-playlist-summary">' + t('共 ') + '<span class="audio-playlist-count"></span>' + t(' 个音频文件') + '</div>' +
                '<button type="button" class="audio-playlist-toggle-btn" title="' + escapeHtml(t('收起虚拟磁盘')) + '" aria-label="' + escapeHtml(t('收起虚拟磁盘')) + '">▾</button>' +
              '</div>' +
              '<div class="audio-playlist-items"></div>' +
            '</div>' +
          '</div>';

        previewLayer.appendChild(win);
        centerPreviewWindow(win);
        openedPreviewWindows.set(playlistKey, win);

        const audioEl = win.querySelector('.audio-playlist-player');
        const currentNameEl = win.querySelector('.audio-playlist-current-name');
        const countEl = win.querySelector('.audio-playlist-count');
        const itemsEl = win.querySelector('.audio-playlist-items');
        const closeBtn = win.querySelector('.preview-close');
        const head = win.querySelector('.preview-head');
        const minimizeBtn = win.querySelector('[data-window-action="minimize"]');
        const maximizeBtn = win.querySelector('[data-window-action="maximize"]');
        const modeGroupEl = win.querySelector('.audio-playlist-mode-group');
        const playlistPanelEl = win.querySelector('.audio-playlist-panel');
        const playlistToggleBtn = win.querySelector('.audio-playlist-toggle-btn');
        let currentIndex = -1;
        let currentFileName = '';
        let currentMode = mode;
        let randomQueue = [];
        let isPlaylistCollapsed = false;

        if (countEl) {
          countEl.textContent = String(displayFiles.length);
        }

        function refillRandomQueue(excludeName) {
          const excluded = String(excludeName || '');
          randomQueue = shuffleList(displayFiles.filter(function (file) {
            return getFilePath(file) !== excluded;
          }));
          if (!randomQueue.length && displayFiles.length === 1) {
            randomQueue = displayFiles.slice();
          }
        }

        function snapshotWindowRect() {
          if (win.classList.contains('is-maximized')) {
            return;
          }
          const rect = win.getBoundingClientRect();
          win.dataset.restoreLeft = Math.round(rect.left) + 'px';
          win.dataset.restoreTop = Math.round(rect.top) + 'px';
          win.dataset.restoreWidth = Math.round(rect.width) + 'px';
          win.dataset.restoreHeight = Math.round(rect.height) + 'px';
        }

        function restoreAudioWindowGeometry() {
          win.classList.remove('is-minimized', 'is-maximized');
          win.style.transform = 'none';
          win.style.resize = 'both';
          win.style.maxHeight = '90vh';
          win.style.width = win.dataset.restoreWidth || '';
          win.style.height = win.dataset.restoreHeight || '';
          win.style.left = win.dataset.restoreLeft || '';
          win.style.top = win.dataset.restoreTop || '';
          if (!win.dataset.restoreLeft || !win.dataset.restoreTop) {
            centerPreviewWindow(win);
          } else {
            clampWindowPosition(win);
          }
        }

        function syncAudioWindowButtons() {
          if (maximizeBtn) {
            const restoreMode = win.classList.contains('is-minimized') || win.classList.contains('is-maximized');
            maximizeBtn.textContent = restoreMode ? '❐' : '□';
            maximizeBtn.title = restoreMode ? t('复原') : t('最大化');
            maximizeBtn.setAttribute('aria-label', restoreMode ? t('复原') : t('最大化'));
          }
        }

        function syncAudioModeUI() {
          if (modeGroupEl) {
            modeGroupEl.innerHTML = renderAudioPlayModeButtons(currentMode);
          }
        }

        function syncAudioWindowTitle() {
          const titleEl = win.querySelector('.preview-title');
          if (titleEl) {
            titleEl.textContent = (AUDIO_PLAY_MODE_LABELS[currentMode] || t('音频播放')) + t('：') + (tagName || t('音频标签'));
          }
        }

        function syncPlaylistPanelUI() {
          if (playlistPanelEl) {
            playlistPanelEl.classList.toggle('is-collapsed', isPlaylistCollapsed);
          }
          if (playlistToggleBtn) {
            playlistToggleBtn.textContent = isPlaylistCollapsed ? '▸' : '▾';
            playlistToggleBtn.title = isPlaylistCollapsed ? t('展开虚拟磁盘') : t('收起虚拟磁盘');
            playlistToggleBtn.setAttribute('aria-label', isPlaylistCollapsed ? t('展开虚拟磁盘') : t('收起虚拟磁盘'));
          }
        }

        function togglePlaylistPanel() {
          isPlaylistCollapsed = !isPlaylistCollapsed;
          syncPlaylistPanelUI();
        }

        function setAudioPlayMode(nextMode) {
          if (!AUDIO_PLAY_MODE_LABELS[nextMode] || currentMode === nextMode) {
            return;
          }
          currentMode = nextMode;
          if (currentMode === 'random') {
            refillRandomQueue(currentFileName);
          }
          syncAudioModeUI();
          syncAudioWindowTitle();
        }

        function minimizeAudioWindow() {
          if (win.classList.contains('is-minimized')) {
            return;
          }
          snapshotWindowRect();
          win.classList.remove('is-maximized');
          win.classList.add('is-minimized');
          win.style.transform = 'none';
          win.style.resize = 'none';
          win.style.height = '';
          win.style.maxHeight = '';
          clampWindowPosition(win);
          syncAudioWindowButtons();
        }

        function maximizeAudioWindow() {
          if (win.classList.contains('is-minimized') || win.classList.contains('is-maximized')) {
            restoreAudioWindowGeometry();
            syncAudioWindowButtons();
            return;
          }
          snapshotWindowRect();
          win.classList.add('is-maximized');
          win.classList.remove('is-minimized');
          win.style.transform = 'none';
          win.style.resize = 'none';
          win.style.left = '22px';
          win.style.top = '22px';
          win.style.width = 'calc(100vw - 44px)';
          win.style.height = 'calc(100vh - 44px)';
          win.style.maxHeight = 'calc(100vh - 44px)';
          syncAudioWindowButtons();
        }

        function updateCurrentTrack(index, fileName) {
          currentIndex = index;
          currentFileName = String(fileName || '');
          if (currentNameEl) {
            currentNameEl.textContent = currentFileName;
          }
          renderAudioPlaylistItems(itemsEl, displayFiles, currentFileName);
          const activeBtn = itemsEl ? itemsEl.querySelector('.audio-playlist-item.active') : null;
          if (activeBtn) {
            activeBtn.scrollIntoView({ block: 'nearest' });
          }
        }

        function playIndex(index, autoplay) {
          const current = displayFiles[index];
          if (!current || !audioEl) {
            return;
          }
          const fileName = getFilePath(current);
          updateCurrentTrack(index, fileName);
          const isLocal = !!current.local;
          audioEl.src = (isLocal ? localDiskDownloadUrl(fileName) : downloadUrlForFile(fileName, true)) + '&v=' + Date.now();
          audioEl.load();
          if (autoplay !== false) {
            audioEl.play().catch(function () {});
          }
        }

        function playFileByName(fileName, autoplay) {
          const targetName = String(fileName || '');
          const nextIndex = displayFiles.findIndex(function (file) {
            return getFilePath(file) === targetName;
          });
          if (nextIndex >= 0) {
            playIndex(nextIndex, autoplay);
          }
        }

        function playNextRandomTrack() {
          if (!displayFiles.length) {
            return;
          }
          if (!randomQueue.length) {
            refillRandomQueue(currentFileName);
          }
          const nextFile = randomQueue.shift();
          if (!nextFile) {
            return;
          }
          playFileByName(getFilePath(nextFile), true);
        }

        function moveNextTrack() {
          if (!displayFiles.length) {
            return;
          }
          if (currentMode === 'loop') {
            playIndex((currentIndex + 1) % displayFiles.length, true);
            return;
          }
          if (currentMode === 'random') {
            if (displayFiles.length === 1) {
              playIndex(0, true);
              return;
            }
            playNextRandomTrack();
            return;
          }
          if (currentIndex + 1 < displayFiles.length) {
            playIndex(currentIndex + 1, true);
          }
        }

        if (itemsEl) {
          itemsEl.addEventListener('click', function (e) {
            const itemBtn = e.target.closest('.audio-playlist-item[data-playlist-index]');
            if (!itemBtn) {
              return;
            }
            const nextIndex = Number(itemBtn.getAttribute('data-playlist-index') || '-1');
            if (nextIndex >= 0) {
              playIndex(nextIndex, true);
              if (currentMode === 'random') {
                refillRandomQueue(getFilePath(displayFiles[nextIndex] || {}));
              }
            }
          });
        }

        if (modeGroupEl) {
          modeGroupEl.addEventListener('click', function (e) {
            const modeBtn = e.target.closest('.audio-mode-btn[data-audio-mode]');
            if (!modeBtn) {
              return;
            }
            const nextMode = modeBtn.getAttribute('data-audio-mode') || '';
            setAudioPlayMode(nextMode);
          });
        }

        if (playlistToggleBtn) {
          playlistToggleBtn.addEventListener('click', function () {
            togglePlaylistPanel();
          });
        }

        if (audioEl) {
          audioEl.addEventListener('ended', function () {
            moveNextTrack();
          });
        }

        if (minimizeBtn) {
          minimizeBtn.addEventListener('click', function () {
            minimizeAudioWindow();
          });
        }

        if (maximizeBtn) {
          maximizeBtn.addEventListener('click', function () {
            maximizeAudioWindow();
          });
        }

        closeBtn.addEventListener('click', function () {
          closePreviewWindow(win, playlistKey);
        });

        win.addEventListener('mousedown', function () {
          bringToFront(win);
        });

        head.addEventListener('mousedown', function (e) {
          if (e.target.closest('.preview-close') || e.target.closest('.preview-window-btn') || win.classList.contains('is-maximized')) {
            return;
          }
          snapshotWindowRect();
          const rect = win.getBoundingClientRect();
          bringToFront(win);
          activeDrag = {
            win: win,
            startX: e.clientX,
            startY: e.clientY,
            left: rect.left,
            top: rect.top
          };
          win.style.transform = 'none';
          e.preventDefault();
        });

        head.addEventListener('dblclick', function (e) {
          if (e.target.closest('.preview-close') || e.target.closest('.preview-window-btn')) {
            return;
          }
          e.preventDefault();
          bringToFront(win);
          maximizeAudioWindow();
        });

        syncAudioWindowButtons();

        syncAudioModeUI();
        syncAudioWindowTitle();
        syncPlaylistPanelUI();

        if (currentMode === 'random') {
          refillRandomQueue('');
          playNextRandomTrack();
        } else {
          playIndex(0, true);
        }
      }

      function clearExpandedNodeState(node) {
        if (!node) {
          return;
        }
        expandedTagNodeIds.delete(node.id);
        const children = Array.isArray(node.children) ? node.children : [];
        for (let i = 0; i < children.length; i += 1) {
          clearExpandedNodeState(children[i]);
        }
      }

      async function deleteCurrentFolder() {
        if (!activeFolderPath) {
          return;
        }
        if (isRecycleRootFolderPath(activeFolderPath)) {
          return;
        }
        if (isRecycleFolderPath(activeFolderPath)) {
          const confirmedPermanent = await askDeleteConfirmDialog({
            title: t('彻底删除目录'),
            description: t('此操作不可恢复，目录及其全部内容将被永久删除。'),
            highlight: activeFolderPath,
            confirmText: t('彻底删除')
          });
          if (!confirmedPermanent) {
            return;
          }
          await fetchJson(api.del + '?file=' + encodeURIComponent(activeFolderPath));
          activeFolderPath = RECYCLE_FOLDER_NAME;
          ensureFolderPathExpanded(activeFolderPath);
          await loadFiles();
          showStatus(t('回收站目录已彻底删除'), 'warn');
          return;
        }
        if (!(await ensureFolderUnlocked(activeFolderPath))) {
          return;
        }
        const confirmed = await askDeleteConfirmDialog({
          title: t('删除目录'),
          description: t('目录及其全部内容将移入回收站。'),
          highlight: activeFolderPath,
          note: t('移入回收站后仍可在回收站中恢复或彻底删除。'),
          confirmText: t('移入回收站')
        });
        if (!confirmed) {
          return;
        }
        await fetchJson(withFolderPassword(api.folderDelete + '?path=' + encodeURIComponent(activeFolderPath), activeFolderPath), { method: 'POST' });
        activeFolderPath = '';
        renderFolderTree();
        renderFiles(activeSourceFiles);
        showStatus(t('文件夹已移入回收站'), 'warn');
      }

      function inferFileTypeLabel(file) {
        if (!file) {
          return t('文件');
        }
        if (file.directory) {
          return t('文件夹');
        }
        const name = String(file.name || getFilePath(file) || '');
        if (isVideoName(name)) return t('视频');
        if (isAudioName(name)) return t('音频');
        if (isImageName(name)) return t('图片');
        if (isPdfName(name)) return 'PDF';
        if (typeof isOfficeName === 'function' && isOfficeName(name)) return 'Office';
        if (typeof isXMindName === 'function' && isXMindName(name)) return 'XMind';
        if (window.WebCoolHtml && window.WebCoolHtml.isHtmlName(name)) return 'HTML';
        if (isTextName(name)) return t('文本');
        const dot = name.lastIndexOf('.');
        return dot >= 0 && dot < name.length - 1 ? name.slice(dot + 1).toUpperCase() : t('文件');
      }

      function clearLocalDiskRenameClickTimer() {
        if (localDiskRenameClickTimer) {
          clearTimeout(localDiskRenameClickTimer);
          localDiskRenameClickTimer = null;
        }
      }

      function updateSortIndicator() {
        const key = sortKey.value || 'name';
        const order = sortOrder.value || 'asc';
        sortButtons.forEach(btn => {
          const btnKey = btn.getAttribute('data-sort-key');
          const arrow = btn.querySelector('.arrow');
          if (btnKey === key) {
            btn.classList.add('active');
            if (arrow) {
              arrow.textContent = order === 'desc' ? '↓' : '↑';
            }
          } else {
            btn.classList.remove('active');
            if (arrow) {
              arrow.textContent = '-';
            }
          }
        });
      }

function setUnlockedFolderPassword(path, password) {
        const target = String(path || '');
        if (!target) {
          return;
        }
        unlockedFolderPasswords.set(target, String(password || ''));
        saveUnlockedFolderPasswords();
      }

      async function handleFolderContextAction(action, path) {
        if (!action) {
          return;
        }
        if (action === 'empty-recycle') {
          await emptyRecycleBin();
          return;
        }
        if (!path && action === 'create') {
          await createFolderAtPath('');
          await loadFiles();
          return;
        }
        if (!path && action !== 'paste') {
          return;
        }
        if (action === 'copy') {
          remoteDiskClipboardPath = path;
          remoteDiskClipboardDirectory = true;
          remoteDiskClipboardPaths = [path];
          remoteDiskClipboardDirectoryFlags = [true];
          showStatus(t('已拷贝远程目录路径：') + path, 'ok');
          return;
        }
        if (action === 'paste') {
          const clipboardPaths = remoteDiskClipboardPaths.length ? remoteDiskClipboardPaths.slice() : (remoteDiskClipboardPath ? [remoteDiskClipboardPath] : []);
          const clipboardDirFlags = remoteDiskClipboardDirectoryFlags.length ? remoteDiskClipboardDirectoryFlags.slice() : clipboardPaths.map(function () { return !!remoteDiskClipboardDirectory; });
          if (!clipboardPaths.length) {
            showStatus(t('没有可粘贴的远程文件或目录'), 'err');
            return;
          }
          if (clipboardPaths.some(function (source, index) { return !!clipboardDirFlags[index] && isSameOrChildFolderPath(source, path); })) {
            showStatus(t('不能将目录粘贴到自身或其子目录中'), 'err');
            return;
          }
          if (!(await ensureFolderUnlocked(path))) {
            return;
          }
          const buildCopyUrl = function (sourcePath, sourceDirectory, overwrite) {
            let url = sourceDirectory
              ? (api.folderCopy + '?async=1&path=' + encodeURIComponent(sourcePath) + '&folder=' + encodeURIComponent(path))
              : (api.fileCopy + '?async=1&file=' + encodeURIComponent(sourcePath) + '&folder=' + encodeURIComponent(path));
            if (overwrite) {
              url += '&overwrite=1';
            }
            if (sourceDirectory) {
              url = withFolderPassword(url, sourcePath);
            } else {
              url = withFolderPassword(url, parentFolderPathFromFilePath(sourcePath));
              url = appendFilePassword(url, sourcePath, false);
            }
            return withFolderPassword(url, path, 'target_folder_password');
          };
          for (let i = 0; i < clipboardPaths.length; i += 1) {
            const sourcePath = clipboardPaths[i];
            const sourceDirectory = !!clipboardDirFlags[i];
            let result = null;
            try {
              result = await fetchJson(buildCopyUrl(sourcePath, sourceDirectory, false), { method: 'POST' });
            } catch (err) {
              if (err && err.status === 409 && /same name|already contains|already exists/i.test(String(err.message || ''))) {
                const confirmed = confirm(t('目标目录下已存在同名文件或目录，是否覆盖？'));
                if (!confirmed) {
                  showStatus(t('已取消粘贴'), 'warn');
                  return;
                }
                result = await fetchJson(buildCopyUrl(sourcePath, sourceDirectory, true), { method: 'POST' });
              } else {
                throw err;
              }
            }
            const copiedPath = String((result && result.path) || '');
            closeFolderContextMenu();
            closeFileContextMenu();
            setRemoteCopyProgress(0, t('准备粘贴...'), { name: copiedPath || sourcePath, state: 'running', progress: 0, size: 0, copied: 0 });
            const copyResult = await pollRemoteCopyTask(String((result && result.task_id) || ''), path, copiedPath, sourceDirectory);
            if (String((copyResult && copyResult.state) || '') !== 'done') {
              return;
            }
          }
          remoteDiskClipboardPaths = [];
          remoteDiskClipboardDirectoryFlags = [];
          remoteDiskClipboardPath = '';
          remoteDiskClipboardDirectory = false;
          const targetLabel = getFolderLabel(path);
          showStatus(clipboardPaths.length > 1 ? (t('已粘贴 ') + clipboardPaths.length + t(' 个项目到：') + targetLabel) : (t('已粘贴到：') + targetLabel), 'ok');
          return;
        }
        if (action === 'lock') {
          if (!(await ensureFolderUnlocked(path))) {
            return;
          }
          const password = await askLockPassword({
            title: t('加锁目录'),
            description: t('请为目录「') + path + t('」设置锁密码。加锁后需要输入密码才能访问。'),
            placeholder: t('请输入新锁密码'),
            errorMessage: t('加锁失败，请重新输入密码。'),
            statusErrorMessage: t('加锁失败：密码错误或验证失败')
          });
          if (password === null) {
            return;
          }
          await fetchJson(withFolderPassword(api.folderLock + '?path=' + encodeURIComponent(path) + '&password=' + encodeURIComponent(password), path), { method: 'POST' });
          setFolderNodeLockedState(path, true);
          setUnlockedFolderPassword(path, password);
          await loadFiles();
          showStatus(t('目录已加锁：') + path, 'ok');
          return;
        }
        if (action === 'session-unlock') {
          const password = await askLockPassword({
            title: t('解锁目录'),
            description: t('请输入目录「') + path + t('」的锁密码。'),
            onSubmit: async function (passwordText) {
              return fetchJson(api.folderLockVerify + '?path=' + encodeURIComponent(path) + '&password=' + encodeURIComponent(passwordText), { method: 'POST' });
            }
          });
          if (password === null) {
            return;
          }
          setUnlockedFolderPassword(path, password);
          activeFolderPath = path;
          ensureFolderPathExpanded(activeFolderPath);
          await loadFiles();
          showStatus(t('目录已解锁（当前会话）：') + path, 'ok');
          return;
        }
        if (action === 'session-lock') {
          await relockFolderInSession(path);
          return;
        }
        if (action === 'remove-lock') {
          const password = await askLockPassword({
            title: t('去锁目录'),
            description: t('请输入目录「') + path + t('」的锁密码。验证成功后会永久移除该目录锁。'),
            errorMessage: t('密码错误或去锁失败，请重新输入。'),
            statusErrorMessage: t('去锁失败：密码错误或验证失败'),
            onSubmit: async function (passwordText) {
              await fetchJson(api.folderUnlock + '?path=' + encodeURIComponent(path) + '&password=' + encodeURIComponent(passwordText), { method: 'POST' });
            }
          });
          if (password === null) {
            return;
          }
          deleteUnlockedFolderPassword(path);
          await loadFiles();
          showStatus(t('目录已去锁：') + path, 'ok');
          return;
        }
        if (!(await ensureFolderUnlocked(path))) {
          return;
        }
        activeFolderPath = path;
        if (action === 'create') {
          await createFolderAtCurrentPath();
          await loadFiles();
        } else if (action === 'delete') {
          await deleteCurrentFolder();
          await loadFiles();
        } else if (action === 'rename') {
          startFolderRename(path);
        }
      }

      function closeConfirmDialog(value) {
        if (!confirmDialog) {
          return;
        }
        confirmDialog.hidden = true;
        document.body.style.overflow = '';
        resetConfirmDialogPresentation();
        const resolver = activeConfirmDialogResolver;
        activeConfirmDialogResolver = null;
        if (resolver) {
          resolver(value);
        }
      }

      function showManualTranscodePrompt(candidates) {
        const list = Array.isArray(candidates) ? candidates : [];
        if (!list.length) {
          return;
        }

        statusBox.className = 'status show warn';
        if (!statusBox.querySelector('.transcode-list')) {
          statusBox.innerHTML =
            '<div>' + t('检测到以下视频为了兼容浏览器建议进行兼容处理，请选择是否处理：') + '</div>' +
            '<div class="transcode-list"></div>';
        }

        const listEl = statusBox.querySelector('.transcode-list');
        if (!listEl) {
          return;
        }

        list.forEach(function (item) {
          const name = String(item.name || '');
          if (!name) {
            return;
          }
          const reason = String(item.reason || t('浏览器兼容性不足'));
          const encoded = encodeURIComponent(name);
          let row = statusBox.querySelector('[data-transcode-item="' + encoded + '"]');
          if (row) {
            const reasonEl = row.querySelector('.transcode-item-reason');
            if (reasonEl && !getTranscodeTaskId(encoded)) {
              reasonEl.textContent = t('原因：') + reason;
            }
            return;
          }
          listEl.insertAdjacentHTML('beforeend',
            '<div class="transcode-item" data-transcode-item="' + encoded + '">' +
              '<div class="transcode-item-head">' +
                '<div>' +
                  '<div class="transcode-item-name">' + escapeHtml(name) + '</div>' +
                  '<div class="transcode-item-reason">' + t('原因：') + escapeHtml(reason) + '</div>' +
                '</div>' +
                '<div class="transcode-actions">' +
                  buildTranscodeActionButtons(encoded, item) +
                  '<button type="button" class="transcode-cancel-btn" data-cancel-file="' + encoded + '">' + t('不转换') + '</button>' +
                  '<button type="button" class="transcode-confirm-btn" data-confirm-transcode="' + encoded + '" hidden>' + t('确认') + '</button>' +
                  '<div class="transcode-progress"><div class="transcode-progress-fill" data-progress-fill="' + encoded + '"></div></div>' +
                  '<span class="transcode-progress-text" data-progress-text="' + encoded + '">' + t('等待确认') + '</span>' +
                '</div>' +
              '</div>' +
            '</div>');
        });
      }

      function isCopyTaskWindowMode(mode) {
        const value = String(mode || '');
        return value === 'remote-copy' || value === 'local-copy';
      }

      function imagePreviewUrlForItem(item) {
        if (!item || !item.file) {
          return '';
        }
        const url = item.local ? localDiskDownloadUrl(item.file) : downloadUrlForFile(item.file, true);
        return url + '&v=' + Date.now();
      }

      async function handleLocalDiskFileDeleteOrRemove(path) {
        const filePath = String(path || '');
        if (!filePath) {
          return;
        }
        const confirmed = await askDeleteConfirmDialog({
          title: t('删除'),
          description: t('文件将移至系统回收站。'),
          highlight: filePath,
          confirmText: t('移除')
        });
        if (!confirmed) {
          return;
        }
        resetStatus();
        await fetchJson(appendLocalDirPassword(appendFilePassword(api.localDiskDelete + '?path=' + encodeURIComponent(filePath), filePath, true), localDiskParentPath(filePath)), { method: 'POST' });
        selectedLocalDiskPaths.delete(filePath);
        showStatus(t('本地文件已删除：') + filePath, 'warn');
        await loadLocalDisk(activeLocalDiskPath || localDiskParentPath(filePath), {
          resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, activeLocalDiskPath || localDiskParentPath(filePath))
        });
      }

      async function controlAdminStorageMigration(action) {
        const taskId = String(activeAdminStorageMigrateTaskId || '');
        if (!taskId) { return; }
        const endpoint = activeAdminStorageProgressMode === 'backup'
          ? api.adminStorageBackupSyncControl
          : api.adminStorageMigrateControl;
        await fetchJson(endpoint + '?task_id=' + encodeURIComponent(taskId) + '&action=' + encodeURIComponent(action), { method: 'POST' });
      }

      function setUnlockedFilePassword(path, local, password) {
        unlockedFilePasswords.set(fileLockKey(path, local), String(password || ''));
        saveUnlockedFilePasswords();
      }

      function getFolderDragPaths(sourcePath) {
        const source = String(sourcePath || '');
        if (source && selectedFolderPaths.has(source)) {
          return Array.from(selectedFolderPaths);
        }
        return source ? [source] : [];
      }

      function getTagNodeToggleSymbol(node, level) {
        if (level >= TAG_MAX_LEVEL) {
          return '';
        }
        if (!hasTagChildren(node)) {
          return '+';
        }
        return expandedTagNodeIds.has(node.id) ? 'v' : '>';
      }

      function upsertTranscodeTaskItem(item) {
        if (!item || !item.name) {
          return;
        }

        const encoded = encodeURIComponent(String(item.name));
        if (!statusBox.querySelector('.transcode-list')) {
          statusBox.className = 'status show warn';
          statusBox.innerHTML =
            '<div>' + t('检测到以下视频为了兼容浏览器建议进行兼容处理，请选择是否处理：') + '</div>' +
            '<div class="transcode-list"></div>';
        }

        const listEl = statusBox.querySelector('.transcode-list');
        if (!listEl) {
          return;
        }

        let row = statusBox.querySelector('[data-transcode-item="' + encoded + '"]');
        if (!row) {
          row = document.createElement('div');
          row.className = 'transcode-item state-running';
          row.setAttribute('data-transcode-item', encoded);
          row.innerHTML =
            '<div class="transcode-item-head">' +
              '<div>' +
                '<div class="transcode-item-name"></div>' +
                '<div class="transcode-item-reason"></div>' +
              '</div>' +
              '<div class="transcode-actions">' +
                '<button type="button" class="transcode-btn" data-transcode-file="' + encoded + '" data-transcode-mode="auto" disabled>' + t('确认转码') + '</button>' +
                '<button type="button" class="transcode-cancel-btn" data-cancel-file="' + encoded + '">' + t('取消转码') + '</button>' +
                '<button type="button" class="transcode-confirm-btn" data-confirm-transcode="' + encoded + '" hidden>' + t('确认') + '</button>' +
                '<div class="transcode-progress"><div class="transcode-progress-fill" data-progress-fill="' + encoded + '"></div></div>' +
                '<span class="transcode-progress-text" data-progress-text="' + encoded + '"></span>' +
              '</div>' +
            '</div>';
          listEl.appendChild(row);
        }

        const title = row.querySelector('.transcode-item-name');
        if (title) {
          title.textContent = String(item.name);
        }

        const reason = row.querySelector('.transcode-item-reason');
        if (reason) {
          reason.textContent = t('状态：') + String(item.message || t('后台转码中'));
        }

        setTranscodeTaskId(encoded, String(item.task_id || ''));
        setTranscodeButtons(encoded, { startDisabled: true, cancelDisabled: !!item.cancel_requested });
        setTranscodeVisualState(encoded, item.cancel_requested ? 'cancelled' : 'running');
        updateTranscodeProgress(encoded, Number(item.progress || 0), String(item.message || '转码中'));
      }

      function localImportFileStateText(state) {
        if (state === 'done') { return t('完成'); }
        if (state === 'running') { return t('上传中'); }
        if (state === 'failed') { return t('失败'); }
        if (state === 'cancelled') { return t('已取消'); }
        return t('等待');
      }

      function listHasFilePath(list, filePath, local) {
        const target = String(filePath || '');
        if (!target) {
          return false;
        }
        return (Array.isArray(list) ? list : []).some(function (item) {
          if (!item) {
            return false;
          }
          if (!!item.local !== !!local) {
            return false;
          }
          const currentPath = local ? String(item.path || '') : String(getFilePath(item) || '');
          return currentPath === target;
        });
      }

      function updateLocalDiskBulkRemoveButton() {
        const disabled = getSelectedLocalDiskFilePaths().length === 0;
        [localDiskBulkRemoveBtn, localDiskTableBulkRemoveBtn, localDiskBulkTagBtn, localDiskTableBulkTagBtn].forEach(function (btn) {
          if (btn) {
            btn.disabled = disabled;
          }
        });
        if (localDiskImportBtn) {
          localDiskImportBtn.disabled = getSelectedLocalDiskImportPaths().length === 0;
        }
        updateLocalDiskSelectAllState();
      }

      async function loadAdminStoragePickerPath(path) {
        const data = await fetchJson(adminStoragePickerUrl(path));
        activeAdminStorageHomePath = String(data.home_path || activeAdminStorageHomePath || '');
        const nodePath = String(data.path || path || '/');
        const dirs = (Array.isArray(data.items) ? data.items : []).filter(function (item) {
          return item && item.directory;
        }).sort(function (a, b) {
          return String((a && a.name) || '').localeCompare(String((b && b.name) || ''), 'zh-CN');
        });
        adminStoragePickerCache.set(nodePath, dirs);
        if (!adminStoragePickerRootPath) {
          adminStoragePickerRootPath = nodePath;
        }
        return nodePath;
      }

      function deleteUnlockedTagPassword(tagId) {
        const id = String(tagId || '');
        if (!id) {
          return;
        }
        unlockedFilePasswords.delete(tagLockKey(id));
        saveUnlockedFilePasswords();
      }

      function isVideoName(name) {
        return /\.(mp4|avi|mkv|rm|rmvb|mov|wmv|mpg|mpeg)$/i.test(String(name || ''));
      }

      function focusActiveTagRenameInput() {
        if (!tagManager || !activeTagRenameId) {
          return;
        }
        setTimeout(function () {
          if (!tagManager || !activeTagRenameId) {
            return;
          }
          const input = tagManager.querySelector('.tag-rename-input[data-tag-rename-input]');
          if (!input) {
            return;
          }
          input.focus();
          input.select();
        }, 0);
      }

      function resetStatus() {
        transcodeProgressTimers.forEach(function (timer) {
          clearInterval(timer);
        });
        transcodeProgressTimers.clear();
        statusBox.className = 'status';
        statusBox.textContent = '';
      }

      function pollCopyTask(taskId, options) {
        return new Promise(function (resolve, reject) {
          if (!taskId) {
            reject(new Error(t('缺少拷贝任务编号')));
            return;
          }
          const opts = options || {};
          const mode = String(opts.mode || 'remote-copy');
          const copiedPath = String(opts.copiedPath || '');
          const itemName = String(opts.itemName || copiedPath || '');
          const onProgressTick = typeof opts.onProgressTick === 'function' ? opts.onProgressTick : null;
          const onDone = typeof opts.onDone === 'function' ? opts.onDone : null;
          activeRemoteCopyTaskId = String(taskId);
          activeRemoteCopyCancelRequested = false;
          let tick = 0;
          const timer = setInterval(function () {
            fetchJson(api.remoteCopyProgress + '?task_id=' + encodeURIComponent(taskId))
              .then(async function (data) {
                tick += 1;
                const progress = Math.max(0, Math.min(100, Number(data.progress || 0)));
                const state = String(data.state || '');
                const displayName = String(itemName || data.path || remoteDiskClipboardPath || localDiskClipboardPath || '');
                setCopyTaskProgress(mode, progress, (data.message || t('粘贴中')) + ' ' + Math.round(progress) + '%', {
                  name: displayName,
                  state: state === 'done' ? 'done' : (state === 'failed' ? 'failed' : (state === 'cancelled' ? 'cancelled' : 'running')),
                  progress: progress,
                  size: Number(data.total_bytes || 0),
                  copied: Number(data.copied_bytes || 0)
                });
                if (tick % 5 === 0 && onProgressTick) {
                  await onProgressTick(data);
                }
                if (state === 'done') {
                  clearInterval(timer);
                  activeRemoteCopyTaskId = '';
                  activeRemoteCopyCancelRequested = false;
                  finishLocalImportProgress(t('粘贴完成 100%'));
                  if (onDone) {
                    await onDone(data);
                  }
                  resolve(data);
                } else if (state === 'failed' || state === 'cancelled') {
                  clearInterval(timer);
                  activeRemoteCopyTaskId = '';
                  activeRemoteCopyCancelRequested = false;
                  if (state === 'cancelled') {
                    failLocalImportProgress(t('粘贴已取消'));
                    resolve(data);
                  } else {
                    failLocalImportProgress(data.error || t('粘贴失败'));
                    reject(new Error(data.error || t('粘贴失败')));
                  }
                }
              })
              .catch(function (err) {
                clearInterval(timer);
                activeRemoteCopyTaskId = '';
                activeRemoteCopyCancelRequested = false;
                reject(err);
              });
          }, 400);
        });
      }

      function currentPreviewImageItem(win) {
        if (!win || !Array.isArray(win.__imageGallery) || !win.__imageGallery.length) {
          return null;
        }
        return win.__imageGallery[Math.max(0, Math.min(Number(win.__imageIndex || 0), win.__imageGallery.length - 1))] || null;
      }

      function invalidateLocalDiskDirLockCache(path) {
        const target = String(path || '');
        if (!target) {
          return;
        }
        localDiskTreeCache.delete(target);
      }

      async function toggleAdminStoragePickerPath(path) {
        const target = String(path || '');
        if (!target) {
          return;
        }
        if (adminStoragePickerExpandedPaths.has(target) && adminStoragePickerCache.has(target)) {
          adminStoragePickerExpandedPaths.delete(target);
          renderAdminStoragePicker();
          return;
        }
        try {
          await loadAdminStoragePickerPath(target);
          adminStoragePickerExpandedPaths.add(target);

          renderAdminStoragePicker();
        } catch (err) {
          showStatus('展开本地目录失败：' + err.message, 'err');
        }
      }

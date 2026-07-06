function appendFilePassword(url, path, local) {
        const password = getFilePassword(path, local);
        if (!password) {
          return url;
        }
        return url + '&file_password=' + encodeURIComponent(password);
      }

      function isRealMediaVideoName(name) {
        return /\.(rm|rmvb|mov|wmv|mpg|mpeg)$/i.test(String(name || ''));
      }

      function isMovVideoName(name) {
        return /\.mov$/i.test(String(name || '').split('?')[0]);
      }

      function startTagRename(tagId) {
        const id = String(tagId || '');
        const meta = findTagMetaById(id);
        if (!meta || !canRenameTagNode(meta.node, meta.level)) {
          return;
        }
        activeTagRenameId = id;
        renderTagTree();
      }

      function setUploadProgress(percent, text) {
        const p = Math.max(0, Math.min(100, Number(percent) || 0));
        uploadProgress.style.display = 'block';
        uploadProgressFill.style.width = p + '%';
        uploadProgressText.textContent = text || (t('上传中 ') + p + '%');
      }

      function setUploadProgressControls(state) {
        const running = state === 'running';
        const paused = state === 'paused';
        const active = running || paused;
        if (uploadProgressPauseBtn) {
          uploadProgressPauseBtn.hidden = !running;
          uploadProgressPauseBtn.disabled = !running;
        }
        if (uploadProgressResumeBtn) {
          uploadProgressResumeBtn.hidden = !paused;
          uploadProgressResumeBtn.disabled = !paused;
        }
        if (uploadProgressCancelBtn) {
          uploadProgressCancelBtn.hidden = !active;
          uploadProgressCancelBtn.disabled = !active;
        }
      }

      function pauseActiveUpload() {
        if (!activeUploadXhr || !activeUploadRunning) {
          return;
        }
        activeUploadPaused = true;
        activeUploadCancelled = false;
        activeUploadRunning = false;
        activeUploadXhr.abort();
        setUploadProgressControls('paused');
        setUploadProgress(0, t('上传已暂停，点击继续将重新开始上传。'));
      }

      function cancelActiveUpload() {
        activeUploadPaused = false;
        activeUploadCancelled = true;
        activeUploadFormData = null;
        if (activeUploadXhr && activeUploadRunning) {
          activeUploadXhr.abort();
        }
        activeUploadRunning = false;
        activeUploadXhr = null;
        setUploadProgressControls('idle');
        setUploadProgress(0, t('上传已取消'));
        setTimeout(hideUploadProgress, 900);
      }

      function pollRemoteCopyTask(taskId, targetPath, copiedPath, copiedDirectory) {
        return pollCopyTask(taskId, {
          mode: 'remote-copy',
          copiedPath: copiedPath,
          onProgressTick: async function () {
            if (!activeFilterTagId) {
              ensureFolderPathExpanded(targetPath);
              await loadFolderTreeState();
            }
          },
          onDone: async function () {
            remoteDiskClipboardPath = '';
            remoteDiskClipboardDirectory = false;
            remoteDiskClipboardPaths = [];
            remoteDiskClipboardDirectoryFlags = [];
            activeFolderPath = targetPath;
            ensureFolderPathExpanded(targetPath);
            if (copiedDirectory && copiedPath) { ensureFolderPathExpanded(copiedPath); }
            await loadFiles();
            if (copiedDirectory && copiedPath) {
              ensureFolderPathExpanded(copiedPath);
              renderFolderTree();
            }
          }
        });
      }

      function setImageEditHint(win, text, error) {
        const hint = win ? win.querySelector('.preview-edit-hint') : null;
        if (!hint) {
          return;
        }
        hint.textContent = String(text || '');
        hint.classList.toggle('error', !!error);
      }

      function localDiskBaseName(path) {
        const raw = String(path || '/');
        const text = raw.replace(/[\/\\]+$/, '') || raw || '/';
        if (text === '/' || /^[A-Za-z]:$/.test(text)) {
          return text === '/' ? '/' : text + '\\';
        }
        const pos = Math.max(text.lastIndexOf('/'), text.lastIndexOf('\\'));
        return pos >= 0 ? text.slice(pos + 1) : text;
      }

      function localDiskDisplayName(path, name) {
        const label = String(name || '');
        if (!label || /[\/\\]/.test(label)) {
          return localDiskBaseName(label || path);
        }
        return label;
      }

      async function loadVideoResumePosition(fileName) {
        if (isMovVideoName(fileName)) {
          return {
            found: false,
            positionMs: 0
          };
        }
        const data = await fetchJson(api.videoResume + '?file=' + encodeURIComponent(fileName || ''));
        return {
          found: !!data.found,
          positionMs: Math.max(0, Number(data.position_ms || 0))
        };
      }

      function isRecycleRootFolderPath(path) {
        return String(path || '') === RECYCLE_FOLDER_NAME;
      }

      function isSharedRootFolderPath(path) {
        return String(path || '') === SHARED_FOLDER_NAME;
      }

      function isRootFixedFolderPath(path) {
        const text = String(path || '');
        return text.indexOf('/') < 0 && SHARED_FIXED_FOLDER_NAMES.indexOf(text) >= 0;
      }

      function isSharedFixedFolderPath(path) {
        const text = String(path || '');
        const prefix = SHARED_FOLDER_NAME + '/';
        if (text.indexOf(prefix) !== 0 || text.indexOf('/', prefix.length) >= 0) {
          return false;
        }
        const name = text.slice(prefix.length);
        return SHARED_FIXED_FOLDER_NAMES.indexOf(name) >= 0;
      }

      function isReservedFixedFolderPath(path) {
        return isRootFixedFolderPath(path) || isSharedFixedFolderPath(path);
      }

      function isFixedFolderParentPath(path) {
        const text = String(path || '');
        return !text || isSharedRootFolderPath(text);
      }

      function getSharedFolderDisplayName(path, fallbackName) {
        const text = String(path || '');
        if (isSharedRootFolderPath(text)) {
          return t('共享目录');
        }
        if (isRootFixedFolderPath(text)) {
          return t(text);
        }
        if (isSharedFixedFolderPath(text)) {
          const prefix = SHARED_FOLDER_NAME + '/';
          return t(text.slice(prefix.length));
        }
        return fallbackName || '';
      }

      function getSharedFixedFolderIconHtml(path) {
        const text = String(path || '');
        if (!isReservedFixedFolderPath(text)) {
          return '';
        }
        const prefix = SHARED_FOLDER_NAME + '/';
        const name = isSharedFixedFolderPath(text) ? text.slice(prefix.length) : text;
        if (name === '视频') {
          return '<span class="folder-tree-icon shared-fixed video" aria-hidden="true">▶</span>';
        }
        if (name === '音频') {
          return '<span class="folder-tree-icon shared-fixed audio" aria-hidden="true">♪</span>';
        }
        if (name === '图片') {
          return '<span class="folder-tree-icon shared-fixed image" aria-hidden="true">🖼</span>';
        }
        if (name === '文档') {
          return '<span class="folder-tree-icon shared-fixed document" aria-hidden="true">📄</span>';
        }
        return '';
      }

      function ensureRootFixedFolderNodes(folders) {
        const list = Array.isArray(folders) ? folders.slice() : [];
        SHARED_FIXED_FOLDER_NAMES.forEach(function (name) {
          const exists = list.some(function (item) {
            const path = String((item && item.path) || (item && item.name) || '');
            return path === name;
          });
          if (!exists) {
            list.push({
              name: name,
              path: name,
              parent_path: '',
              file_count: 0,
              folder_count: 0,
              locked: false,
              children: []
            });
          }
        });
        return list;
      }

      function canUploadLocalDiskToRemoteFolder(path) {
        const folder = String(path || '');
        return !isRecycleFolderPath(folder);
      }

      async function checkVideoAudio(videoFileName) {
        try {
          const headers = authState.token
            ? { Authorization: 'Bearer ' + authState.token }
            : undefined;
          let url = api.probeVideo + '?file=' + encodeURIComponent(videoFileName || '');
          url = withFolderPassword(url, parentFolderPathFromFilePath(videoFileName));
          url = appendFilePassword(url, videoFileName, false);
          const response = await fetch(url, {
            credentials: 'same-origin',
            headers: headers
          });
          if (!response.ok) {
            return { ok: false };
          }
          const data = await response.json();
          return data;
        } catch (err) {
          return { ok: false, error: err.message };
        }
      }

      function getTagFileTypeConstraint(tagId) {
        const rootMeta = getTagRootMeta(tagId);
        if (!rootMeta || !rootMeta.node) {
          return '';
        }
        const rootName = String(rootMeta.node.name || '').trim();
        if (rootName === '视频') {
          return 'video';
        }
        if (rootName === '音频') {
          return 'audio';
        }
        if (rootName === '图片') {
          return 'image';
        }
        if (rootName === '文档') {
          return 'document';
        }
        return '';
      }

      function setFolderNodeLockedState(path, locked) {
        const node = findFolderNodeByPath(path);
        if (node) {
          node.locked = !!locked;
        }
      }

      function compareFiles(a, b, key, order) {
        if (!activeFilterTagId && isFixedFolderParentPath(activeFolderPath)) {
          const fixedRes = compareSharedFixedDirectoryItems(a, b);
          if (fixedRes !== 0) {
            return fixedRes;
          }
        }
        let res = 0;
        if (key === 'size') {
          res = safeSize(a) - safeSize(b);
        } else if (key === 'uploaded_at') {
          res = safeTime(a) - safeTime(b);
        } else {
          const an = String(a.name || '');
          const bn = String(b.name || '');
          res = an.localeCompare(bn, 'zh-CN');
        }
        return order === 'desc' ? -res : res;
      }

      function compareSharedFixedDirectoryItems(a, b) {
        const left = a || {};
        const right = b || {};
        const leftOrder = left.directory ? sharedFixedFolderOrder(getFilePath(left) || left.name) : Number.MAX_SAFE_INTEGER;
        const rightOrder = right.directory ? sharedFixedFolderOrder(getFilePath(right) || right.name) : Number.MAX_SAFE_INTEGER;
        if (leftOrder !== rightOrder) {
          return leftOrder - rightOrder;
        }
        return 0;
      }

      function sharedFixedFolderOrder(path) {
        const text = String(path || '');
        const prefix = SHARED_FOLDER_NAME + '/';
        const name = text.indexOf(prefix) === 0 ? text.slice(prefix.length).split('/')[0] : text.split('/').pop();
        const index = SHARED_FIXED_FOLDER_NAMES.indexOf(name);
        return index >= 0 ? index : Number.MAX_SAFE_INTEGER;
      }

      function replacePreviewImageWithCanvas(win, canvas, options) {
        const img = win ? win.querySelector('.preview-image') : null;
        const item = currentPreviewImageItem(win);
        const opts = options || {};
        const mime = imageEditMimeForFile(item && item.file);
        if (!img || !mime) {
          throw new Error('当前图片格式暂不支持编辑保存');
        }
        if (opts.updateBase !== false) {
          win.__imageBaseCanvas = cloneCanvas(canvas);
          win.__imageScale = 1;
          win.__imageUserZoom = false;
        }
        win.__imageCurrentWidth = canvas.width;
        win.__imageCurrentHeight = canvas.height;
        img.onload = function () {
          fitPreviewImageToWindow(win);
        };
        img.src = canvas.toDataURL(mime, 0.92);
        win.__imageDirty = true;
        win.__imageCropRect = null;
        const cropRect = win.querySelector('.preview-crop-rect');
        if (cropRect) {
          cropRect.hidden = true;
          cropRect.removeAttribute('style');
        }
        updatePreviewImageSizeLabel(win, canvas.width, canvas.height);
      }

      function renderLocalDiskTableView(list) {
        if (!localDiskList || !localDiskTable || !localDiskEmpty) {
          return;
        }
        if (localDiskTableWrap) {
          localDiskTableWrap.hidden = false;
        }
        if (localDiskExplorer) {
          localDiskExplorer.hidden = true;
        }
        if (!list.length) {
          localDiskList.innerHTML = '';
          localDiskTable.style.display = 'none';
          localDiskEmpty.textContent = '当前目录没有可显示的内容。';
          localDiskEmpty.style.display = 'block';
          return;
        }

        localDiskEmpty.style.display = 'none';
        localDiskTable.style.display = 'table';
        localDiskList.innerHTML = list.map(function (item) {
          const path = String((item && item.path) || '');
          const name = localDiskDisplayName(path, item && item.name);
          const encodedPath = encodeURIComponent(path);
          const isDir = !!(item && item.directory);
          const isDriveRoot = /^[A-Za-z]:[\/\\]?$/.test(path);
          const dirLocked = isDir && !!(item && item.locked);
          const dirLockIcon = isDir && !isDriveRoot ? localDirLockIconHtml(path, dirLocked) : '';
          const fileLocked = !isDir && !!(item && item.locked);
          const lockIcon = fileLocked
            ? '<span class="folder-lock-icon file-lock-inline' + (getFilePassword(path, true) ? ' unlocked' : '') + '" title="' + (getFilePassword(path, true) ? '点击重新加锁' : '点击解锁') + '" aria-label="' + (getFilePassword(path, true) ? '点击重新加锁' : '点击解锁') + '"><span class="folder-lock-shackle"></span><span class="folder-lock-body"></span></span>'
            : '';
          const checked = selectedLocalDiskPaths.has(path) ? ' checked' : '';
          const selectedClass = selectedLocalDiskPaths.has(path) ? ' selected-file-row' : '';
          const selectBox = isDir
            ? (isDriveRoot ? '<span class="file-select-tools"><span class="local-disk-select-placeholder"></span></span>' : '<span class="file-select-tools"><input class="local-disk-select" type="checkbox" data-local-select="' + encodedPath + '" aria-label="' + escapeHtml(t('选择 ') + name) + '"' + checked + '></span>')
            : '<span class="file-select-tools"><input class="local-disk-select" type="checkbox" data-local-select="' + encodedPath + '" aria-label="' + escapeHtml(t('选择 ') + name) + '"' + checked + '><button class="file-tag-quick-btn local-file-tag-btn" type="button" data-local-tag-file="' + encodedPath + '" title="' + escapeHtml(t('加入标签')) + '" aria-label="' + escapeHtml(t('加入标签')) + '">🏷</button></span>';
          const isRenamingLocalDiskItem = !isDir && activeLocalDiskRenamePath === path;
          const renameInput = isRenamingLocalDiskItem
            ? '<input class="file-rename-input local-disk-rename-input" type="text" draggable="false" value="' + escapeHtml(name) + '" data-local-disk-rename-path="' + encodedPath + '" aria-label="' + escapeHtml(t('改名文件')) + '">'
            : '';
          const nameHtml = isDir
            ? '<button type="button" class="local-folder-link" data-local-folder="' + encodedPath + '" title="' + escapeHtml(path) + '"><span class="local-folder-icon">📁</span><span>' + escapeHtml(name) + '</span></button>' + dirLockIcon
            : (renameInput || '<button type="button" class="file-name file-name-action local-disk-file-name-action" data-local-disk-file-name-click="' + encodedPath + '">' + escapeHtml(name) + '</button>') + lockIcon;
          const displayName = '<span class="local-disk-table-name-cell">' + selectBox + nameHtml + '</span>';
          const previewBtn = !isDir && isImageName(name)
            ? '<button class="local-preview-btn preview-btn" data-kind="image" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '">预览</button>'
            : '';
          const videoBtn = !isDir && isVideoName(name)
            ? '<button class="local-preview-btn video-btn" data-kind="video" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '">观影</button>'
            : '';
          const audioBtn = !isDir && isAudioName(name)
            ? '<button class="local-preview-btn audio-btn" data-kind="audio" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '">听音</button>'
            : '';
          const textBtn = !isDir && isTextName(name)
            ? '<button class="local-preview-btn text-btn" data-kind="text" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '">查看</button>'
            : '';
          const pdfBtn = !isDir && isPdfName(name)
            ? '<button class="local-preview-btn preview-btn" data-kind="pdf" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '">预览</button>'
            : '';
          const officeBtn = !isDir && isOfficeName(name)
            ? '<button class="local-preview-btn preview-btn" data-kind="office" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '">预览</button>'
            : '';
          const xmindBtn = !isDir && isXMindName(name)
            ? '<button class="local-preview-btn preview-btn" data-kind="mindmap" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '">预览</button>'
            : '';
          const deleteBtn = isDir
            ? (!isDriveRoot && item.empty_directory
              ? '<button class="local-delete-btn delete-btn" data-local-delete="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '">删除</button>'
              : '')
            : '<button class="local-delete-btn delete-btn" data-local-delete="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '" title="移至回收站" aria-label="移至回收站">移除</button>';
          return (
            '<tr class="local-disk-draggable' + selectedClass + '"' + (!isDir ? (' draggable="' + (isRenamingLocalDiskItem ? 'false' : 'true') + '" data-local-drag="' + encodedPath + '"') : '') + (!isDir ? (' data-local-file-context="' + encodedPath + '" data-file-locked="' + (fileLocked ? '1' : '0') + '" data-file-video="' + (isVideoName(name) ? '1' : '0') + '"') : (isDriveRoot ? '' : (' data-local-dir-context="' + encodedPath + '" data-local-dir-locked="' + (dirLocked ? '1' : '0') + '"'))) + '>' +
              '<td>' + displayName + '</td>' +
              '<td>' + (isDir ? '文件夹' : '文件') + '</td>' +
              '<td>' + (isDir ? '-' : (formatNumber(Number(item.size || 0)) + ' 字节')) + '</td>' +
              '<td>' + escapeHtml((item && item.modified_time) || '-') + '</td>' +
              '<td class="actions-cell"><div class="actions">' + previewBtn + videoBtn + audioBtn + textBtn + pdfBtn + officeBtn + xmindBtn + '</div></td>' +
              '<td class="row-danger-action"><div class="danger-actions">' + deleteBtn + '</div></td>' +
            '</tr>'
          );
        }).join('');
        updateLocalDiskBulkRemoveButton();
      }

      function clampLocalDiskDirWidth(width) {
        const explorerRect = localDiskExplorer ? localDiskExplorer.getBoundingClientRect() : null;
        const explorerWidth = explorerRect ? explorerRect.width : 900;
        const maxWidth = Math.max(240, Math.min(680, explorerWidth - 260));
        return Math.round(Math.max(220, Math.min(Number(width) || 270, maxWidth)));
      }

      function collectFolderPaths(nodes, out) {
        const list = Array.isArray(nodes) ? nodes : [];
        const result = Array.isArray(out) ? out : [];
        list.forEach(function (node) {
          const path = String((node && node.path) || '');
          if (path) {
            result.push(path);
          }
          if (node && Array.isArray(node.children) && node.children.length) {
            collectFolderPaths(node.children, result);
          }
        });
        return result;
      }

      function detectCodeLang(name) {
        const n = String(name || '').toLowerCase();
        if (/\.(c|h)$/i.test(n)) return 'c';
        if (/\.(cpp|hpp|cc)$/i.test(n)) return 'cpp';
        if (/\.java$/i.test(n)) return 'java';
        if (/\.(js|jsx)$/i.test(n)) return 'javascript';
        if (/\.(ts|tsx)$/i.test(n)) return 'typescript';
        if (/\.py$/i.test(n)) return 'python';
        if (/\.go$/i.test(n)) return 'go';
        if (/\.sql$/i.test(n)) return 'sql';
        if (/\.(sh|bash)$/i.test(n)) return 'shell';
        if (/\.proto$/i.test(n)) return 'proto';
        return '';
      }

      function getTagRootName(tagId) {
        const rootMeta = getTagRootMeta(tagId);
        if (!rootMeta || !rootMeta.node) {
          return '';
        }
        return String(rootMeta.node.name || '').trim();
      }

      function findFolderTreeNodeElement(path) {
        if (!folderTree) {
          return null;
        }
        const target = String(path || '');
        const nodes = folderTree.querySelectorAll('.folder-tree-node[data-folder-path]');
        for (let i = 0; i < nodes.length; i += 1) {
          const node = nodes[i];
          if (String(node.getAttribute('data-folder-path') || '') === target) {
            return node;
          }
        }
        return null;
      }

      function syncSelectedFilesByCurrentView() {
        const visibleNames = new Set((Array.isArray(currentFiles) ? currentFiles : []).map(function (file) {
          return getFilePath(file);
        }).filter(Boolean));

        Array.from(selectedFileNames).forEach(function (name) {
          if (!visibleNames.has(name)) {
            selectedFileNames.delete(name);
          }
        });
      }

      function rotatePreviewImage(win, direction) {
        const img = win ? win.querySelector('.preview-image') : null;
        if (!img || !img.complete || !img.naturalWidth || !img.naturalHeight) {
          setImageEditHint(win, '图片还没有加载完成，请稍后再试。', true);
          return;
        }
        try {
          const canvas = document.createElement('canvas');
          canvas.width = img.naturalHeight;
          canvas.height = img.naturalWidth;
          const ctx = canvas.getContext('2d');
          if (direction === 'left') {
            ctx.translate(0, canvas.height);
            ctx.rotate(-Math.PI / 2);
          } else {
            ctx.translate(canvas.width, 0);
            ctx.rotate(Math.PI / 2);
          }
          ctx.drawImage(img, 0, 0, img.naturalWidth, img.naturalHeight);
          replacePreviewImageWithCanvas(win, canvas);
          setImageEditHint(win, direction === 'left'
            ? '已向左旋转 90 度，点击“保存到服务”写入文件。'
            : '已向右旋转 90 度，点击“保存到服务”写入文件。');
        } catch (err) {
          setImageEditHint(win, '旋转失败：' + err.message, true);
        }
      }

      function renderLocalDiskSplitView(list) {
        if (!localDiskDirList || !localDiskDirEmpty || !localDiskSplitList || !localDiskSplitTable || !localDiskSplitEmpty) {
          return;
        }
        if (localDiskTableWrap) {
          localDiskTableWrap.hidden = true;
        }
        if (localDiskExplorer) {
          localDiskExplorer.hidden = false;
        }
        const files = list.filter(function (item) { return !(item && item.directory); });

        // Render directories
        if (activeLocalDiskTreeRootPath) {
          localDiskDirEmpty.style.display = 'none';
          localDiskDirList.innerHTML = renderLocalDiskTreeNode(activeLocalDiskTreeRootPath, 0, null);
        } else {
          localDiskDirList.innerHTML = '';
          localDiskDirEmpty.style.display = 'block';
        }

        // Render files
        if (files.length) {
          localDiskSplitEmpty.style.display = 'none';
          localDiskSplitTable.style.display = 'table';
          localDiskSplitList.innerHTML = files.map(buildLocalDiskFileRowHtml).join('');
        } else {
          localDiskSplitList.innerHTML = '';
          localDiskSplitTable.style.display = 'none';
          localDiskSplitEmpty.style.display = 'block';
        }

        updateLocalDiskBulkRemoveButton();
        localDiskEmpty.style.display = 'none';
      }

      function setLocalDiskDirWidth(width, persist) {
        if (!localDiskExplorer) {
          return;
        }
        const nextWidth = clampLocalDiskDirWidth(width);
        localDiskExplorer.style.setProperty('--local-disk-dir-width', nextWidth + 'px');
        if (persist) {
          try {
            window.localStorage.setItem('webcool.localDiskDirWidth', String(nextWidth));
          } catch (_) {}
        }
      }

      function getFolderPasswordForPath(path) {
        const lockedPath = getFolderLockAncestorPath(path);
        if (!lockedPath) {
          return '';
        }
        return unlockedFolderPasswords.get(lockedPath) || '';
      }

      function sortAudioFilesForPlaylist(files) {
        return (Array.isArray(files) ? files : []).slice().sort(function (a, b) {
          return getFilePath(a).localeCompare(getFilePath(b), 'zh-CN');
        });
      }

      async function ensureTagUnlocked(tagId) {
        const id = String(tagId || '');
        if (!id) {
          return false;
        }
        const meta = findTagMetaById(id);
        if (!meta || !meta.node) {
          showStatus(t('标签不存在，可能已被删除'), 'err');
          return false;
        }
        if (!meta.node.locked || getTagPassword(id)) {
          return true;
        }
        return await handleTagLockAction('session-unlock', id, { silentSuccess: true });
      }

      function renderFolderTree() {
        if (!folderTree || !folderTreeEmpty) {
          return;
        }
        syncFolderActionButtons();
        folderTreeEmpty.textContent = t('当前没有文件夹。');
        folderTreeEmpty.style.display = folderTreeData.length ? 'none' : 'block';
        folderTree.innerHTML =
          buildFolderTreeHtml(getRootFolderTreeNodesForRender(), 0);
        syncFolderDropHighlight();
        focusActiveFolderRenameInput();
      }

      function canRenameFileRecord(file) {
        if (!file || file.directory || file.local) {
          return false;
        }
        return !isRecycleFolderPath(activeFolderPath);
      }

      async function downloadPreviewImageEdits(win) {
        const img = win ? win.querySelector('.preview-image') : null;
        const item = currentPreviewImageItem(win);
        if (!img || !item || !item.file) {
          return;
        }
        const mime = imageEditMimeForFile(item.file);
        if (!mime) {
          setImageEditHint(win, 'GIF 动图暂不支持编辑后下载，请先转换为 PNG/JPG。', true);
          return;
        }
        try {
          const canvas = drawImageElementToCanvas(img);
          const blob = await canvasToBlob(canvas, mime, 0.92);
          const url = URL.createObjectURL(blob);
          const link = document.createElement('a');
          link.href = url;
          link.download = localDiskBaseName(item.file) || 'image';
          document.body.appendChild(link);
          link.click();
          link.remove();
          URL.revokeObjectURL(url);
          setImageEditHint(win, '已生成本地下载文件。');
        } catch (err) {
          setImageEditHint(win, '下载失败：' + err.message, true);
        }
      }

      function snapshotPreviewWindowRect(win) {
        if (!win || win.classList.contains('is-maximized')) {
          return;
        }
        const rect = win.getBoundingClientRect();
        win.dataset.restoreLeft = Math.round(rect.left) + 'px';
        win.dataset.restoreTop = Math.round(rect.top) + 'px';
        win.dataset.restoreWidth = Math.round(rect.width) + 'px';
        win.dataset.restoreHeight = Math.round(rect.height) + 'px';
      }

      function localDiskDragPathsFor(sourcePath) {
        const selected = getSelectedLocalDiskPaths();
        if (selected.indexOf(sourcePath) >= 0) {
          return selected;
        }
        return sourcePath ? [sourcePath] : [];
      }

      function closeFolderContextMenu() {
        if (activeFolderContextMenu && activeFolderContextMenu.parentNode) {
          activeFolderContextMenu.parentNode.removeChild(activeFolderContextMenu);
        }
        activeFolderContextMenu = null;
      }

      async function startAudioPlaylistFromTag(tagId, tagName, mode) {
        const files = await loadAudioFilesForTag(tagId);
        if (!files.length) {
          throw new Error(t('该标签下没有音频文件'));
        }
        openAudioPlaylistWindow(tagId, tagName, mode, files);
      }

      function getFileNameSet() {
        const set = new Set();
        allFiles.forEach(function (it) {
          const name = String((it && it.name) || '');
          if (name) {
            set.add(name);
          }
        });
        return set;
      }

      async function restoreCurrentRecycleFolder() {
        if (!activeFolderPath || !isRecycleFolderPath(activeFolderPath) || isRecycleRootFolderPath(activeFolderPath)) {
          return;
        }
        const selected = selectedFolderPaths.has(activeFolderPath)
          ? normalizeFolderMoveSources(Array.from(selectedFolderPaths)).filter(function (path) {
            return isRecycleFolderPath(path) && !isRecycleRootFolderPath(path);
          })
          : [activeFolderPath];
        if (!selected.length) {
          return;
        }
        const confirmed = await askConfirmDialog({
          title: selected.length > 1 ? '批量恢复目录' : '恢复目录',
          description: selected.length > 1
            ? ('确认恢复回收站中选中的 ' + selected.length + ' 个目录？将恢复到原路径（如冲突会自动改名）。')
            : ('确认恢复回收站中的目录「' + activeFolderPath + '」？将恢复到原路径（如冲突会自动改名）。'),
          confirmText: '恢复',
          danger: false
        });
        if (!confirmed) {
          return;
        }
        let restoredCount = 0;
        let lastTargetPath = '';
        for (let i = 0; i < selected.length; i += 1) {
          const result = await fetchJson(api.restore + '?file=' + encodeURIComponent(selected[i]));
          lastTargetPath = String((result && result.path) || '');
          restoredCount += 1;
        }
        clearSelectedFolders();
        activeFolderPath = RECYCLE_FOLDER_NAME;
        ensureFolderPathExpanded(activeFolderPath);
        await loadFiles();
        showStatus(restoredCount > 1
          ? ('已恢复 ' + restoredCount + ' 个目录')
          : ('已恢复目录' + (lastTargetPath ? ('：' + lastTargetPath) : '')),
          'ok');
      }

      async function emptyRecycleBin() {
        const confirmed = await askConfirmDialog({
          title: t('清空回收站'),
          description: t('确认清空回收站中的所有文件和目录？此操作不可恢复。'),
          confirmText: t('清空'),
          danger: true
        });
        if (!confirmed) {
          return;
        }
        resetStatus();
        const result = await fetchJson(api.folderEmpty + '?path=' + encodeURIComponent(RECYCLE_FOLDER_NAME), { method: 'POST' });
        const deletedCount = Number((result && result.count) || 0);
        selectedFileNames.clear();
        selectedFolderPaths.clear();
        if (isRecycleFolderPath(activeFolderPath)) {
          activeFolderPath = RECYCLE_FOLDER_NAME;
        }
        ensureFolderPathExpanded(RECYCLE_FOLDER_NAME);
        await loadFiles();
        showStatus(deletedCount === 0
          ? t('回收站已经是空的')
          : (t('回收站已清空，共删除 ') + deletedCount + t(' 个项目')),
          deletedCount === 0 ? 'ok' : 'warn');
      }

      function getFileTimeText(file, keys) {
        for (let i = 0; i < keys.length; i += 1) {
          const value = file && file[keys[i]];
          if (value !== undefined && value !== null && String(value) !== '') {
            return String(value);
          }
        }
        return '-';
      }

      function refreshRenderedLocalDiskSelection() {
        const roots = [localDiskList, localDiskExplorer].filter(Boolean);
        roots.forEach(function (root) {
          root.querySelectorAll('.local-disk-select[data-local-select]').forEach(function (checkbox) {
            const path = decodeURIComponent(checkbox.getAttribute('data-local-select') || '');
            const selected = selectedLocalDiskPaths.has(path);
            checkbox.checked = selected;
            const row = checkbox.closest('tr, .local-disk-dir-item');
            if (row) {
              row.classList.toggle('selected-file-row', selected);
            }
          });
        });
        updateLocalDiskBulkRemoveButton();
      }

      function setAuthError(message) {
        if (!authError) {
          return;
        }
        authError.textContent = String(message || '');
        authError.hidden = !message;
      }

      function setAuthBootingUi() {
        if (document.documentElement) {
          if (authState.resolved) {
            document.documentElement.classList.remove('auth-booting');
          } else {
            document.documentElement.classList.add('auth-booting');
          }
        }
      }

      function applyAuthUi() {
        const isSetup = !authState.initialized;
        if (authTitle) {
          authTitle.textContent = isSetup ? t('注册管理员') : t('登录');
        }
        if (authDesc) {
          authDesc.textContent = isSetup
            ? t('首次使用，请创建管理员用户名和密码。')
            : t('请输入用户名和密码。');
        }
        if (authSubmitBtn) {
          authSubmitBtn.textContent = isSetup ? t('创建管理员') : t('登录');
        }
        if (authPassword) {
          authPassword.setAttribute('autocomplete', isSetup ? 'new-password' : 'current-password');
        }
        setAuthBootingUi();
        const showAuthenticated = authState.resolved && authState.authenticated;
        if (shell) {
          shell.hidden = !showAuthenticated;
        }
        if (authGate) {
          authGate.hidden = !authState.resolved || !!authState.authenticated;
        }
        if (authUserChip) {
          authUserChip.hidden = !showAuthenticated;
        }
        if (authCurrentUser) {
          authCurrentUser.textContent = authState.username
            ? (authState.username + (authState.admin ? ' · ' + t('管理员') : ''))
            : '';
        }
        const showAdmin = showAuthenticated && authState.admin;
        const adminMenuBtn = document.querySelector('.menu-btn[data-panel="panel-admin"]');
        if (adminMenuBtn) {
          adminMenuBtn.hidden = !showAdmin;
        }
        const usersMenuBtn = document.querySelector('.menu-btn[data-panel="panel-users"]');
        if (usersMenuBtn) {
          usersMenuBtn.hidden = !showAdmin;
        }
        const localDiskMenuBtn = document.querySelector('.menu-btn[data-panel="panel-local-disk"]');
        if (localDiskMenuBtn) {
          localDiskMenuBtn.hidden = !showAuthenticated || !authState.localDiskAllowed;
        }
        const accountMenuBtn = document.querySelector('.menu-btn[data-panel="panel-account"]');
        if (accountMenuBtn) {
          accountMenuBtn.hidden = !showAuthenticated;
        }
        const adminPanel = document.getElementById('panel-admin');
        const usersPanel = document.getElementById('panel-users');
        const accountPanel = document.getElementById('panel-account');
        const localDiskPanel = document.getElementById('panel-local-disk');
        if (!showAdmin
          && ((adminPanel && adminPanel.classList.contains('active'))
            || (usersPanel && usersPanel.classList.contains('active')))) {
          activatePanel('panel-files');
        }
        if ((!showAuthenticated || !authState.localDiskAllowed)
          && localDiskPanel && localDiskPanel.classList.contains('active')) {
          activatePanel('panel-files');
        }
        if (authState.resolved && !authState.authenticated
          && accountPanel && accountPanel.classList.contains('active')) {
          activatePanel('panel-files');
        }
      }

      function requireLoginAgain() {
        authState.resolved = true;
        authState.authenticated = false;
        authState.username = '';
        authState.admin = false;
        authState.localDiskAllowed = true;
        authState.token = '';
        rememberCurrentUserSession('');
        authState.userPrefs = null;
        try {
          sessionStorage.removeItem(USER_PREFS_SESSION_KEY);
        } catch (_) {}
        applyAuthUi();
        if (authUsername) {
          window.setTimeout(function () { authUsername.focus(); }, 30);
        }
      }

      async function refreshAuthStatus() {
        const data = await fetchJson(api.authStatus);
        authState.initialized = !!data.initialized;
        authState.authenticated = !!data.authenticated;
        authState.username = data.username || '';
        authState.admin = !!data.admin;
        authState.localDiskAllowed = data.local_disk_allowed !== false;
        if (!authState.authenticated) {
          authState.token = '';
        }
        rememberCurrentUserSession(authState.authenticated ? authState.username : '');
        if (authState.authenticated) {
          applyUserPreferencesFromAuthData(data);
        } else {
          authState.userPrefs = null;
          try {
            sessionStorage.removeItem(USER_PREFS_SESSION_KEY);
          } catch (_) {}
          applyFontSizePreference('sm');
        }
        authState.resolved = true;
        applyAuthUi();
        return data;
      }

      function buildAuthUrl(base) {
        const username = authUsername ? String(authUsername.value || '').trim() : '';
        const password = authPassword ? String(authPassword.value || '') : '';
        return base + '?username=' + encodeURIComponent(username)
          + '&password=' + encodeURIComponent(password);
      }

      async function submitAuthForm() {
        setAuthError('');
        if (!authUsername || !authPassword) {
          return;
        }
        const username = String(authUsername.value || '').trim();
        const password = String(authPassword.value || '');
        if (!username || !password) {
          setAuthError(t('请输入用户名和密码。'));
          return;
        }
        if (authSubmitBtn) {
          authSubmitBtn.disabled = true;
        }
        try {
          const url = buildAuthUrl(authState.initialized ? api.authLogin : api.authRegister);
          const data = await fetchJson(url, { method: 'POST' });
          authState.initialized = true;
          authState.authenticated = true;
          authState.username = data.username || username;
          authState.admin = !!data.admin;
          authState.localDiskAllowed = data.local_disk_allowed !== false;
          authState.token = data.auth_token || '';
          rememberCurrentUserSession(authState.username);
          cacheUserPrefsSession(authState.username, parseUserPrefsPayload(data));
          if (authPassword) {
            authPassword.value = '';
          }
          authState.resolved = true;
          applyUserPreferencesFromAuthData(data);
          applyAuthUi();
          resetStatus();
          activatePanel('panel-files');
          await loadFiles();
          if (authState.admin) {
            await loadAdminUsers();
          }
        } catch (err) {
          setAuthError(err.message || t('认证失败'));
        } finally {
          if (authSubmitBtn) {
            authSubmitBtn.disabled = false;
          }
        }
      }

      function renderAdminUsers(users) {
        if (!adminUsersList) {
          return;
        }
        const list = (Array.isArray(users) ? users : []).filter(function (user) {
          return user && !user.admin;
        });
        if (!list.length) {
          adminUsersList.innerHTML = '<p class="empty">' + escapeHtml(t('暂无用户')) + '</p>';
          return;
        }
        adminUsersList.innerHTML = list.map(function (user) {
          const name = String(user.username || '');
          return '<div class="admin-user-row">' +
            '<div class="admin-user-main">' +
              '<strong>' + escapeHtml(name) + '</strong>' +
              '<span class="admin-user-role">' + escapeHtml(t('普通用户')) + '</span>' +
            '</div>' +
            '<div class="admin-user-actions">' +
              '<button type="button" class="admin-user-action" data-user-edit="' + escapeHtml(name) + '">' + escapeHtml(t('修改')) + '</button>' +
              '<button type="button" class="admin-user-action danger" data-user-delete="' + escapeHtml(name) + '">' + escapeHtml(t('删除')) + '</button>' +
            '</div>' +
          '</div>';
        }).join('');
      }

      async function loadAdminUsers() {
        if (!authState.admin || !adminUsersList) {
          return;
        }
        try {
          const data = await fetchJson(api.authUsers);
          renderAdminUsers(data.users);
        } catch (err) {
          adminUsersList.innerHTML = '<p class="empty">' + escapeHtml(err.message || t('加载失败')) + '</p>';
        }
      }

      async function createNormalUser() {
        const username = adminNewUsername ? String(adminNewUsername.value || '').trim() : '';
        const password = adminNewPassword ? String(adminNewPassword.value || '') : '';
        if (!username || !password) {
          showStatus(t('请输入用户名和密码。'), 'err');
          return;
        }
        try {
          await fetchJson(api.authUserCreate + '?username=' + encodeURIComponent(username)
            + '&password=' + encodeURIComponent(password), { method: 'POST' });
          if (adminNewUsername) {
            adminNewUsername.value = '';
          }
          if (adminNewPassword) {
            adminNewPassword.value = '';
          }
          showStatus(t('用户已添加：') + username, 'ok');
          await loadAdminUsers();
        } catch (err) {
          showStatus(t('添加用户失败：') + err.message, 'err');
        }
      }

      async function updateNormalUser(username) {
        const currentName = String(username || '').trim();
        if (!currentName) {
          return;
        }
        const nextName = window.prompt(t('请输入新的用户名'), currentName);
        if (nextName === null) {
          return;
        }
        const cleanName = String(nextName || '').trim();
        if (!cleanName) {
          showStatus(t('用户名不能为空'), 'err');
          return;
        }
        const nextPassword = window.prompt(t('请输入新密码；留空则不修改密码'), '');
        if (nextPassword === null) {
          return;
        }
        try {
          await fetchJson(api.authUserUpdate
            + '?username=' + encodeURIComponent(currentName)
            + '&new_username=' + encodeURIComponent(cleanName)
            + '&password=' + encodeURIComponent(String(nextPassword || '')), { method: 'POST' });
          showStatus(t('用户已更新：') + cleanName, 'ok');
          await loadAdminUsers();
        } catch (err) {
          showStatus(t('修改用户失败：') + err.message, 'err');
        }
      }

      async function deleteNormalUser(username) {
        const target = String(username || '').trim();
        if (!target) {
          return;
        }
        if (!window.confirm(t('确认删除用户：') + target + ' ?')) {
          return;
        }
        try {
          await fetchJson(api.authUserDelete + '?username=' + encodeURIComponent(target), { method: 'POST' });
          showStatus(t('用户已删除：') + target, 'ok');
          await loadAdminUsers();
        } catch (err) {
          showStatus(t('删除用户失败：') + err.message, 'err');
        }
      }

      async function changeOwnPassword() {
        const oldPassword = accountOldPassword ? String(accountOldPassword.value || '') : '';
        const newPassword = accountNewPassword ? String(accountNewPassword.value || '') : '';
        const confirmPassword = accountConfirmPassword ? String(accountConfirmPassword.value || '') : '';
        if (!oldPassword || !newPassword || !confirmPassword) {
          showStatus(t('请输入当前密码和新密码。'), 'err');
          return;
        }
        if (newPassword !== confirmPassword) {
          showStatus(t('两次输入的新密码不一致。'), 'err');
          return;
        }
        try {
          const data = await fetchJson(api.authPassword
            + '?old_password=' + encodeURIComponent(oldPassword)
            + '&new_password=' + encodeURIComponent(newPassword), { method: 'POST' });
          authState.token = data.auth_token || authState.token || '';
          if (accountOldPassword) {
            accountOldPassword.value = '';
          }
          if (accountNewPassword) {
            accountNewPassword.value = '';
          }
          if (accountConfirmPassword) {
            accountConfirmPassword.value = '';
          }
          showStatus(t('密码已修改。'), 'ok');
        } catch (err) {
          showStatus(t('修改密码失败：') + err.message, 'err');
        }
      }

      function isFetchNetworkError(err) {
        if (!err) {
          return false;
        }
        const message = String(err.message || '');
        return err.name === 'TypeError'
          || message === 'Failed to fetch'
          || message.indexOf('NetworkError') >= 0
          || message.indexOf('Load failed') >= 0;
      }

      function fetchJsonShouldRetryBody(body) {
        if (!body) {
          return true;
        }
        return !(typeof FormData !== 'undefined' && body instanceof FormData);
      }

      async function fetchJson(url, options, retryLeft) {
        const requestOptions = options ? Object.assign({}, options) : {};
        if (!requestOptions.credentials) {
          requestOptions.credentials = 'same-origin';
        }
        if (authState.token) {
          const headers = new Headers(requestOptions.headers || {});
          if (!headers.has('Authorization')) {
            headers.set('Authorization', 'Bearer ' + authState.token);
          }
          requestOptions.headers = headers;
        }
        const retries = retryLeft == null ? 2 : retryLeft;
        let res;
        try {
          res = await fetch(url, requestOptions);
        } catch (err) {
          if (retries > 0
            && fetchJsonShouldRetryBody(requestOptions.body)
            && isFetchNetworkError(err)) {
            await new Promise(function (resolve) {
              window.setTimeout(resolve, retries === 2 ? 300 : 800);
            });
            return fetchJson(url, options, retries - 1);
          }
          throw err;
        }
        let data = null;
        try {
          data = await res.json();
        } catch (_) {
          data = { ok: false, error: 'invalid json response' };
        }
        if (!res.ok || !data.ok) {
          const err = new Error(data.error || ('http ' + res.status));
          err.status = res.status;
          err.data = data;
          if (res.status === 401 && url !== api.authStatus) {
            requireLoginAgain();
          }
          throw err;
        }
        return data;
      }

function deleteUnlockedFolderPassword(path) {
        const target = String(path || '');
        if (!target) {
          return;
        }
        unlockedFolderPasswords.delete(target);
        saveUnlockedFolderPasswords();
      }

      async function handleFileContextAction(action, path, local) {
        if (!action || !path) {
          return;
        }
        const fileLabel = local ? path : path;
        if (action === 'lock') {
          const password = await askLockPassword({
            title: t('加锁文件'),
            description: t('请为文件「') + fileLabel + t('」设置锁密码。'),
            placeholder: t('请输入新锁密码'),
            errorMessage: t('加锁失败，请重新输入密码。'),
            statusErrorMessage: t('加锁失败：密码错误或验证失败')
          });
          if (password === null) {
            return;
          }
          const url = api.fileLock
            + (local
              ? ('?local=1&path=' + encodeURIComponent(path))
              : ('?file=' + encodeURIComponent(path)))
            + '&password=' + encodeURIComponent(password);
          await fetchJson(url, { method: 'POST' });
          deleteUnlockedFilePassword(path, local);
          setFileLockedState(path, local, true);
          if (activeFilterTagId) {
            renderFiles(activeSourceFiles);
          } else if (local) {
            renderLocalDiskItems(activeLocalDiskItems);
          } else {
            renderFiles(activeSourceFiles);
          }
          showStatus(t('文件已加锁：') + fileLabel, 'ok');
          return;
        }
        if (action === 'session-unlock') {
          const password = await askLockPassword({
            title: t('解锁文件'),
            description: t('请输入文件「') + fileLabel + t('」的锁密码。'),
            onSubmit: async function (passwordText) {
              const url = api.fileLockVerify
                + (local
                  ? ('?local=1&path=' + encodeURIComponent(path))
                  : ('?file=' + encodeURIComponent(path)))
                + '&password=' + encodeURIComponent(passwordText);
              await fetchJson(url, { method: 'POST' });
            }
          });
          if (password === null) {
            return;
          }
          setUnlockedFilePassword(path, local, password);
          if (activeFilterTagId) {
            renderFiles(activeSourceFiles);
          } else if (local) {
            renderLocalDiskItems(activeLocalDiskItems);
          } else {
            renderFiles(activeSourceFiles);
          }
          showStatus(t('文件已解锁（当前会话）：') + fileLabel, 'ok');
          return;
        }
        if (action === 'session-lock') {
          deleteUnlockedFilePassword(path, local);
          if (activeFilterTagId) {
            renderFiles(activeSourceFiles);
          } else if (local) {
            renderLocalDiskItems(activeLocalDiskItems);
          } else {
            renderFiles(activeSourceFiles);
          }
          showStatus(t('文件已重新加锁：') + fileLabel, 'ok');
          return;
        }
        if (action === 'remove-lock') {
          const password = await askLockPassword({
            title: t('去锁文件'),
            description: t('请输入文件「') + fileLabel + t('」的锁密码。验证成功后会永久移除该文件锁。'),
            errorMessage: t('密码错误或去锁失败，请重新输入。'),
            statusErrorMessage: t('去锁失败：密码错误或验证失败'),
            onSubmit: async function (passwordText) {
              const url = api.fileUnlock
                + (local
                  ? ('?local=1&path=' + encodeURIComponent(path))
                  : ('?file=' + encodeURIComponent(path)))
                + '&password=' + encodeURIComponent(passwordText);
              await fetchJson(url, { method: 'POST' });
            }
          });
          if (password === null) {
            return;
          }
          deleteUnlockedFilePassword(path, local);
          setFileLockedState(path, local, false);
          if (activeFilterTagId) {
            renderFiles(activeSourceFiles);
          } else if (local) {
            renderLocalDiskItems(activeLocalDiskItems);
          } else {
            renderFiles(activeSourceFiles);
          }
          showStatus(t('文件已去锁：') + fileLabel, 'ok');
          return;
        }
        if (action === 'open-local-player') {
          let url = api.localDiskOpenFile + '?path=' + encodeURIComponent(path);
          url = appendFilePassword(url, path, true);
          url = appendLocalDirPassword(url, localDiskParentPath(path));
          await fetchJson(url, { method: 'POST' });
          showStatus(t('已调用本地播放器：') + fileLabel, 'ok');
          return;
        }
        if (action === 'choose-local-player') {
          let url = api.localDiskOpenFile + '?chooser=1&path=' + encodeURIComponent(path);
          url = appendFilePassword(url, path, true);
          url = appendLocalDirPassword(url, localDiskParentPath(path));
          await fetchJson(url, { method: 'POST' });
          showStatus(t('已打开本地播放器选择窗口：') + fileLabel, 'ok');
        }
      }

      function resetConfirmDialogPresentation() {
        if (!confirmDialog) {
          return;
        }
        confirmDialog.classList.remove('confirm-dialog-danger', 'confirm-dialog-warn', 'confirm-dialog-info', 'wide-confirm-dialog');
        if (confirmDialogIcon) {
          confirmDialogIcon.textContent = '❓';
        }
        if (confirmDialogHighlight) {
          confirmDialogHighlight.hidden = true;
          confirmDialogHighlight.textContent = '';
        }
        if (confirmDialogNote) {
          confirmDialogNote.hidden = true;
          confirmDialogNote.textContent = '';
        }
      }

      function askConfirmDialog(options) {
        if (!confirmDialog || !confirmDialogTitle || !confirmDialogDesc) {
          return Promise.resolve(false);
        }
        if (activeConfirmDialogResolver) {
          closeConfirmDialog(false);
        }
        const opts = options || {};
        resetConfirmDialogPresentation();
        const variant = String(opts.variant || (opts.danger === true ? 'danger' : 'default'));
        confirmDialog.classList.toggle('wide-confirm-dialog', !!opts.wide);
        confirmDialog.classList.toggle('confirm-dialog-danger', variant === 'danger');
        confirmDialog.classList.toggle('confirm-dialog-warn', variant === 'warn');
        confirmDialog.classList.toggle('confirm-dialog-info', variant === 'info');
        if (confirmDialogIcon) {
          const icon = opts.icon != null ? String(opts.icon) : (variant === 'danger' ? '🗑' : (variant === 'warn' ? '⚠️' : '❓'));
          confirmDialogIcon.textContent = icon;
        }
        confirmDialogTitle.textContent = String(opts.title || t('确认操作'));
        confirmDialogDesc.textContent = String(opts.description || t('请确认是否继续。'));
        const highlight = String(opts.highlight || '').trim();
        if (confirmDialogHighlight) {
          confirmDialogHighlight.hidden = !highlight;
          confirmDialogHighlight.textContent = highlight;
        }
        const note = String(opts.note || '').trim();
        if (confirmDialogNote) {
          confirmDialogNote.hidden = !note;
          confirmDialogNote.textContent = note;
        }
        if (confirmDialogCancelBtn) {
          confirmDialogCancelBtn.textContent = String(opts.cancelText || t('取消'));
        }
        if (confirmDialogExtraBtn) {
          const hasExtra = opts.extraText != null && String(opts.extraText) !== '';
          confirmDialogExtraBtn.hidden = !hasExtra;
          confirmDialogExtraBtn.textContent = hasExtra ? String(opts.extraText) : '';
          confirmDialogExtraBtn.setAttribute('data-confirm-extra-value', String(opts.extraValue == null ? 'extra' : opts.extraValue));
        }
        const extraButtons = Array.isArray(opts.extraButtons) ? opts.extraButtons : [];
        [confirmDialogExtra2Btn, confirmDialogExtra3Btn].forEach(function (btn, idx) {
          if (!btn) { return; }
          const item = extraButtons[idx];
          const visible = item && item.text != null && String(item.text) !== '';
          btn.hidden = !visible;
          btn.textContent = visible ? String(item.text) : '';
          btn.setAttribute('data-confirm-extra-value', visible ? String(item.value == null ? ('extra' + (idx + 2)) : item.value) : '');
        });
        if (confirmDialogConfirmBtn) {
          confirmDialogConfirmBtn.textContent = String(opts.confirmText || t('确认'));
          confirmDialogConfirmBtn.classList.toggle('danger', opts.danger !== false);
        }
        confirmDialog.hidden = false;
        document.body.style.overflow = 'hidden';
        requestAnimationFrame(function () {
          if (confirmDialogConfirmBtn) {
            confirmDialogConfirmBtn.focus();
          }
        });
        return new Promise(function (resolve) {
          activeConfirmDialogResolver = resolve;
        });
      }

      function askDeleteConfirmDialog(options) {
        const opts = options || {};
        return askConfirmDialog({
          title: opts.title || t('确认删除'),
          description: opts.description || t('请确认是否继续。'),
          highlight: opts.highlight || '',
          note: opts.note || '',
          confirmText: opts.confirmText || t('删除'),
          cancelText: opts.cancelText || t('取消'),
          danger: opts.danger !== false,
          variant: opts.variant || 'danger',
          icon: opts.icon != null ? opts.icon : '🗑',
          wide: !!opts.wide
        });
      }

      async function promptUploadedTranscodes(candidates) {
        const list = Array.isArray(candidates) ? candidates : [];
        if (!list.length) {
          return;
        }
        const hasAudioSplitChoice = list.some(function (item) {
          return isAudioSplitChoiceCandidate(item);
        });
        const confirmed = await askConfirmDialog({
          title: t('处理视频兼容性'),
          description: hasAudioSplitChoice
            ? (t('上传完成，检测到 ') + list.length + t(' 个视频需要进行兼容处理。其中音频不兼容的视频可选择拆分视频并转音频或音视频都转。是否继续？'))
            : (t('上传完成，检测到 ') + list.length + t(' 个视频建议转换为MP4以兼容浏览器播放。是否现在转换？')),
          confirmText: t('继续处理'),
          cancelText: t('保持原文件'),
          danger: false
        });
        if (!confirmed) {
          showStatus(t('已保留原视频文件，稍后仍可进行兼容处理。'), 'warn');
          return;
        }
        showManualTranscodePrompt(list);
        for (let i = 0; i < list.length; i += 1) {
          if (isAudioSplitChoiceCandidate(list[i])) {
            continue;
          }
          const name = String((list[i] && list[i].name) || '');
          if (name) {
            startManualTranscode(encodeURIComponent(name), 'auto');
          }
        }
      }

      function getCopyTaskProgressDescription() {
        return localImportProgressWindowMode === 'local-copy'
          ? t('正在将本地磁盘文件或目录粘贴到目标目录。')
          : t('正在将虚拟磁盘文件或目录粘贴到目标目录。');
      }

      function isHeicImageName(name) {
        return /\.(heic|heif)$/i.test(String(name || ''));
      }

      function maybeLoadHeicPreviewRuntime(item) {
        const fileName = String((item && (item.name || item.file)) || '');
        if (!isHeicImageName(fileName)) {
          return;
        }
        if (typeof window.loadWebCoolHeicPreviewRuntime !== 'function') {
          return;
        }
        window.loadWebCoolHeicPreviewRuntime().catch(function (err) {
          showStatus(t('HEIC图片预览组件加载失败：') + (err && err.message ? err.message : err), 'err');
        });
      }

      function updateImagePreviewWindow(win, gallery, index) {
        if (!win || !Array.isArray(gallery) || !gallery.length) {
          return;
        }
        const nextIndex = Math.max(0, Math.min(Number(index || 0), gallery.length - 1));
        const item = gallery[nextIndex];
        const titleEl = win.querySelector('.preview-title');
        const imageEl = win.querySelector('.preview-image');
        const prevBtn = win.querySelector('.preview-nav-btn[data-preview-nav="prev"]');
        const nextBtn = win.querySelector('.preview-nav-btn[data-preview-nav="next"]');
        win.__imageGallery = gallery;
        win.__imageIndex = nextIndex;
        if (titleEl) {
          titleEl.textContent = '图片预览：' + String(item.name || item.file || '');
        }
        resetImageEditState(win, { preserveImageDisplay: true });
        if (imageEl) {
          imageEl.classList.add('is-loading');
          imageEl.onload = function () {
            if (!win.__imageBaseCanvas) {
              capturePreviewBaseCanvas(win);
            }
            fitPreviewImageToWindow(win, true);
            imageEl.classList.remove('is-loading');
          };
          imageEl.onerror = function () {
            imageEl.classList.remove('is-loading');
          };
          imageEl.alt = String(item.name || '图片预览');
          imageEl.src = imagePreviewUrlForItem(item);
        }
        maybeLoadHeicPreviewRuntime(item);
        if (prevBtn) {
          prevBtn.disabled = nextIndex <= 0;
          prevBtn.hidden = gallery.length <= 1;
        }
        if (nextBtn) {
          nextBtn.disabled = nextIndex >= gallery.length - 1;
          nextBtn.hidden = gallery.length <= 1;
        }
      }

      function getVisibleLocalDiskPathSet() {
        const visible = new Set();
        activeLocalDiskItems.forEach(function (item) {
          if (item && item.path) {
            visible.add(String(item.path));
          }
        });
        if (activeLocalDiskTreeRootPath) {
          visible.add(activeLocalDiskTreeRootPath);
        }
        localDiskTreeCache.forEach(function (dirs) {
          dirs.forEach(function (item) {
            if (item && item.path) {
              visible.add(String(item.path));
            }
          });
        });
        return visible;
      }

      function localizeAdminStorageMigrationMessage(message) {
        const text = String(message || '');
        const prefixes = [
          '正在处理同名文件(覆盖)：',
          '正在处理同名文件(跳过)：',
          '正在处理同名文件(相同跳过)：',
          '正在处理同名文件：',
          '正在拷贝：',
          '正在备份：'
        ];
        for (let i = 0; i < prefixes.length; i += 1) {
          if (text.indexOf(prefixes[i]) === 0) {
            return t(prefixes[i]) + text.slice(prefixes[i].length);
          }
        }
        return t(text);
      }

      function deleteUnlockedFilePassword(path, local) {
        unlockedFilePasswords.delete(fileLockKey(path, local));
        saveUnlockedFilePasswords();
      }

      async function moveFoldersToFolder(folderPaths, targetFolder) {
        const selected = normalizeFolderMoveSources(folderPaths);
        if (!selected.length) {
          return { movedCount: 0, ignoredCount: 0 };
        }

        const target = String(targetFolder || '');
        const rawSelectedCount = Array.isArray(folderPaths) ? folderPaths.length : 0;
        const ignoredCount = rawSelectedCount > selected.length ? (rawSelectedCount - selected.length) : 0;

        let movedCount = 0;
        for (let i = 0; i < selected.length; i += 1) {
          const sourcePath = selected[i];
          const result = await fetchJson(
            withFolderPassword(
              withFolderPassword(api.folderMove + '?path=' + encodeURIComponent(sourcePath) + '&folder=' + encodeURIComponent(target), sourcePath),
              target,
              'target_folder_password'
            ),
            { method: 'POST' }
          );
          const nextPath = String((result && result.path) || '');
          activeFolderPath = relocatePathAfterFolderMove(activeFolderPath, sourcePath, nextPath);
          movedCount += 1;
        }

        ensureFolderPathExpanded(activeFolderPath);
        ensureFolderPathExpanded(target);
        await loadFiles();
        return { movedCount: movedCount, ignoredCount: ignoredCount };
      }

      function buildRestrictedRootIconHtml(type) {
        const cls = 'tag-root-icon ' + escapeHtml(type);
        if (type === 'video') {
          return '<span class="' + cls + '" aria-hidden="true"><svg viewBox="0 0 16 16" focusable="false"><rect x="2.25" y="3" width="11.5" height="10" rx="2"></rect><path d="M7 6l4 2-4 2V6z"></path></svg></span>';
        }
        if (type === 'audio') {
          return '<span class="' + cls + '" aria-hidden="true"><svg viewBox="0 0 16 16" focusable="false"><path d="M10 3.1v7.2a2.25 2.25 0 1 1-1.35-2.06V4.2l4.1-.82v1.82L10 5.75"></path></svg></span>';
        }
        if (type === 'image') {
          return '<span class="' + cls + '" aria-hidden="true"><svg viewBox="0 0 16 16" focusable="false"><rect x="2.25" y="3" width="11.5" height="10" rx="2"></rect><circle cx="6" cy="6.25" r="1.1"></circle><path d="M3.8 11.25l2.8-2.75 1.85 1.7 1.35-1.35 2.45 2.4"></path></svg></span>';
        }
        return '<span class="' + cls + '" aria-hidden="true"><svg viewBox="0 0 16 16" focusable="false"><path d="M4 2.5h5.5L12 5v8.5H4z"></path><path d="M9.5 2.5V5H12"></path><path d="M6 7.4h4M6 9.7h4M6 12h2.7"></path></svg></span>';
      }

      function buildTagNodeHtml(node, level) {
        const safeLevel = Math.max(1, Math.min(TAG_MAX_LEVEL, level || 1));
        const rawTagName = String((node && node.name) || '');
        const indent = (safeLevel - 1) * 5;
        const canExpand = safeLevel < TAG_MAX_LEVEL;
        const hasChildren = hasTagChildren(node);
        const expanded = expandedTagNodeIds.has(node.id);
        const toggleSymbol = getTagNodeToggleSymbol(node, safeLevel);
        const restrictedRootType = getRestrictedRootTagType(node, safeLevel);
        const restrictedIconHtml = restrictedRootType
          ? buildRestrictedRootIconHtml(restrictedRootType)
          : '';

        let childHtml = '';
        if (canExpand && hasChildren && expanded) {
          childHtml = (Array.isArray(node.children) ? node.children : []).map(function (child) {
            return buildTagNodeHtml(child, safeLevel + 1);
          }).join('');
        }

        const nameInlineStyle = 'cursor:pointer;';
        const toggleBtn = hasChildren
          ? '<button type="button" class="tag-node-toggle" data-tag-id="' + node.id + '">' + toggleSymbol + '</button>'
          : '<span class="tag-node-toggle placeholder"></span>';

        const nodeClass = activeFilterTagId === node.id ? 'tag-node active' : 'tag-node';
        const canDeleteTag = !isProtectedRestrictedRootTag(node, safeLevel);
        const canLockTag = canDeleteTag;
        const isRenaming = activeTagRenameId === node.id && canRenameTagNode(node, safeLevel);
        const displayTagName = restrictedRootType ? t(rawTagName) : rawTagName;
        const tagNameHtml = isRenaming
          ? '<input class="tag-rename-input" data-tag-rename-input="' + escapeHtml(node.id) + '" value="' + escapeHtml(rawTagName) + '" maxlength="60">'
          : '<span class="tag-node-name" data-tag-id="' + node.id + '">' + escapeHtml(displayTagName) + '</span>';
        const tagUnlocked = !!getTagPassword(node.id);
        const tagLockHtml = (canLockTag && node.locked)
          ? '<span class="folder-lock-icon file-lock-inline tag-lock-inline' + (tagUnlocked ? ' unlocked' : '') + '" data-tag-lock-toggle="' + escapeHtml(node.id) + '" title="' + escapeHtml(t(tagUnlocked ? '点击重新加锁' : '点击解锁')) + '" aria-label="' + escapeHtml(t(tagUnlocked ? '点击重新加锁' : '点击解锁')) + '"><span class="folder-lock-shackle"></span><span class="folder-lock-body"></span></span>'
          : '';
        const actionHtml =
          '<div class="tag-actions">' +
            (canExpand
              ? '<button type="button" class="tag-inline-btn" data-tag-create="' + node.id + '" data-tag-level="' + safeLevel + '" title="' + escapeHtml(t('新增子标签')) + '">+</button>'
              : '') +
            (canDeleteTag
              ? '<button type="button" class="tag-inline-btn danger" data-tag-delete="' + node.id + '" title="' + escapeHtml(t('删除标签')) + '">-</button>'
              : '') +
          '</div>';

        return (
          '<div class="' + nodeClass + '" data-tag-id="' + node.id + '" data-tag-locked="' + (node.locked ? '1' : '0') + '" data-tag-lockable="' + (canLockTag ? '1' : '0') + '">' +
            '<div class="tag-line">' +
              '<div class="tag-line-main" style="padding-left:' + indent + 'px;">' +
                toggleBtn +
                '<span class="tag-node-name-wrap" style="' + nameInlineStyle + '">' +
                  restrictedIconHtml +
                  tagNameHtml +
                  tagLockHtml +
                '</span>' +
              '</div>' +
              actionHtml +
            '</div>' +
            childHtml +
          '</div>'
        );
      }

      async function recoverRunningTranscodeTasks() {
        try {
          const data = await fetchJson(api.convertTasks);
          const tasks = Array.isArray(data.tasks) ? data.tasks : [];
          for (let i = 0; i < tasks.length; i += 1) {
            const task = tasks[i];
            if (!task || !task.task_id || !task.name) {
              continue;
            }
            upsertTranscodeTaskItem(task);
            const encoded = encodeURIComponent(String(task.name));
            stopTranscodePolling(encoded);
            const timer = setInterval(function () {
              pollTranscodeProgress(encoded, String(task.task_id));
            }, 1000);
            transcodeProgressTimers.set(encoded, timer);
            await pollTranscodeProgress(encoded, String(task.task_id));
          }
        } catch (_) {
        }
      }

      function renderLocalImportProgressFiles(files, fileSpeeds) {
        if (!localImportProgressFiles) {
          return;
        }
        const list = Array.isArray(files) ? files : [];
        if (!list.length) {
          localImportProgressFiles.innerHTML = '';
          return;
        }
        localImportProgressFiles.innerHTML = list.map(function (file, index) {
          const name = String((file && file.name) || '');
          const key = String((file && (file.path || file.remote_path || file.name)) || index);
          const state = String((file && file.state) || 'pending');
          const progress = Math.max(0, Math.min(100, Number((file && file.progress) || 0)));
          const size = Number((file && file.size) || 0);
          const copied = Number((file && file.copied) || 0);
          const speed = fileSpeeds && typeof fileSpeeds.get === 'function'
            ? Number(fileSpeeds.get(key) || 0)
            : 0;
          const speedText = speed > 0 ? (' · ' + t('速度：') + formatByteSpeed(speed)) : '';
          return '<div class="local-import-progress-file state-' + escapeHtml(state) + '">' +
            '<div class="local-import-progress-file-main">' +
              '<span class="local-import-progress-file-name">' + escapeHtml(name) + '</span>' +
              '<span class="local-import-progress-file-state">' + localImportFileStateText(state) + '</span>' +
            '</div>' +
            '<div class="local-import-progress-file-track"><div class="local-import-progress-file-fill" style="width:' + progress + '%"></div></div>' +
            '<div class="local-import-progress-file-meta">' + formatNumber(copied) + ' / ' + formatNumber(size) + t(' 字节') + escapeHtml(speedText) + '</div>' +
          '</div>';
        }).join('');
      }

      function resolveSplitVideoAudioUrl(filePath, local) {
        const candidates = splitAudioSidecarCandidates(filePath);
        if (!candidates.length) {
          return '';
        }
        for (let i = 0; i < candidates.length; i += 1) {
          const sidecar = candidates[i];
          if (local) {
            if (listHasFilePath(activeLocalDiskItems, sidecar, true)) {
              return localDiskDownloadUrl(sidecar) + '&v=' + Date.now();
            }
            continue;
          }
          const exists = listHasFilePath(currentFiles, sidecar, false)
            || listHasFilePath(activeSourceFiles, sidecar, false)
            || listHasFilePath(allFiles, sidecar, false);
          if (exists) {
            return downloadUrlForFile(sidecar, true) + '&v=' + Date.now();
          }
        }
        return '';
      }

      function setVisibleLocalDiskFilesSelected(checked) {
        getVisibleLocalDiskFilePaths().forEach(function (path) {
          if (checked) {
            selectedLocalDiskPaths.add(path);
          } else {
            selectedLocalDiskPaths.delete(path);
          }
        });
        renderLocalDiskItems(activeLocalDiskItems);
      }

      function renderAdminStoragePickerNode(path, level, itemMeta) {
        const textPath = String(path || '/');
        const encodedPath = encodeURIComponent(textPath);
        const dirs = adminStoragePickerCache.get(textPath) || [];
        const isExpanded = adminStoragePickerExpandedPaths.has(textPath);
        const hasCache = adminStoragePickerCache.has(textPath);
        const isActive = adminStoragePickerSelectedPath === textPath;
        const name = textPath === '/' ? t('根目录') : localDiskDisplayName(textPath, itemMeta && itemMeta.name);
        let html = '<div class="admin-storage-picker-node" data-admin-storage-picker-node="' + escapeHtml(textPath) + '">' +
          '<div class="admin-storage-picker-line' + (isActive ? ' active' : '') + '" style="padding-left:' + (8 + level * 18) + 'px;">' +
            '<button type="button" class="admin-storage-picker-toggle' + (hasCache && !dirs.length ? ' placeholder' : '') + '" data-admin-storage-picker-toggle="' + encodedPath + '" title="展开或收起目录" aria-label="展开或收起目录">' +
              (isExpanded && dirs.length ? '▾' : (hasCache && !dirs.length ? '•' : '▸')) +
            '</button>' +
            '<button type="button" class="admin-storage-picker-entry" data-admin-storage-picker-select="' + encodedPath + '" title="' + escapeHtml(textPath) + '">' +
              '<span class="local-folder-icon">📁</span>' +
              '<span class="admin-storage-picker-name">' + escapeHtml(name) + '</span>' +
            '</button>' +
          '</div>';
        if (isExpanded && dirs.length) {
          html += '<div class="admin-storage-picker-children">';
          html += dirs.map(function (item) {
            return renderAdminStoragePickerNode(String(item.path || ''), level + 1, item);
          }).join('');
          html += '</div>';
        }
        html += '</div>';
        return html;
      }

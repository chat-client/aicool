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

      function openAudioExtractProgress(fileName, windowTitle) {
        const dialog = document.createElement('div');
        dialog.className = 'audio-extract-progress-dialog';
        dialog.innerHTML =
          '<div class="audio-extract-progress-card" role="status" aria-live="polite">' +
            '<div class="audio-extract-progress-head">' +
              '<strong>' + (windowTitle || t('提取音频')) + '</strong>' +
              '<div class="audio-extract-progress-window-actions">' +
                '<button type="button" class="audio-extract-progress-minimize" aria-label="' + t('最小化') + '">−</button>' +
                '<button type="button" class="audio-extract-progress-close" aria-label="' + t('关闭') + '">×</button>' +
              '</div>' +
            '</div>' +
            '<div class="audio-extract-progress-body">' +
              '<div class="audio-extract-progress-file">' + escapeHtml(String(fileName || '')) + '</div>' +
              '<div class="audio-extract-progress-track" role="progressbar" aria-valuemin="0" aria-valuemax="100" aria-valuenow="0">' +
                '<div class="audio-extract-progress-fill"></div>' +
              '</div>' +
              '<div class="audio-extract-progress-text">' + t('正在启动音频提取：') + '0%</div>' +
              '<div class="audio-extract-progress-actions"><button type="button" class="audio-extract-progress-cancel">' + t('取消') + '</button></div>' +
            '</div>' +
          '</div>';
        document.body.appendChild(dialog);
        const fill = dialog.querySelector('.audio-extract-progress-fill');
        const track = dialog.querySelector('.audio-extract-progress-track');
        const textNode = dialog.querySelector('.audio-extract-progress-text');
        const cancelButton = dialog.querySelector('.audio-extract-progress-cancel');
        const minimizeButton = dialog.querySelector('.audio-extract-progress-minimize');
        const head = dialog.querySelector('.audio-extract-progress-head');
        let cancelHandler = null;
        let minimized = false;
        let currentProgress = 0;
        dialog.querySelector('.audio-extract-progress-close').addEventListener('click', function () {
          if (dialog.parentNode) dialog.parentNode.removeChild(dialog);
        });
        minimizeButton.addEventListener('click', function () {
          minimized = !minimized;
          dialog.classList.toggle('is-minimized', minimized);
          minimizeButton.textContent = minimized ? '□' : '−';
          minimizeButton.setAttribute('aria-label', minimized ? t('恢复') : t('最小化'));
        });
        cancelButton.addEventListener('click', function () {
          if (!cancelHandler || cancelButton.disabled) return;
          cancelButton.disabled = true;
          cancelButton.textContent = t('取消中');
          cancelHandler();
        });
        head.addEventListener('mousedown', function (event) {
          if (event.button !== 0 || event.target.closest('button')) return;
          const rect = dialog.getBoundingClientRect();
          const offsetX = event.clientX - rect.left;
          const offsetY = event.clientY - rect.top;
          dialog.style.right = 'auto';
          dialog.style.bottom = 'auto';
          const move = function (moveEvent) {
            const maxLeft = Math.max(0, window.innerWidth - dialog.offsetWidth);
            const maxTop = Math.max(0, window.innerHeight - dialog.offsetHeight);
            dialog.style.left = Math.max(0, Math.min(maxLeft, moveEvent.clientX - offsetX)) + 'px';
            dialog.style.top = Math.max(0, Math.min(maxTop, moveEvent.clientY - offsetY)) + 'px';
          };
          const stop = function () {
            document.removeEventListener('mousemove', move);
            document.removeEventListener('mouseup', stop);
          };
          document.addEventListener('mousemove', move);
          document.addEventListener('mouseup', stop);
          event.preventDefault();
        });
        return {
          setCancelHandler: function (handler) { cancelHandler = handler; },
          update: function (progress, message, state) {
            const value = progress == null
              ? currentProgress
              : Math.max(0, Math.min(100, Math.round(Number(progress || 0))));
            currentProgress = value;
            if (!dialog.parentNode) return;
            fill.style.width = value + '%';
            fill.classList.toggle('is-done', state === 'done');
            fill.classList.toggle('is-failed', state === 'failed');
            fill.classList.toggle('is-cancelled', state === 'cancelled');
            track.setAttribute('aria-valuenow', String(value));
            textNode.textContent = String(message || '') + (state ? '' : (' ' + value + '%'));
            if (state) {
              cancelButton.disabled = true;
              cancelButton.hidden = true;
            }
          }
        };
      }

      function formatVideoPropertySize(bytes) {
        const value = Number(bytes);
        if (!Number.isFinite(value) || value < 0) return '-';
        if (value >= 1073741824) return (value / 1073741824).toFixed(2) + ' GB';
        if (value >= 1048576) return (value / 1048576).toFixed(2) + ' MB';
        if (value >= 1024) return (value / 1024).toFixed(2) + ' KB';
        return value + ' B';
      }

      function formatVideoPropertyDuration(milliseconds) {
        const total = Math.max(0, Math.round(Number(milliseconds || 0) / 1000));
        const hours = Math.floor(total / 3600);
        const minutes = Math.floor((total % 3600) / 60);
        const seconds = total % 60;
        return (hours ? (String(hours).padStart(2, '0') + ':') : '')
          + String(minutes).padStart(2, '0') + ':' + String(seconds).padStart(2, '0');
      }

      async function showVideoProperties(path, local) {
        const loading = document.createElement('div');
        loading.className = 'video-properties-dialog';
        loading.innerHTML = '<div class="video-properties-card"><div class="video-properties-loading">' + t('正在读取媒体属性...') + '</div></div>';
        document.body.appendChild(loading);
        try {
          const endpoint = local ? api.localDiskVideoProperties : api.videoProperties;
          const parameter = local ? 'path' : 'file';
          const data = await fetchJson(endpoint + '?' + parameter + '=' + encodeURIComponent(path));
          const rows = [
            [t('文件名'), data.name || path],
            [t('文件大小'), formatVideoPropertySize(data.size)],
            [t('时长'), formatVideoPropertyDuration(data.duration_ms)],
            [t('总码率'), data.bitrate_kbps == null ? '-' : (data.bitrate_kbps + ' kb/s')]
          ];
          if (data.has_video) {
            rows.push(
              [t('视频编码'), data.video_codec || '-'],
              [t('分辨率'), data.width && data.height ? (data.width + ' × ' + data.height) : '-'],
              [t('帧率'), data.fps == null ? '-' : (Number(data.fps).toFixed(2) + ' fps')],
              [t('视频码率'), data.video_bitrate_kbps == null ? '-' : (data.video_bitrate_kbps + ' kb/s')]
            );
          }
          rows.push(
            [t('音频编码'), data.has_audio ? (data.audio_codec || '-') : t('无音轨')],
            [t('采样率'), data.sample_rate_hz == null ? '-' : (data.sample_rate_hz + ' Hz')],
            [t('声道'), data.audio_channels || '-'],
            [t('音频码率'), data.audio_bitrate_kbps == null ? '-' : (data.audio_bitrate_kbps + ' kb/s')]
          );
          const propertiesTitle = data.has_video ? t('视频属性') : t('音频属性');
          loading.innerHTML =
            '<div class="video-properties-card" role="dialog" aria-modal="true" aria-label="' + propertiesTitle + '">' +
              '<div class="video-properties-head"><strong>' + propertiesTitle + '</strong><div class="video-properties-window-actions">' +
                '<button type="button" data-video-properties-minimize aria-label="' + t('最小化') + '">−</button>' +
                '<button type="button" data-video-properties-close aria-label="' + t('关闭') + '">×</button>' +
              '</div></div>' +
              '<div class="video-properties-body">' +
                '<div class="video-properties-grid">' + rows.map(function (row) {
                  return '<div class="video-properties-label">' + escapeHtml(String(row[0])) + '</div><div class="video-properties-value">' + escapeHtml(String(row[1])) + '</div>';
                }).join('') + '</div>' +
                '<div class="video-properties-actions">' +
                  (data.has_video ? '<button type="button" class="video-properties-enhance" data-video-properties-enhance>' + t('提升码率和分辨率') + '</button>' : '') +
                  '<button type="button" data-video-properties-close>' + t('关闭') + '</button></div>' +
              '</div>' +
            '</div>';
          const card = loading.querySelector('.video-properties-card');
          const head = loading.querySelector('.video-properties-head');
          const minimizeButton = loading.querySelector('[data-video-properties-minimize]');
          let minimized = false;
          loading.querySelectorAll('[data-video-properties-close]').forEach(function (button) {
            button.addEventListener('click', function () { loading.remove(); });
          });
          const enhanceButton = loading.querySelector('[data-video-properties-enhance]');
          if (enhanceButton) {
            enhanceButton.addEventListener('click', async function () {
              const currentHeight = Number(data.height || 0);
              const currentBitrate = Number(data.video_bitrate_kbps || data.bitrate_kbps || 0);
              const suggested = currentHeight < 720
                ? { width: 1280, height: 720, bitrate: Math.max(2500, currentBitrate * 2) }
                : { width: 1920, height: 1080, bitrate: Math.max(5000, currentBitrate * 2) };
              const form = document.createElement('div');
              form.className = 'video-enhance-dialog';
              form.innerHTML = '<div class="video-enhance-card"><div class="video-enhance-head"><h3>' + t('提升码率和分辨率') + '</h3></div>' +
                '<p>' + t('建议值可根据需要手工修改。普通转码无法恢复源视频已经丢失的真实细节。') + '</p>' +
                '<label>' + t('处理方式') + '<select data-enhance-method><option value="standard">' + t('普通增强（快速）') + '</option><option value="ai">' + t('AI超分辨率（高质量，耗时）') + '</option></select></label>' +
                '<label>' + t('目标宽度') + '<input type="number" data-enhance-width min="320" max="3840" step="2" value="' + suggested.width + '"></label>' +
                '<label>' + t('目标高度') + '<input type="number" data-enhance-height min="240" max="2160" step="2" value="' + suggested.height + '"></label>' +
                '<label>' + t('视频码率') + ' (kb/s)<input type="number" data-enhance-bitrate min="300" max="50000" step="100" value="' + Math.round(suggested.bitrate) + '"><small data-enhance-bitrate-recommendation></small></label>' +
                '<div data-standard-enhance-options><label>' + t('降噪强度') + '<select data-enhance-denoise><option value="0">' + t('关闭') + '</option><option value="1" selected>' + t('轻度') + '</option><option value="2">' + t('中度') + '</option></select></label>' +
                '<label>' + t('锐化强度') + '<input type="range" data-enhance-sharpen min="0" max="100" step="10" value="30"><span class="video-enhance-range-value" data-enhance-sharpen-value>30%</span></label>' +
                '<label class="video-enhance-check"><input type="checkbox" data-enhance-deinterlace> ' + t('去隔行（仅老式隔行视频建议开启）') + '</label>' +
                '<label>' + t('运动补帧') + '<select data-enhance-fps><option value="0" selected>' + t('保持原帧率') + '</option><option value="30">30 FPS</option><option value="50">50 FPS</option><option value="60">60 FPS</option></select><small>' + t('补帧可以改善运动流畅度，但会显著增加处理时间。') + '</small></label></div>' +
                '<div data-ai-enhance-options hidden><label>' + t('旧视频修复预设') + '<select data-enhance-ai-restore-preset><option value="off">' + t('关闭预处理') + '</option><option value="conservative">' + t('保守（轻微处理）') + '</option><option value="balanced" selected>' + t('均衡（推荐）') + '</option><option value="strong">' + t('强化（严重压缩）') + '</option></select><small>' + t('先去除压缩块和噪声，再进行AI超分，通常比直接放大更清晰。') + '</small></label>' +
                '<details class="video-enhance-advanced" data-ai-restore-options><summary>' + t('旧视频修复细化选项') + '</summary>' +
                '<label>' + t('去压缩块') + '<select data-enhance-ai-deblock><option value="0">' + t('关闭') + '</option><option value="1" selected>' + t('轻度') + '</option><option value="2">' + t('中度') + '</option></select></label>' +
                '<label>' + t('AI前降噪') + '<select data-enhance-ai-predenoise><option value="0">' + t('关闭') + '</option><option value="1" selected>' + t('轻度') + '</option><option value="2">' + t('中度') + '</option></select></label>' +
                '<label>' + t('轻微去模糊/预锐化') + '<input type="range" data-enhance-ai-presharpen min="0" max="50" step="5" value="20"><span class="video-enhance-range-value" data-enhance-ai-presharpen-value>20%</span></label>' +
                '<label class="video-enhance-check"><input type="checkbox" data-enhance-ai-deinterlace> ' + t('AI前去隔行（仅隔行视频开启）') + '</label></details>' +
                '<label>' + t('AI模型') + '<select data-enhance-ai-model>' +
                '<option value="coreml-x2plus">' + t('真实2×（高质量/较快）') + '</option>' +
                '<option value="coreml-general-x4v3">' + t('轻量x4（速度优先）') + '</option>' +
                '<option value="coreml-general-x4v3-w8a8">' + t('轻量W8A8 x4（M4实验）') + '</option>' +
                '<option value="coreml-x4plus-int8">' + t('M4量化x4（质量优先）') + '</option>' +
                '<option value="realesrgan-x4plus">' + t('原始x4（对照/最慢）') + '</option>' +
                '<option value="realesr-animevideov3">' + t('动漫') + '</option></select>' +
                '<small data-ai-model-description></small></label>' +
                '<label>' + t('AI放大倍数') + '<select data-enhance-ai-scale><option value="2">2×</option><option value="4">4×</option></select></label>' +
                '<label>' + t('性能档位') + '<select data-enhance-ai-performance><option value="fast">' + t('快速') + '</option><option value="balanced" selected>' + t('均衡') + '</option><option value="quality">' + t('高质量') + '</option></select></label>' +
                '<details class="video-enhance-advanced"><summary>' + t('高级性能选项') + '</summary>' +
                '<label>' + t('推理输入尺寸') + '<select data-enhance-ai-input-sizing><option value="target">' + t('极速（允许大幅预缩小）') + '</option><option value="balanced" selected>' + t('均衡（最多缩小25%）') + '</option><option value="source">' + t('画质（不缩小源画面）') + '</option></select><small>' + t('画质模式保留全部源像素但耗时更长；极速模式可能损失原始细节。') + '</small></label>' +
                '<label>' + t('Core ML并发') + '<select data-enhance-ai-workers><option value="0" selected>' + t('按模型自动') + '</option><option value="1">1</option><option value="2">2</option><option value="4">4</option></select><small>' + t('重模型通常1个更快，轻量模型通常2个更快。') + '</small></label>' +
                '<label>' + t('Tile批量') + '<select data-enhance-ai-batch><option value="0" selected>' + t('按模型自动') + '</option><option value="1">1</option><option value="2">2</option><option value="4">4</option></select><small>' + t('批量可提高ANE吞吐，但会增加统一内存占用。') + '</small></label>' +
                '<label>' + t('Tile重叠') + '<select data-enhance-ai-overlap><option value="low">' + t('较少（更快）') + '</option><option value="balanced" selected>' + t('均衡') + '</option><option value="quality">' + t('较多（减少接缝）') + '</option></select></label>' +
                '<label>' + t('时序复用') + '<select data-enhance-ai-temporal><option value="1">' + t('每帧推理（最佳画质）') + '</option><option value="0" selected>' + t('自适应静态帧（推荐）') + '</option><option value="2">' + t('每2帧推理（较快）') + '</option><option value="3">' + t('每3帧推理（最快）') + '</option></select><small>' + t('自适应模式仅复用静态或近似静态画面，并定期强制刷新。') + '</small></label>' +
                '<label>Tile<select data-enhance-ai-tile><option value="0">' + t('自动') + '</option><option value="128">128</option><option value="256">256</option><option value="512">512</option></select><small>' + t('显存不足时选择较小值；较大值通常更快。') + '</small></label>' +
                '<label>' + t('推理线程') + '<select data-enhance-ai-threads><option value="1:2:2">1:2:2</option><option value="2:4:2" selected>2:4:2</option><option value="4:4:4">4:4:4</option></select></label>' +
                '<label>' + t('计算单元') + '<select data-enhance-ai-compute><option value="auto" selected>' + t('自动（CPU/GPU/ANE）') + '</option><option value="gpu">' + t('GPU优先') + '</option><option value="ane">' + t('Neural Engine优先') + '</option><option value="cpu">' + t('仅CPU（对照）') + '</option></select><small>' + t('仅Core ML模型有效；建议用相同短片段分别测试耗时。') + '</small></label>' +
                '<label>' + t('GPU编号') + '<select data-enhance-ai-gpu><option value="-1">' + t('自动') + '</option><option value="0">GPU 0</option><option value="1">GPU 1</option></select></label>' +
                '<label>' + t('最终编码速度') + '<select data-enhance-ai-encode><option value="fast">Fast</option><option value="medium" selected>Medium</option><option value="slow">Slow</option></select></label></details>' +
                '<small>' + t('AI会推测并生成纹理细节，结果不一定与原始真实内容完全一致。') + '</small></div>' +
                '<label>' + t('试跑范围') + '<select data-enhance-ai-preview><option value="0">' + t('完整视频') + '</option><option value="10">' + t('仅前10秒') + '</option><option value="30">' + t('仅前30秒') + '</option><option value="60">' + t('仅前60秒') + '</option></select><small>' + t('可先转换短片段，确认参数和画面效果。') + '</small></label>' +
                '<div class="video-enhance-actions"><button type="button" data-enhance-cancel>' + t('取消') + '</button><button type="button" data-enhance-compare hidden>' + t('生成10秒三方案对比') + '</button><button type="button" data-enhance-confirm>' + t('开始提升') + '</button></div></div>';
              document.body.appendChild(form);
              const enhanceCard = form.querySelector('.video-enhance-card');
              const enhanceHead = form.querySelector('.video-enhance-head');
              enhanceHead.addEventListener('pointerdown', function (event) {
                if (event.button != null && event.button !== 0) return;
                const rect = enhanceCard.getBoundingClientRect();
                const offsetX = event.clientX - rect.left;
                const offsetY = event.clientY - rect.top;
                enhanceCard.style.position = 'fixed';
                enhanceCard.style.left = rect.left + 'px';
                enhanceCard.style.top = rect.top + 'px';
                enhanceCard.style.width = rect.width + 'px';
                enhanceCard.style.margin = '0';
                enhanceHead.setPointerCapture(event.pointerId);
                const move = function (moveEvent) {
                  const maxLeft = Math.max(0, window.innerWidth - enhanceCard.offsetWidth);
                  const maxTop = Math.max(0, window.innerHeight - 120);
                  const nextTop = Math.max(0, Math.min(maxTop, moveEvent.clientY - offsetY));
                  enhanceCard.style.left = Math.max(0, Math.min(maxLeft, moveEvent.clientX - offsetX)) + 'px';
                  enhanceCard.style.top = nextTop + 'px';
                  enhanceCard.style.maxHeight = Math.max(120, window.innerHeight - nextTop - 8) + 'px';
                };
                const stop = function () {
                  enhanceHead.removeEventListener('pointermove', move);
                  enhanceHead.removeEventListener('pointerup', stop);
                  enhanceHead.removeEventListener('pointercancel', stop);
                };
                enhanceHead.addEventListener('pointermove', move);
                enhanceHead.addEventListener('pointerup', stop);
                enhanceHead.addEventListener('pointercancel', stop);
                event.preventDefault();
              });
              const methodSelect = form.querySelector('[data-enhance-method]');
              const comparisonButton = form.querySelector('[data-enhance-compare]');
              methodSelect.addEventListener('change', function () {
                form.querySelector('[data-standard-enhance-options]').hidden = methodSelect.value === 'ai';
                form.querySelector('[data-ai-enhance-options]').hidden = methodSelect.value !== 'ai';
                comparisonButton.hidden = methodSelect.value !== 'ai';
                syncRecommendedBitrate(!bitrateManuallyEdited);
              });
              const widthInput = form.querySelector('[data-enhance-width]');
              const heightInput = form.querySelector('[data-enhance-height]');
              const bitrateInput = form.querySelector('[data-enhance-bitrate]');
              const bitrateRecommendation = form.querySelector('[data-enhance-bitrate-recommendation]');
              const sourceWidth = Math.max(1, Number(data.width || 0));
              const sourceHeight = Math.max(1, Number(data.height || 0));
              const sourceBitrate = Math.max(300, currentBitrate || 1000);
              let bitrateManuallyEdited = false;
              const recommendedBitrate = function () {
                const width = Number(widthInput.value);
                const height = Number(heightInput.value);
                if (!Number.isFinite(width) || !Number.isFinite(height) || width <= 0 || height <= 0) return null;
                const targetPixels = width * height;
                const pixelRatio = sourceWidth > 1 && sourceHeight > 1
                  ? targetPixels / (sourceWidth * sourceHeight) : 1;
                // Use a continuous resolution floor instead of height bands,
                // otherwise changing width inside the same 720p/1080p band
                // can appear to have no effect at all.
                const resolutionFloor = 2500 * Math.pow(targetPixels / (1280 * 720), 0.72);
                let value = sourceBitrate * Math.pow(Math.max(0.25, pixelRatio), 0.75);
                if (methodSelect.value === 'ai') value *= 1.2;
                const targetFps = Number(form.querySelector('[data-enhance-fps]').value || 0);
                const sourceFps = Math.max(1, Number(data.fps || 25));
                if (targetFps > sourceFps) value *= Math.sqrt(targetFps / sourceFps);
                value = Math.max(600, resolutionFloor, Math.min(50000, value));
                value = Math.min(50000, value);
                return Math.round(value / 100) * 100;
              };
              const syncRecommendedBitrate = function (applyValue) {
                const value = recommendedBitrate();
                if (value == null) return;
                if (applyValue) bitrateInput.value = String(value);
                bitrateRecommendation.textContent = t('自动建议：') + value
                  + ' kb/s' + (bitrateManuallyEdited ? t('（已保留手工设置）') : t('（随分辨率自动调整）'));
              };
              const resolutionChanged = function () { syncRecommendedBitrate(!bitrateManuallyEdited); };
              widthInput.addEventListener('input', resolutionChanged);
              widthInput.addEventListener('change', resolutionChanged);
              heightInput.addEventListener('input', resolutionChanged);
              heightInput.addEventListener('change', resolutionChanged);
              form.querySelector('[data-enhance-fps]').addEventListener('change', function () {
                syncRecommendedBitrate(!bitrateManuallyEdited);
              });
              bitrateInput.addEventListener('input', function () {
                bitrateManuallyEdited = true;
                syncRecommendedBitrate(false);
              });
              bitrateRecommendation.addEventListener('click', function () {
                bitrateManuallyEdited = false;
                syncRecommendedBitrate(true);
              });
              bitrateRecommendation.title = t('点击恢复自动码率');
              syncRecommendedBitrate(true);
              const aiModelSelect = form.querySelector('[data-enhance-ai-model]');
              const aiScaleSelect = form.querySelector('[data-enhance-ai-scale]');
              const aiComputeSelect = form.querySelector('[data-enhance-ai-compute]');
              const syncAiScaleAvailability = function () {
                const fixedScale = aiModelSelect.value === 'coreml-x2plus' ? '2'
                  : (aiModelSelect.value === 'realesr-animevideov3' ? '' : '4');
                if (fixedScale) aiScaleSelect.value = fixedScale;
                aiScaleSelect.disabled = Boolean(fixedScale);
                aiScaleSelect.title = fixedScale ? t('该模型使用固定推理倍数，最终仍会缩放到目标分辨率') : '';
                const descriptions = {
                  'coreml-x2plus': t('官方RealESRGAN_x2plus，保留真人细节，适合约2×放大。'),
                  'coreml-general-x4v3': t('官方tiny通用模型，速度最快，适合先比较速度和可接受画质。'),
                  'coreml-general-x4v3-w8a8': t('轻量模型的8位权重与8位激活版本，使用视频样本校准，针对M4 ANE。'),
                  'coreml-x4plus-int8': t('高质量x4网络的8位权重量化版，用于比较M4速度、体积和画质。'),
                  'realesrgan-x4plus': t('原始高质量x4模型，模型最大且最慢，作为画质基准。'),
                  'realesr-animevideov3': t('动漫视频专用模型，不建议用于真人视频。')
                };
                form.querySelector('[data-ai-model-description]').textContent = descriptions[aiModelSelect.value] || '';
                const coremlModel = aiModelSelect.value !== 'realesr-animevideov3';
                aiComputeSelect.disabled = !coremlModel;
                aiComputeSelect.title = coremlModel ? '' : t('动漫模型使用NCNN后端，计算单元选项不适用');
              };
              aiModelSelect.addEventListener('change', syncAiScaleAvailability);
              syncAiScaleAvailability();
              const restorePreset = form.querySelector('[data-enhance-ai-restore-preset]');
              const preSharpenInput = form.querySelector('[data-enhance-ai-presharpen]');
              const syncRestorePreset = function () {
                const presets = {
                  off: { deblock: '0', denoise: '0', sharpen: '0' },
                  conservative: { deblock: '1', denoise: '0', sharpen: '10' },
                  balanced: { deblock: '1', denoise: '1', sharpen: '20' },
                  strong: { deblock: '2', denoise: '2', sharpen: '30' }
                };
                const preset = presets[restorePreset.value] || presets.balanced;
                form.querySelector('[data-enhance-ai-deblock]').value = preset.deblock;
                form.querySelector('[data-enhance-ai-predenoise]').value = preset.denoise;
                preSharpenInput.value = preset.sharpen;
                form.querySelector('[data-enhance-ai-presharpen-value]').textContent = preset.sharpen + '%';
              };
              restorePreset.addEventListener('change', syncRestorePreset);
              preSharpenInput.addEventListener('input', function () {
                form.querySelector('[data-enhance-ai-presharpen-value]').textContent = preSharpenInput.value + '%';
              });
              syncRestorePreset();
              const sharpenInput = form.querySelector('[data-enhance-sharpen]');
              sharpenInput.addEventListener('input', function () {
                form.querySelector('[data-enhance-sharpen-value]').textContent = sharpenInput.value + '%';
              });
              const performanceSelect = form.querySelector('[data-enhance-ai-performance]');
              performanceSelect.addEventListener('change', function () {
                const presets = {
                  fast: { model: 'coreml-general-x4v3-w8a8', input: 'target', scale: '4', tile: '256', threads: '4:4:4', encode: 'fast', overlap: 'low', temporal: '2' },
                  balanced: { model: 'coreml-x2plus', input: 'balanced', scale: '2', tile: '0', threads: '2:4:2', encode: 'medium', overlap: 'balanced', temporal: '0' },
                  quality: { model: 'coreml-x2plus', input: 'source', scale: '2', tile: '128', threads: '1:2:2', encode: 'slow', overlap: 'balanced', temporal: '1' }
                };
                const preset = presets[performanceSelect.value];
                form.querySelector('[data-enhance-ai-scale]').value = preset.scale;
                form.querySelector('[data-enhance-ai-model]').value = preset.model;
                form.querySelector('[data-enhance-ai-input-sizing]').value = preset.input;
                form.querySelector('[data-enhance-ai-tile]').value = preset.tile;
                form.querySelector('[data-enhance-ai-threads]').value = preset.threads;
                form.querySelector('[data-enhance-ai-encode]').value = preset.encode;
                form.querySelector('[data-enhance-ai-overlap]').value = preset.overlap;
                form.querySelector('[data-enhance-ai-temporal]').value = preset.temporal;
                syncAiScaleAvailability();
              });
              form.querySelector('[data-enhance-cancel]').addEventListener('click', function () { form.remove(); });
              const collectEnhanceOptions = function () {
                return {
                  width: Number(form.querySelector('[data-enhance-width]').value),
                  height: Number(form.querySelector('[data-enhance-height]').value),
                  bitrate: Number(form.querySelector('[data-enhance-bitrate]').value),
                  denoise: Number(form.querySelector('[data-enhance-denoise]').value),
                  sharpen: Number(sharpenInput.value),
                  deinterlace: form.querySelector('[data-enhance-deinterlace]').checked,
                  targetFps: Number(form.querySelector('[data-enhance-fps]').value),
                  method: methodSelect.value,
                  aiModel: form.querySelector('[data-enhance-ai-model]').value,
                  aiScale: Number(form.querySelector('[data-enhance-ai-scale]').value),
                  sourceFps: Number(data.fps || 25),
                  aiTile: Number(form.querySelector('[data-enhance-ai-tile]').value),
                  aiThreads: form.querySelector('[data-enhance-ai-threads]').value,
                  aiComputeUnits: aiComputeSelect.value,
                  aiInputSizing: form.querySelector('[data-enhance-ai-input-sizing]').value,
                  aiWorkers: Number(form.querySelector('[data-enhance-ai-workers]').value),
                  aiTileBatch: Number(form.querySelector('[data-enhance-ai-batch]').value),
                  aiOverlap: form.querySelector('[data-enhance-ai-overlap]').value,
                  aiTemporalStep: Number(form.querySelector('[data-enhance-ai-temporal]').value),
                  aiPreDeblock: Number(form.querySelector('[data-enhance-ai-deblock]').value),
                  aiPreDenoise: Number(form.querySelector('[data-enhance-ai-predenoise]').value),
                  aiPreSharpen: Number(preSharpenInput.value),
                  aiPreDeinterlace: form.querySelector('[data-enhance-ai-deinterlace]').checked,
                  aiGpu: Number(form.querySelector('[data-enhance-ai-gpu]').value),
                  aiEncodePreset: form.querySelector('[data-enhance-ai-encode]').value,
                  previewSeconds: Number(form.querySelector('[data-enhance-ai-preview]').value)
                };
              };
              const validEnhanceOptions = function (options) {
                return Number.isInteger(options.width) && Number.isInteger(options.height)
                  && options.width >= 320 && options.width <= 3840 && options.width % 2 === 0
                  && options.height >= 240 && options.height <= 2160 && options.height % 2 === 0
                  && Number.isFinite(options.bitrate) && options.bitrate >= 300 && options.bitrate <= 50000;
              };
              comparisonButton.addEventListener('click', async function () {
                const base = collectEnhanceOptions();
                if (!validEnhanceOptions(base)) {
                  showStatus(t('请输入有效的偶数宽高和视频码率'), 'err');
                  return;
                }
                const fast = Object.assign({}, base, {
                  method: 'ai', aiModel: 'coreml-general-x4v3-w8a8', aiScale: 4,
                  aiInputSizing: 'target', aiOverlap: 'low', aiTemporalStep: 2,
                  aiTile: 256, aiThreads: '4:4:4', aiEncodePreset: 'fast', previewSeconds: 10
                });
                const quality = Object.assign({}, base, {
                  method: 'ai', aiModel: 'coreml-x2plus', aiScale: 2,
                  aiInputSizing: 'source', aiOverlap: 'balanced', aiTemporalStep: 1,
                  aiTile: 128, aiThreads: '1:2:2', aiEncodePreset: 'slow', previewSeconds: 10
                });
                form.remove(); loading.remove();
                try {
                  showStatus(t('正在依次生成极速和高质量10秒对比文件'), 'ok');
                  const fastName = await startVideoEnhance(path, local, fast);
                  const qualityName = await startVideoEnhance(path, local, quality);
                  showStatus(t('三方案对比已生成：原视频、') + fastName + '、' + qualityName, 'ok');
                } catch (err) {
                  showStatus(t('对比文件生成失败：') + err.message, 'err');
                }
              });
              form.querySelector('[data-enhance-confirm]').addEventListener('click', function () {
                const options = collectEnhanceOptions();
                if (!validEnhanceOptions(options)) {
                  showStatus(t('请输入有效的偶数宽高和视频码率'), 'err');
                  return;
                }
                form.remove(); loading.remove();
                startVideoEnhance(path, local, options).catch(function (err) { showStatus(t('画质提升失败：') + err.message, 'err'); });
              });
            });
          }
          minimizeButton.addEventListener('click', function () {
            minimized = !minimized;
            card.classList.toggle('is-minimized', minimized);
            minimizeButton.textContent = minimized ? '□' : '−';
            minimizeButton.setAttribute('aria-label', minimized ? t('恢复') : t('最小化'));
          });
          head.addEventListener('mousedown', function (event) {
            if (event.button !== 0 || event.target.closest('button')) return;
            const rect = card.getBoundingClientRect();
            const offsetX = event.clientX - rect.left;
            const offsetY = event.clientY - rect.top;
            card.style.position = 'fixed';
            card.style.margin = '0';
            card.style.left = rect.left + 'px';
            card.style.top = rect.top + 'px';
            const move = function (moveEvent) {
              const maxLeft = Math.max(0, window.innerWidth - card.offsetWidth);
              const maxTop = Math.max(0, window.innerHeight - card.offsetHeight);
              card.style.left = Math.max(0, Math.min(maxLeft, moveEvent.clientX - offsetX)) + 'px';
              card.style.top = Math.max(0, Math.min(maxTop, moveEvent.clientY - offsetY)) + 'px';
            };
            const stop = function () {
              document.removeEventListener('mousemove', move);
              document.removeEventListener('mouseup', stop);
            };
            document.addEventListener('mousemove', move);
            document.addEventListener('mouseup', stop);
            event.preventDefault();
          });
          loading.addEventListener('click', function (event) {
            if (event.target === loading) loading.remove();
          });
        } catch (err) {
          loading.remove();
          throw err;
        }
      }

      async function startVideoEnhance(path, local, options) {
        const isAi = options.method === 'ai';
        const progressView = openAudioExtractProgress(path, isAi ? t('AI超分辨率') : t('提升码率和分辨率'));
        const startApi = local ? (isAi ? api.localDiskAiVideoEnhance : api.localDiskVideoEnhance) : (isAi ? api.aiVideoEnhance : api.videoEnhance);
        const parameter = local ? 'path' : 'file';
        const query = '?' + parameter + '=' + encodeURIComponent(path)
          + '&width=' + encodeURIComponent(options.width) + '&height=' + encodeURIComponent(options.height)
          + '&bitrate_kbps=' + encodeURIComponent(options.bitrate)
          + '&denoise=' + encodeURIComponent(options.denoise)
          + '&sharpen=' + encodeURIComponent(options.sharpen)
          + '&deinterlace=' + (options.deinterlace ? '1' : '0')
          + '&target_fps=' + encodeURIComponent(options.targetFps)
          + '&model=' + encodeURIComponent(options.aiModel)
          + '&scale=' + encodeURIComponent(options.aiScale)
          + '&fps=' + encodeURIComponent(options.sourceFps)
          + '&tile=' + encodeURIComponent(options.aiTile)
          + '&threads=' + encodeURIComponent(options.aiThreads)
          + '&compute_units=' + encodeURIComponent(options.aiComputeUnits || 'auto')
          + '&input_sizing=' + encodeURIComponent(options.aiInputSizing || 'balanced')
          + '&coreml_workers=' + encodeURIComponent(options.aiWorkers || 0)
          + '&tile_batch=' + encodeURIComponent(options.aiTileBatch || 0)
          + '&overlap_mode=' + encodeURIComponent(options.aiOverlap || 'balanced')
          + '&temporal_step=' + encodeURIComponent(Number.isFinite(options.aiTemporalStep) ? options.aiTemporalStep : 1)
          + '&ai_pre_deblock=' + encodeURIComponent(options.aiPreDeblock || 0)
          + '&ai_pre_denoise=' + encodeURIComponent(options.aiPreDenoise || 0)
          + '&ai_pre_sharpen=' + encodeURIComponent(options.aiPreSharpen || 0)
          + '&ai_pre_deinterlace=' + (options.aiPreDeinterlace ? '1' : '0')
          + '&gpu=' + encodeURIComponent(options.aiGpu)
          + '&encode_preset=' + encodeURIComponent(options.aiEncodePreset)
          + '&preview_seconds=' + encodeURIComponent(options.previewSeconds);
        let started;
        try {
          started = await fetchJson(startApi + query, { method: 'POST' });
        } catch (err) {
          progressView.update(0, (isAi ? t('AI超分辨率启动失败：') : t('画质提升失败：')) + err.message, 'failed');
          throw err;
        }
        const taskId = String(started.task_id || '');
        if (!taskId) throw new Error(t('无法启动画质提升'));
        saveVideoEnhanceRecoveryTask({
          taskId: taskId,
          path: path,
          local: local,
          isAi: isAi,
          startedAt: Date.now()
        });
        if (isAi && started.backend === 'coreml') {
          progressView.update(0, t('已启用 M4 Core ML 加速'));
        }
        await monitorVideoEnhanceTask(taskId, path, local, isAi, progressView, Date.now(), true);
        return String(started.name || '');
      }

      function videoEnhanceTaskInfo(task) {
        const file = String((task && task.file) || '');
        const prefixes = [
          { prefix: 'ai-enhance-local:', local: true, ai: true },
          { prefix: 'ai-enhance:', local: false, ai: true },
          { prefix: 'video-enhance-local:', local: true, ai: false },
          { prefix: 'video-enhance:', local: false, ai: false }
        ];
        for (let i = 0; i < prefixes.length; i += 1) {
          const item = prefixes[i];
          if (file.indexOf(item.prefix) === 0) {
            return { path: file.slice(item.prefix.length), local: item.local, isAi: item.ai };
          }
        }
        return null;
      }

      const VIDEO_ENHANCE_RECOVERY_STORAGE_KEY = 'webcool.videoEnhanceRecoveryTasks.v1';

      function loadVideoEnhanceRecoveryTasks() {
        try {
          const parsed = JSON.parse(localStorage.getItem(VIDEO_ENHANCE_RECOVERY_STORAGE_KEY) || '[]');
          return Array.isArray(parsed) ? parsed.filter(function (item) {
            return item && item.taskId && item.path;
          }) : [];
        } catch (_) {
          return [];
        }
      }

      function writeVideoEnhanceRecoveryTasks(tasks) {
        try {
          localStorage.setItem(VIDEO_ENHANCE_RECOVERY_STORAGE_KEY, JSON.stringify(tasks.slice(-20)));
        } catch (_) {
        }
      }

      function saveVideoEnhanceRecoveryTask(task) {
        const tasks = loadVideoEnhanceRecoveryTasks().filter(function (item) {
          return String(item.taskId) !== String(task.taskId);
        });
        tasks.push(task);
        writeVideoEnhanceRecoveryTasks(tasks);
      }

      function removeVideoEnhanceRecoveryTask(taskId) {
        writeVideoEnhanceRecoveryTasks(loadVideoEnhanceRecoveryTasks().filter(function (item) {
          return String(item.taskId) !== String(taskId);
        }));
      }

      function restorePersistedVideoEnhanceTasks() {
        loadVideoEnhanceRecoveryTasks().forEach(function (task) {
          const taskId = String(task.taskId || '');
          if (!taskId || activeVideoEnhanceProgressTasks.has(taskId)) return;
          const progressView = openAudioExtractProgress(String(task.path || ''),
            task.isAi ? t('AI超分辨率') : t('提升码率和分辨率'));
          progressView.update(0, t('正在恢复转换进度'));
          monitorVideoEnhanceTask(taskId, String(task.path || ''), Boolean(task.local),
            Boolean(task.isAi), progressView, Number(task.startedAt || Date.now()));
        });
      }

      function formatEnhanceElapsed(seconds) {
        const total = Math.max(0, Math.floor(Number(seconds) || 0));
        const hours = Math.floor(total / 3600);
        const minutes = Math.floor((total % 3600) / 60);
        const secs = total % 60;
        if (hours > 0) return hours + t('小时') + minutes + t('分钟');
        if (minutes > 0) return minutes + t('分钟') + secs + t('秒');
        return secs + t('秒');
      }

      async function monitorVideoEnhanceTask(taskId, path, local, isAi, progressView, taskStartedAt, propagateErrors) {
        const progressApi = local ? (isAi ? api.localDiskAiVideoEnhanceProgress : api.localDiskVideoEnhanceProgress) : (isAi ? api.aiVideoEnhanceProgress : api.videoEnhanceProgress);
        const cancelApi = local ? (isAi ? api.localDiskAiVideoEnhanceCancel : api.localDiskVideoEnhanceCancel) : (isAi ? api.aiVideoEnhanceCancel : api.videoEnhanceCancel);
        const id = String(taskId || '');
        activeVideoEnhanceProgressTasks.set(id, progressView);
        progressView.setCancelHandler(function () {
          fetchJson(cancelApi + '?task_id=' + encodeURIComponent(id), { method: 'POST' })
            .then(function () { progressView.update(null, t('取消中')); })
            .catch(function (err) { progressView.update(null, t('取消失败：') + err.message, 'failed'); });
        });
        const poll = async function () {
          const data = await fetchJson(progressApi + '?task_id=' + encodeURIComponent(id));
          const value = Number(data.progress || 0);
          if (!data.done) {
            let message = data.message || t('正在提升画质');
            const elapsedSeconds = Math.max(0, (Date.now() - taskStartedAt) / 1000);
            message += ' · ' + t('已耗时') + formatEnhanceElapsed(elapsedSeconds);
            if (isAi && value > 13) {
              const remainingSeconds = Math.max(0, Math.max(1, elapsedSeconds) * (100 - value) / value);
              const remainingMinutes = Math.ceil(remainingSeconds / 60);
              message += ' · ' + t('预计剩余约') + remainingMinutes + t('分钟');
            }
            progressView.update(value, message);
            await new Promise(function (resolve) { window.setTimeout(resolve, 800); });
            return poll();
          }
          activeVideoEnhanceProgressTasks.delete(id);
          removeVideoEnhanceRecoveryTask(id);
          if (!data.success) {
            const failureMessage = data.cancel_requested ? t('已取消') : (data.error || t('画质提升失败'));
            progressView.update(value, failureMessage, data.cancel_requested ? 'cancelled' : 'failed');
            if (propagateErrors) {
              const failure = new Error(failureMessage);
              failure.enhanceTaskFailure = true;
              throw failure;
            }
            return;
          }
          const totalElapsedSeconds = Math.max(0, (Date.now() - taskStartedAt) / 1000);
          progressView.update(100, t('画质提升完成：') + data.name
            + ' · ' + t('总耗时') + formatEnhanceElapsed(totalElapsedSeconds), 'done');
          if (local) await loadLocalDisk(activeLocalDiskPath || localDiskParentPath(path) || ''); else await loadFiles();
        };
        try {
          await poll();
        } catch (err) {
          activeVideoEnhanceProgressTasks.delete(id);
          removeVideoEnhanceRecoveryTask(id);
          if (!err.enhanceTaskFailure) {
            progressView.update(null, t('进度查询失败：') + err.message, 'failed');
          }
          if (propagateErrors) throw err;
        }
      }

      async function handleFileContextAction(action, path, local) {
        if (!action || !path) {
          return;
        }
        const fileLabel = local ? path : path;
        if (action === 'video-properties') {
          await showVideoProperties(path, local);
          return;
        }
        if (action === 'extract-audio') {
          const progressView = openAudioExtractProgress(fileLabel);
          try {
            const started = await fetchJson(
              (local
                ? (api.localDiskAudioExtract + '?path=')
                : (api.extractAudio + '?file=')) + encodeURIComponent(path),
              { method: 'POST' }
            );
            if (!started.task_id) {
              throw new Error(started.message || t('无法启动音频提取'));
            }
            const taskId = String(started.task_id);
            const progressApi = local ? api.localDiskAudioExtractProgress : api.extractAudioProgress;
            const cancelApi = local ? api.localDiskAudioExtractCancel : api.extractAudioCancel;
            progressView.setCancelHandler(function () {
              fetchJson(cancelApi + '?task_id=' + encodeURIComponent(taskId), { method: 'POST' })
                .then(function () { progressView.update(null, t('取消中')); })
                .catch(function (err) { progressView.update(null, t('取消失败：') + err.message, 'failed'); });
            });
            const poll = async function () {
              const data = await fetchJson(progressApi + '?task_id=' + encodeURIComponent(taskId));
              const progress = Math.max(0, Math.min(100, Math.round(Number(data.progress || 0))));
              if (!data.done) {
                progressView.update(progress, data.message || t('正在提取音频：'));
                window.setTimeout(function () {
                  poll().catch(function (err) {
                    progressView.update(progress, t('音频提取失败：') + err.message, 'failed');
                  });
                }, 800);
                return;
              }
              if (!data.success) {
                if (data.cancel_requested) {
                  progressView.update(progress, t('已取消'), 'cancelled');
                  return;
                }
                throw new Error(data.error || data.message || t('未知错误'));
              }
              progressView.update(100, t('音频提取完成：') + String(data.name || ''), 'done');
              if (local) {
                await loadLocalDisk(activeLocalDiskPath || localDiskParentPath(path) || '');
              } else {
                await loadFiles();
              }
              showStatus(t('音频提取完成：') + String(data.name || ''), 'ok');
            };
            await poll();
          } catch (err) {
            progressView.update(0, t('音频提取失败：') + err.message, 'failed');
          }
          return;
        }
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
        restorePersistedVideoEnhanceTasks();
        try {
          const data = await fetchJson(api.convertTasks);
          const tasks = Array.isArray(data.tasks) ? data.tasks : [];
          for (let i = 0; i < tasks.length; i += 1) {
            const task = tasks[i];
            if (!task || !task.task_id || !task.name) {
              continue;
            }
            const enhanceInfo = videoEnhanceTaskInfo(task);
            if (enhanceInfo) {
              const enhanceTaskId = String(task.task_id);
              if (!activeVideoEnhanceProgressTasks.has(enhanceTaskId)) {
                saveVideoEnhanceRecoveryTask({
                  taskId: enhanceTaskId,
                  path: enhanceInfo.path,
                  local: enhanceInfo.local,
                  isAi: enhanceInfo.isAi,
                  startedAt: Date.now()
                });
                const progressView = openAudioExtractProgress(enhanceInfo.path,
                  enhanceInfo.isAi ? t('AI超分辨率') : t('提升码率和分辨率'));
                progressView.update(Number(task.progress || 0), task.message || t('正在恢复转换进度'));
                monitorVideoEnhanceTask(enhanceTaskId, enhanceInfo.path, enhanceInfo.local,
                  enhanceInfo.isAi, progressView, Date.now());
              }
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

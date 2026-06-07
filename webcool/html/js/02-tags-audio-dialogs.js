function getLocalDirPassword(path) {
        return unlockedFilePasswords.get(localDirLockKey(path)) || '';
      }

      async function moveFoldersToRecycle(folderPaths) {
        const selected = normalizeFolderMoveSources(folderPaths).filter(function (path) {
          return path && !isRecycleFolderPath(path);
        });
        if (!selected.length) {
          return { movedCount: 0, ignoredCount: Array.isArray(folderPaths) ? folderPaths.length : 0 };
        }

        const rawSelectedCount = Array.isArray(folderPaths) ? folderPaths.length : 0;
        const ignoredCount = rawSelectedCount > selected.length ? (rawSelectedCount - selected.length) : 0;
        const confirmed = await askConfirmDialog({
          title: t('移入回收站'),
          description: t('确认将选中的 ') + selected.length + t(' 个文件夹及其全部内容移入回收站？'),
          confirmText: t('移入回收站'),
          danger: true
        });
        if (!confirmed) {
          return { movedCount: 0, ignoredCount: ignoredCount, cancelled: true };
        }

        let movedCount = 0;
        for (let i = 0; i < selected.length; i += 1) {
          const sourcePath = selected[i];
          await fetchJson(withFolderPassword(api.folderDelete + '?path=' + encodeURIComponent(sourcePath), sourcePath), { method: 'POST' });
          if (isSameOrChildFolderPath(sourcePath, activeFolderPath)) {
            activeFolderPath = RECYCLE_FOLDER_NAME;
          }
          movedCount += 1;
        }

        ensureFolderPathExpanded(RECYCLE_FOLDER_NAME);
        await loadFiles();
        return { movedCount: movedCount, ignoredCount: ignoredCount };
      }

      function updateFilesTagToggleButton() {
        if (!filesTagToggleBtn) {
          return;
        }
        filesTagToggleBtn.textContent = '+';
        filesTagToggleBtn.setAttribute('title', t('新增一级标签'));
      }

      function setTranscodeReason(encodedName, text) {
        const item = statusBox.querySelector('[data-transcode-item="' + encodedName + '"]');
        if (!item) {
          return;
        }
        const reason = item.querySelector('.transcode-item-reason');
        if (reason) {
          reason.textContent = text || '';
        }
      }

      function finishLocalImportProgress(text) {
        setLocalImportProgress(100, text || t('上传完成 100%'));
        if (localImportProgressClose) {
          localImportProgressClose.hidden = false;
        }
        if (localImportProgressCancel) {
          localImportProgressCancel.hidden = true;
        }
      }

      function bindSplitVideoAudio(videoEl, audioEl) {
        if (!videoEl || !audioEl) {
          return;
        }

        function syncVolumeAndRate() {
          audioEl.muted = !!videoEl.muted;
          audioEl.volume = Math.max(0, Math.min(1, Number(videoEl.volume || 0)));
          audioEl.playbackRate = Number(videoEl.playbackRate || 1) || 1;
        }

        function syncCurrentTime(force) {
          const videoTime = Math.max(0, Number(videoEl.currentTime || 0));
          const audioTime = Math.max(0, Number(audioEl.currentTime || 0));
          if (force || Math.abs(videoTime - audioTime) > 0.35) {
            try {
              audioEl.currentTime = videoTime;
            } catch (_) {}
          }
        }

        syncVolumeAndRate();
        videoEl.addEventListener('volumechange', syncVolumeAndRate);
        videoEl.addEventListener('ratechange', syncVolumeAndRate);
        videoEl.addEventListener('loadedmetadata', function () {
          syncCurrentTime(true);
        });
        videoEl.addEventListener('play', function () {
          syncVolumeAndRate();
          syncCurrentTime(true);
          audioEl.play().catch(function () {});
        });
        videoEl.addEventListener('pause', function () {
          audioEl.pause();
        });
        videoEl.addEventListener('seeking', function () {
          syncCurrentTime(true);
        });
        videoEl.addEventListener('timeupdate', function () {
          if (!audioEl.paused) {
            syncCurrentTime(false);
          }
        });
        videoEl.addEventListener('ended', function () {
          audioEl.pause();
          syncCurrentTime(true);
        });
      }

      function removeSelectedLocalDiskFiles() {
        const paths = getSelectedLocalDiskFilePaths();
        if (!paths.length) {
          return;
        }
        if (!confirm('确认将选中的 ' + paths.length + ' 个本地文件移至回收站？')) {
          return;
        }
        [localDiskBulkRemoveBtn, localDiskTableBulkRemoveBtn].forEach(function (btn) {
          if (btn) {
            btn.disabled = true;
          }
        });
        return Promise.all(paths.map(function (path) {
          return fetchJson(appendLocalDirPassword(appendFilePassword(api.localDiskDelete + '?path=' + encodeURIComponent(path), path, true), localDiskParentPath(path)), { method: 'POST' });
        })).then(function () {
          showStatus('已移除 ' + paths.length + ' 个本地文件到回收站', 'warn');
          clearLocalDiskSelection();
          loadLocalDisk(activeLocalDiskPath || '');
        }).catch(function (err) {
          showStatus('批量移除失败：' + err.message, 'err');
          loadLocalDisk(activeLocalDiskPath || '');
        });
      }

      function renderAdminStoragePicker() {
        if (!adminStoragePickerTree || !adminStoragePickerEmpty) {
          return;
        }
        if (!adminStoragePickerRootPath) {
          adminStoragePickerTree.innerHTML = '';
          adminStoragePickerEmpty.style.display = 'block';
          return;
        }
        adminStoragePickerTree.innerHTML = renderAdminStoragePickerNode(adminStoragePickerRootPath, 0, null);
        adminStoragePickerEmpty.style.display = 'none';
        if (adminStoragePickerPath) {
          adminStoragePickerPath.innerHTML = '当前选择：<span class="admin-storage-picker-current">'
            + escapeHtml(adminStoragePickerSelectedPath || adminStoragePickerRootPath)
            + '</span>';
        }
      }

      function appendTagPassword(url, tagId) {
        const password = getTagPassword(tagId);
        if (!password) {
          return url;
        }
        return url + '&tag_password=' + encodeURIComponent(password);
      }

      function isLocalDiskConvertibleVideoName(name) {
        return /\.(rm|rmvb|avi)$/i.test(String(name || ''));
      }

      function cancelTagRename() {
        if (!activeTagRenameId) {
          return;
        }
        activeTagRenameId = '';
        renderTagTree();
      }

      function hideUploadProgress() {
        uploadProgress.style.display = 'none';
        uploadProgressFill.style.width = '0%';
        uploadProgressText.textContent = t('准备上传...');
      }

      function pollLocalDiskCopyTask(taskId, targetPath, copiedPath) {
        return pollCopyTask(taskId, {
          mode: 'local-copy',
          copiedPath: copiedPath,
          onDone: async function () {
            localDiskTreeCache.delete(targetPath);
            await loadLocalDisk(targetPath, { resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, targetPath) });
          }
        });
      }

      function updatePreviewImageSizeLabel(win, width, height) {
        if (!win) {
          return;
        }
        const widthInput = win ? win.querySelector('.preview-size-input[data-image-size="width"]') : null;
        const heightInput = win ? win.querySelector('.preview-size-input[data-image-size="height"]') : null;
        const img = win.querySelector('.preview-image');
        const w = Math.max(0, Math.round(Number(width || (img && img.naturalWidth) || 0)));
        const h = Math.max(0, Math.round(Number(height || (img && img.naturalHeight) || 0)));
        if (widthInput) {
          widthInput.value = w ? String(w) : '';
        }
        if (heightInput) {
          heightInput.value = h ? String(h) : '';
        }
      }

      function previewImageSourceSize(win) {
        const base = win && win.__imageBaseCanvas;
        const img = win ? win.querySelector('.preview-image') : null;
        const width = Math.max(0, Math.round(Number((base && base.width) || (img && img.naturalWidth) || 0)));
        const height = Math.max(0, Math.round(Number((base && base.height) || (img && img.naturalHeight) || 0)));
        return { width: width, height: height };
      }

      function previewImageFitScale(win) {
        const shell = win ? win.querySelector('.preview-image-shell') : null;
        const size = previewImageSourceSize(win);
        if (!shell || !size.width || !size.height) {
          return 1;
        }
        const style = window.getComputedStyle ? window.getComputedStyle(shell) : null;
        const padX = style
          ? (parseFloat(style.paddingLeft) || 0) + (parseFloat(style.paddingRight) || 0)
          : 0;
        const padY = style
          ? (parseFloat(style.paddingTop) || 0) + (parseFloat(style.paddingBottom) || 0)
          : 0;
        const availableWidth = Math.max(1, shell.clientWidth - padX);
        const availableHeight = Math.max(1, shell.clientHeight - padY);
        return Math.max(0.05, Math.min(1, availableWidth / size.width, availableHeight / size.height));
      }

      function setPreviewImageDisplayScale(win, scale, message) {
        const img = win ? win.querySelector('.preview-image') : null;
        const size = previewImageSourceSize(win);
        if (!img || !size.width || !size.height) {
          return;
        }
        const safeScale = Math.max(0.05, Math.min(1, Number(scale) || 1));
        const displayWidth = Math.max(1, Math.round(size.width * safeScale));
        const displayHeight = Math.max(1, Math.round(size.height * safeScale));
        win.__imageDisplayScale = safeScale;
        img.style.width = displayWidth + 'px';
        img.style.height = displayHeight + 'px';
        updatePreviewImageSizeLabel(win, displayWidth, displayHeight);
        if (message) {
          setImageEditHint(win, message);
        }
      }

      function fitPreviewImageToWindow(win, force) {
        if (!win) {
          return;
        }
        if (win.__imageUserZoom && !force) {
          return;
        }
        const img = win.querySelector('.preview-image');
        if (!img || !img.complete || !img.naturalWidth || !img.naturalHeight) {
          return;
        }
        setPreviewImageDisplayScale(win, previewImageFitScale(win));
      }

      function localDiskPathContains(base, path) {
        const normalize = function (value) {
          return String(value || '/').replace(/[\/\\]+$/, '') || '/';
        };
        const left = normalize(base);
        const right = normalize(path);
        const rightLower = right.toLowerCase();
        const leftLower = left.toLowerCase();
        if (left === '/') {
          return right.charAt(0) === '/' || /^[A-Za-z]:/.test(right) || right.indexOf('\\\\') === 0;
        }
        return rightLower === leftLower
          || rightLower.indexOf(leftLower + '/') === 0
          || rightLower.indexOf(leftLower + '\\') === 0;
      }

      async function saveVideoResumePosition(fileName, positionMs) {
        const safeMs = Math.max(0, Math.round(Number(positionMs) || 0));
        await fetchJson(
          api.videoResumeSave
            + '?file=' + encodeURIComponent(fileName || '')
            + '&position_ms=' + encodeURIComponent(String(safeMs)),
          { method: 'POST' }
        );
      }

      function appendLocalDirPassword(url, path, paramName) {
        const password = getLocalDirPasswordForPath(path);
        if (!password) {
          return url;
        }
        return url + '&' + encodeURIComponent(paramName || 'local_dir_password') + '=' + encodeURIComponent(password);
      }

      function isAudioName(name) {
        return /\.(mp3|m4a|aac|wav|ogg|flac)$/i.test(String(name || ''));
      }

      async function submitTagRename(input) {
        if (!input) {
          return;
        }
        const tagId = String(input.getAttribute('data-tag-rename-input') || '');
        if (!tagId || activeTagRenameId !== tagId) {
          return;
        }
        if (tagRenameRequestId === tagId) {
          return;
        }

        const meta = findTagMetaById(tagId);
        if (!meta || !canRenameTagNode(meta.node, meta.level)) {
          activeTagRenameId = '';
          renderTagTree();
          return;
        }

        const nextName = String(input.value || '').trim();
        if (!nextName) {
          showStatus(t('标签名称不能为空'), 'err');
          input.focus();
          return;
        }
        if (nextName === String(meta.node.name || '')) {
          activeTagRenameId = '';
          renderTagTree();
          return;
        }

        try {
          tagRenameRequestId = tagId;
          await fetchJson(
            api.tagRename + '?id=' + encodeURIComponent(tagId) + '&name=' + encodeURIComponent(nextName),
            { method: 'POST' }
          );
          activeTagRenameId = '';
          await loadTagTreeState();
          renderTagTree();
          if (activeFilterTagId === tagId) {
            await showFilesForTag(tagId);
          } else {
            updateFileViewContext();
          }
          showStatus(t('标签已改名：') + nextName, 'ok');
        } catch (err) {
          showStatus(t('标签改名失败：') + err.message, 'err');
          input.focus();
          input.select();
        } finally {
          tagRenameRequestId = '';
        }
      }

      function safeTime(file) {
        const n = Number(file.uploaded_at || 0);
        return Number.isFinite(n) ? n : 0;
      }

      function pollLocalImportProgress(taskId) {
        return new Promise(function (resolve, reject) {
          if (!taskId) {
            reject(new Error(t('缺少上传任务编号')));
            return;
          }
          const timer = setInterval(function () {
            fetchJson(api.localDiskImportProgress + '?task_id=' + encodeURIComponent(taskId))
              .then(function (data) {
                const progress = Math.max(0, Math.min(100, Number(data.progress || 0)));
                const state = String(data.state || '');
                setLocalImportProgress(progress, (data.message || t('上传中')) + ' ' + Math.round(progress) + '%');
                renderLocalImportProgressFiles(data.files);
                if (state === 'done') {
                  clearInterval(timer);
                  finishLocalImportProgress(t('上传完成 100%'));
                  resolve(data);
                } else if (state === 'failed') {
                  clearInterval(timer);
                  reject(new Error(data.error || t('上传失败')));
                }
              })
              .catch(function (err) {
                clearInterval(timer);
                reject(err);
              });
          }, 400);
        });
      }

      function resetImageEditState(win, options) {
        if (!win) {
          return;
        }
        const opts = options || {};
        win.__imageDirty = false;
        win.__imageCropMode = false;
        win.__imageCropRect = null;
        win.__imageBaseCanvas = null;
        win.__imageScale = 1;
        if (!opts.preserveImageDisplay) {
          win.__imageDisplayScale = 1;
          win.__imageUserZoom = false;
        }
        win.__imageCurrentWidth = 0;
        win.__imageCurrentHeight = 0;
        const img = win.querySelector('.preview-image');
        const shell = win.querySelector('.preview-image-shell');
        const cropRect = win.querySelector('.preview-crop-rect');
        if (img && !opts.preserveImageDisplay) {
          img.style.width = '';
          img.style.height = '';
        }
        if (shell) {
          shell.classList.remove('crop-mode');
        }
        if (cropRect) {
          cropRect.hidden = true;
          cropRect.removeAttribute('style');
        }
        setImageEditHint(win, '');
        updatePreviewImageSizeLabel(win);
      }

      function localDiskParentPath(path) {
        const raw = String(path || '/');
        const text = raw.replace(/[\/\\]+$/, '') || raw || '/';
        if (text === '/') {
          return '/';
        }
        if (/^[A-Za-z]:$/.test(text)) {
          return '/';
        }
        const pos = Math.max(text.lastIndexOf('/'), text.lastIndexOf('\\'));
        if (pos <= 0) {
          return text.charAt(0) === '\\' ? '\\' : '/';
        }
        if (pos === 2 && /^[A-Za-z]:/.test(text)) {
          return text.slice(0, 3);
        }
        return text.slice(0, pos);
      }

      function scheduleSaveVideoResumePosition(fileName, positionMs) {
        const key = String(fileName || '');
        if (!key) {
          return;
        }

        const existed = videoResumeSaveTimers.get(key);
        if (existed) {
          existed.positionMs = positionMs;
          return;
        }

        const state = {
          positionMs: positionMs,
          timer: setTimeout(function () {
            const latest = videoResumeSaveTimers.get(key);
            if (!latest) {
              return;
            }
            clearTimeout(latest.timer);
            videoResumeSaveTimers.delete(key);
            saveVideoResumePosition(key, latest.positionMs).catch(function () {});
          }, 800)
        };
        videoResumeSaveTimers.set(key, state);
      }

      function folderPathExists(path) {
        const text = String(path || '');
        if (!text) {
          return true;
        }
        return collectFolderPaths(folderTreeData, []).includes(text);
      }

      function escapeRegExp(text) {
        return String(text).replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
      }

      function getRestrictedRootTagType(node, level) {
        if ((level || 1) !== 1 || !node || !node.id) {
          return '';
        }
        return getTagFileTypeConstraint(node.id);
      }

      function scrollFolderPathIntoView(path) {
        const node = findFolderTreeNodeElement(path);
        if (!node || typeof node.scrollIntoView !== 'function') {
          return;
        }
        node.scrollIntoView({ block: 'nearest' });
      }

      function getSelectedVisibleFileNames() {
        const names = [];
        (Array.isArray(currentFiles) ? currentFiles : []).forEach(function (file) {
          const name = getFilePath(file);
          if (name && selectedFileNames.has(name)) {
            names.push(name);
          }
        });
        return names;
      }

      function renderPreviewImageFromBase(win, width, height, message) {
        const img = win ? win.querySelector('.preview-image') : null;
        if (!img || !img.complete || !img.naturalWidth || !img.naturalHeight) {
          setImageEditHint(win, '图片还没有加载完成，请稍后再试。', true);
          return;
        }
        const base = previewBaseCanvas(win);
        if (!base) {
          setImageEditHint(win, '图片还没有加载完成，请稍后再试。', true);
          return;
        }
        const nextWidth = Math.max(1, Math.round(Number(width || 0)));
        const nextHeight = Math.max(1, Math.round(Number(height || 0)));
        if (nextWidth > 12000 || nextHeight > 12000) {
          setImageEditHint(win, '图片尺寸过大，无法调整。', true);
          return;
        }
        try {
          const canvas = document.createElement('canvas');
          canvas.width = nextWidth;
          canvas.height = nextHeight;
          const ctx = canvas.getContext('2d');
          ctx.imageSmoothingEnabled = true;
          ctx.imageSmoothingQuality = 'high';
          ctx.drawImage(base, 0, 0, nextWidth, nextHeight);
          win.__imageUserZoom = false;
          replacePreviewImageWithCanvas(win, canvas, { updateBase: false });
          setImageEditHint(win, message || ('已调整至 ' + nextWidth + ' x ' + nextHeight + '。'));
        } catch (err) {
          setImageEditHint(win, '调整尺寸失败：' + err.message, true);
        }
      }

      function compareLocalDiskItems(a, b) {
        const left = a || {};
        const right = b || {};
        const nameRes = String(left.name || '').localeCompare(String(right.name || ''), 'zh-CN');
        let res = 0;
        if (localDiskSortKey === 'type') {
          const typeRes = (left.directory === right.directory) ? 0 : (left.directory ? -1 : 1);
          if (typeRes === 0) {
            return nameRes;
          }
          res = typeRes;
        } else if (localDiskSortKey === 'size') {
          res = Number(left.size || 0) - Number(right.size || 0);
        } else if (localDiskSortKey === 'modified_at') {
          res = Number(left.modified_at || 0) - Number(right.modified_at || 0);
        } else {
          res = nameRes;
        }
        return localDiskSortOrder === 'desc' ? -res : res;
      }

      function openSelectedLocalDiskTagMenu(anchor) {
        const paths = getSelectedLocalDiskFilePaths();
        if (!paths.length) {
          showStatus(t('请先选择要加标签的本地文件'), 'err');
          return;
        }
        openFilesTagMenu(anchor, paths, { local: true }).catch(function (err) {
          showStatus(t('打开标签选择失败：') + err.message, 'err');
        });
      }

      function isFolderUnlockedInSession(path) {
        const lockedPath = getFolderLockAncestorPath(path);
        return !lockedPath || unlockedFolderPasswords.has(lockedPath);
      }

      function getAudioPlaylistFilesByMode(files, mode) {
        const sorted = sortAudioFilesForPlaylist(files);
        if (mode === 'random') {
          return shuffleList(sorted);
        }
        return sorted;
      }

      async function handleTagLockAction(action, tagId, options) {
        const id = String(tagId || '');
        const opts = options || {};
        if (!action || !id) {
          return false;
        }

        const meta = findTagMetaById(id);
        if (!meta || !meta.node) {
          showStatus(t('标签不存在，可能已被删除'), 'err');
          return false;
        }
        if (!canLockTagNode(meta.node, meta.level)) {
          showStatus(t('保留标签不能加锁'), 'err');
          return false;
        }

        const label = String(meta.node.name || id);
        if (action === 'lock') {
          const password = await askLockPassword({
            title: t('加锁标签'),
            description: t('请为标签「') + label + t('」设置锁密码。加锁后需要输入密码才能查看该标签下的文件。'),
            placeholder: t('请输入新锁密码'),
            errorMessage: t('加锁失败，请重新输入密码。'),
            statusErrorMessage: t('加锁失败：密码错误或验证失败')
          });
          if (password === null) {
            return false;
          }
          await fetchJson(api.tagLock + '?id=' + encodeURIComponent(id) + '&password=' + encodeURIComponent(password), { method: 'POST' });
          deleteUnlockedTagPassword(id);
          await loadTagTreeState();
          if (activeFilterTagId === id) {
            renderFiles([]);
          }
          renderTagTree();
          showStatus(t('标签已加锁：') + label, 'ok');
          return true;
        }

        if (action === 'session-unlock') {
          const password = await askLockPassword({
            title: t('解锁标签'),
            description: t('请输入标签「') + label + t('」的锁密码。'),
            onSubmit: async function (passwordText) {
              await fetchJson(api.tagLockVerify + '?id=' + encodeURIComponent(id) + '&password=' + encodeURIComponent(passwordText), { method: 'POST' });
            }
          });
          if (password === null) {
            return false;
          }
          setUnlockedTagPassword(id, password);
          await loadTagTreeState();
          renderTagTree();
          if (activeFilterTagId === id) {
            await showFilesForTag(id);
          }
          if (!opts.silentSuccess) {
            showStatus(t('标签已解锁（当前会话）：') + label, 'ok');
          }
          return true;
        }

        if (action === 'session-lock') {
          deleteUnlockedTagPassword(id);
          if (activeFilterTagId === id) {
            renderFiles([]);
          }
          renderTagTree();
          showStatus(t('标签已重新加锁：') + label, 'ok');
          return true;
        }

        if (action === 'remove-lock') {
          const password = await askLockPassword({
            title: t('去锁标签'),
            description: t('请输入标签「') + label + t('」的锁密码。验证成功后会永久移除该标签锁。'),
            errorMessage: t('密码错误或去锁失败，请重新输入。'),
            statusErrorMessage: t('去锁失败：密码错误或验证失败'),
            onSubmit: async function (passwordText) {
              await fetchJson(api.tagUnlock + '?id=' + encodeURIComponent(id) + '&password=' + encodeURIComponent(passwordText), { method: 'POST' });
            }
          });
          if (password === null) {
            return false;
          }
          deleteUnlockedTagPassword(id);
          await loadTagTreeState();
          if (activeFilterTagId === id) {
            await showFilesForTag(id);
          } else {
            renderTagTree();
          }
          showStatus(t('标签已去锁：') + label, 'ok');
          return true;
        }

        return false;
      }

      function focusActiveFolderRenameInput() {
        if (!folderTree || !activeFolderRenamePath) {
          return;
        }
        setTimeout(function () {
          if (!folderTree || !activeFolderRenamePath) {
            return;
          }
          const input = folderTree.querySelector('.folder-rename-input[data-folder-rename-input]');
          if (!input) {
            return;
          }
          selectRenameInputText(input);
        }, 0);
      }

      function clearFileRenameClickTimer() {
        if (fileRenameClickTimer) {
          clearTimeout(fileRenameClickTimer);
          fileRenameClickTimer = null;
        }
      }

      function initPreviewImageEditing(win) {
        const shell = win ? win.querySelector('.preview-image-shell') : null;
        const img = win ? win.querySelector('.preview-image') : null;
        const cropRect = win ? win.querySelector('.preview-crop-rect') : null;
        if (!win || !shell || !img || !cropRect) {
          return;
        }

        let drag = null;
        function clampPoint(e) {
          const rect = img.getBoundingClientRect();
          const x = Math.max(rect.left, Math.min(rect.right, e.clientX));
          const y = Math.max(rect.top, Math.min(rect.bottom, e.clientY));
          return { x: x, y: y, imageRect: rect };
        }
        function drawRect(a, b) {
          const shellRect = shell.getBoundingClientRect();
          const left = Math.min(a.x, b.x);
          const top = Math.min(a.y, b.y);
          const width = Math.abs(a.x - b.x);
          const height = Math.abs(a.y - b.y);
          cropRect.hidden = false;
          cropRect.style.left = (left - shellRect.left) + 'px';
          cropRect.style.top = (top - shellRect.top) + 'px';
          cropRect.style.width = width + 'px';
          cropRect.style.height = height + 'px';
          win.__imageCropRect = {
            x: ((left - b.imageRect.left) / b.imageRect.width) * img.naturalWidth,
            y: ((top - b.imageRect.top) / b.imageRect.height) * img.naturalHeight,
            width: (width / b.imageRect.width) * img.naturalWidth,
            height: (height / b.imageRect.height) * img.naturalHeight
          };
        }

        shell.addEventListener('mousedown', function (e) {
          if (!win.__imageCropMode || !img.complete || !img.naturalWidth) {
            return;
          }
          e.preventDefault();
          const start = clampPoint(e);
          drag = { start: start };
          drawRect(start, start);
        });
        window.addEventListener('mousemove', function (e) {
          if (!drag) {
            return;
          }
          e.preventDefault();
          drawRect(drag.start, clampPoint(e));
        });
        window.addEventListener('mouseup', function (e) {
          if (!drag) {
            return;
          }
          e.preventDefault();
          drawRect(drag.start, clampPoint(e));
          drag = null;
        });

        win.addEventListener('click', function (e) {
          const action = e.target.closest('[data-image-edit]');
          if (!action || !win.contains(action)) {
            return;
          }
          e.preventDefault();
          const type = action.getAttribute('data-image-edit') || '';
          if (type === 'rotate-left') {
            rotatePreviewImage(win, 'left');
          } else if (type === 'rotate-right') {
            rotatePreviewImage(win, 'right');
          } else if (type === 'zoom-in') {
            scalePreviewImage(win, 1.25);
          } else if (type === 'zoom-out') {
            scalePreviewImage(win, 0.8);
          } else if (type === 'crop') {
            setPreviewCropMode(win, true);
          } else if (type === 'apply-crop') {
            applyPreviewImageCrop(win);
          } else if (type === 'cancel-crop') {
            cancelPreviewImageCrop(win);
          } else if (type === 'download') {
            downloadPreviewImageEdits(win);
          } else if (type === 'save') {
            savePreviewImageEdits(win);
          }
        });

        win.addEventListener('keydown', function (e) {
          if (!e.target.closest('.preview-size-input[data-image-size]')) {
            return;
          }
          if (e.key === 'Enter') {
            e.preventDefault();
            applyPreviewImageManualSize(win);
          }
        });
      }

      function syncPreviewWindowButtons(win) {
        if (!win) {
          return;
        }
        const maximizeBtn = win.querySelector('[data-preview-window-action="maximize"]');
        if (!maximizeBtn) {
          return;
        }
        const restoreMode = win.classList.contains('is-minimized') || win.classList.contains('is-maximized');
        maximizeBtn.textContent = restoreMode ? '❐' : '□';
        maximizeBtn.title = restoreMode ? t('复原') : t('最大化');
        maximizeBtn.setAttribute('aria-label', restoreMode ? t('复原') : t('最大化'));
      }

      function handleLocalDiskDragStart(e) {
        const dragItem = e.target.closest('[data-local-drag]');
        if (!dragItem || !localDiskExplorer || !localDiskExplorer.contains(dragItem)) {
          return;
        }
        if (e.target.closest('.local-disk-select, .local-mkdir-btn, .local-delete-btn, .local-preview-btn, .local-file-tag-btn')) {
          e.preventDefault();
          return;
        }
        const sourcePath = decodeURIComponent(dragItem.getAttribute('data-local-drag') || '');
        const paths = localDiskDragPathsFor(sourcePath);
        if (!paths.length) {
          e.preventDefault();
          return;
        }
        activeLocalDiskDragPaths = paths;
        dragItem.classList.add('local-disk-dragging');
        if (e.dataTransfer) {
          e.dataTransfer.effectAllowed = 'move';
          e.dataTransfer.setData('application/webcool-local-disk-paths', JSON.stringify(paths));
          e.dataTransfer.setData('text/plain', paths.join('\n'));
        }
      }

      function closeFileContextMenu() {
        if (activeFileContextMenu && activeFileContextMenu.parentNode) {
          activeFileContextMenu.parentNode.removeChild(activeFileContextMenu);
        }
        activeFileContextMenu = null;
      }

      function showStatus(msg, type) {
        statusBox.className = 'status show ' + (type || 'ok');
        statusBox.textContent = msg;
      }

      async function bindFileToTag(tagId, fileName, options) {
        const cleanName = String(fileName || '');
        if (!cleanName) {
          return { ok: false, message: t('请选择要引用的文件') };
        }
        const opts = options || {};

        try {
          await fetchJson(
            api.tagBind + '?tag_id=' + encodeURIComponent(tagId) + '&file=' + encodeURIComponent(cleanName)
              + (opts.local ? '&local=1' : ''),
            { method: 'POST' }
          );
          return { ok: true };
        } catch (err) {
          return { ok: false, message: err.message };
        }
      }

      async function moveFilesToFolder(filePaths, folderPath) {
        const list = Array.isArray(filePaths) ? filePaths.filter(Boolean) : [];
        if (!list.length) {
          return;
        }
        for (let i = 0; i < list.length; i += 1) {
          await fetchJson(
            withFolderPassword(
              appendFilePassword(withFolderPassword(api.fileMove + '?file=' + encodeURIComponent(list[i]) + '&folder=' + encodeURIComponent(folderPath || ''), parentFolderPathFromFilePath(list[i])), list[i], false),
              folderPath || '',
              'target_folder_password'
            ),
            { method: 'POST' }
          );
          selectedFileNames.delete(list[i]);
        }
        await loadFiles();
        showStatus(list.length > 1 ? ('已移动 ' + list.length + ' 个文件') : '文件已移动', 'ok');
      }

      function showFileSummaryDialog(filePath) {
        const file = getFileRecordByPath(filePath);
        if (!file) {
          showStatus(t('文件摘要失败：未找到文件'), 'err');
          return;
        }
        const oldDialog = document.getElementById('file-summary-dialog');
        if (oldDialog && oldDialog.parentNode) {
          oldDialog.parentNode.removeChild(oldDialog);
        }
        const path = getFilePath(file);
        const name = String(file.name || path || '');
        const sizeText = file.directory ? t('文件夹') : (formatNumber(safeSize(file)) + t(' 字节'));
        const createdText = getFileTimeText(file, ['created_time', 'created_at', 'uploaded_time']);
        const modifiedText = getFileTimeText(file, ['modified_time', 'modified_at', 'uploaded_time']);
        const rows = [
          [t('文件名'), name],
          [t('文件大小'), sizeText],
          [t('文件类型'), inferFileTypeLabel(file)],
          [t('创建时间'), createdText],
          [t('修改时间'), modifiedText]
        ];
        const dialog = document.createElement('div');
        dialog.className = 'tag-dialog file-summary-dialog';
        dialog.id = 'file-summary-dialog';
        dialog.innerHTML =
          '<div class="tag-dialog-backdrop" data-file-summary-close="1"></div>' +
          '<div class="tag-dialog-card file-summary-card" role="dialog" aria-modal="true" aria-labelledby="file-summary-title">' +
            '<div class="tag-dialog-head">' +
              '<h2 id="file-summary-title">' + escapeHtml(t('文件摘要')) + '</h2>' +
              '<p>' + escapeHtml(path) + '</p>' +
            '</div>' +
            '<dl class="file-summary-list">' + rows.map(function (row) {
              return '<div class="file-summary-row"><dt>' + escapeHtml(row[0]) + '</dt><dd>' + escapeHtml(row[1]) + '</dd></div>';
            }).join('') + '</dl>' +
            '<div class="tag-dialog-actions">' +
              '<button type="button" class="tag-dialog-btn" data-file-summary-close="1">' + escapeHtml(t('确定')) + '</button>' +
            '</div>' +
          '</div>';
        document.body.appendChild(dialog);
      }

      function startLocalDiskFileRename(path) {
        const filePath = String(path || '');
        const item = getLocalDiskItemByPath(filePath);
        if (!item || item.directory) {
          return;
        }
        clearLocalDiskRenameClickTimer();
        activeLocalDiskRenamePath = filePath;
        renderLocalDiskItems(activeLocalDiskItems);
        window.setTimeout(function () {
          const selector = '.local-disk-rename-input[data-local-disk-rename-path="' + encodeURIComponent(filePath) + '"]';
          const input = (localDiskList && localDiskList.querySelector(selector))
            || (localDiskExplorer && localDiskExplorer.querySelector(selector));
          if (input) {
            selectRenameInputText(input);
          }
        }, 0);
      }

      function activateAdminView(name) {
        const isLanguage = name === 'language';
        if (adminStorageTab) {
          adminStorageTab.classList.toggle('active', !isLanguage);
        }
        if (adminLanguageTab) {
          adminLanguageTab.classList.toggle('active', isLanguage);
        }
        if (adminStorageView) {
          adminStorageView.hidden = isLanguage;
        }
        if (adminLanguageView) {
          adminLanguageView.hidden = !isLanguage;
        }
        if (!isLanguage) {
          loadAdminStoragePath();
        }
      }

function saveUnlockedFilePasswords() {
        try {
          const entries = Array.from(unlockedFilePasswords.entries()).filter(function (entry) {
            return entry[0] && entry[1];
          });
          sessionStorage.setItem(FILE_UNLOCK_SESSION_STORAGE_KEY, JSON.stringify(entries));
        } catch (_) {}
      }

      async function handleLocalDirContextAction(action, path, locked, paths) {
        const dirPath = String(path || '');
        if (!action || !dirPath) {
          return;
        }
        const actionPaths = Array.isArray(paths) && paths.length
          ? paths.map(function (item) { return String(item || ''); }).filter(Boolean)
          : [dirPath];
        if (action === 'copy') {
          localDiskClipboardPaths = actionPaths.slice();
          localDiskClipboardDirectoryFlags = actionPaths.map(function () { return true; });
          localDiskClipboardPath = localDiskClipboardPaths[0] || '';
          localDiskClipboardDirectory = true;
          showStatus(actionPaths.length > 1 ? (t('已拷贝 ') + actionPaths.length + t(' 个本地目录')) : (t('已拷贝本地目录路径：') + dirPath), 'ok');
          return;
        }
        if (action === 'upload') {
          if (actionPaths.length > 1) {
            await openLocalImportDialog(actionPaths);
            return;
          }
          if (locked && !getLocalDirPassword(dirPath) && !(await ensureLocalDirUnlocked(dirPath))) {
            return;
          }
          await openLocalImportDialog([dirPath]);
          return;
        }
        if (action === 'create') {
          if (locked && !getLocalDirPassword(dirPath) && !(await ensureLocalDirUnlocked(dirPath))) {
            return;
          }
          const name = window.prompt(t('请输入新建子目录名称'));
          if (name === null) {
            return;
          }
          const cleanName = String(name || '').trim();
          if (!cleanName) {
            showStatus(t('子目录名称不能为空'), 'err');
            return;
          }
          await fetchJson(appendLocalDirPassword(api.localDiskMkdir + '?path=' + encodeURIComponent(dirPath) + '&name=' + encodeURIComponent(cleanName), dirPath), { method: 'POST' });
          closeFileContextMenu();
          localDiskTreeCache.delete(dirPath);
          await loadLocalDisk(dirPath, { resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, dirPath) });
          showStatus(t('子目录已创建：') + cleanName, 'ok');
          return;
        }
        if (action === 'paste') {
          const clipboardPaths = localDiskClipboardPaths.length ? localDiskClipboardPaths.slice() : (localDiskClipboardPath ? [localDiskClipboardPath] : []);
          const clipboardDirFlags = localDiskClipboardDirectoryFlags.length ? localDiskClipboardDirectoryFlags.slice() : clipboardPaths.map(function () { return !!localDiskClipboardDirectory; });
          if (!clipboardPaths.length) {
            showStatus(t('没有可粘贴的本地文件或目录'), 'err');
            return;
          }
          if (clipboardPaths.some(function (source, index) { return !!clipboardDirFlags[index] && localDiskPathContains(source, dirPath); })) {
            showStatus(t('不能将目录粘贴到自身或其子目录中'), 'err');
            return;
          }
          if (locked && !getLocalDirPassword(dirPath) && !(await ensureLocalDirUnlocked(dirPath))) {
            return;
          }
          const buildCopyUrl = function (sourcePath, sourceDirectory, overwrite) {
            const sourceLockPath = sourceDirectory && getLocalDirPassword(sourcePath)
              ? sourcePath
              : localDiskParentPath(sourcePath);
            let url = api.localDiskCopy
              + '?path=' + encodeURIComponent(sourcePath)
              + '&target=' + encodeURIComponent(dirPath);
            url += '&async=1';
            if (overwrite) {
              url += '&overwrite=1';
            }
            return appendLocalDirPassword(
              appendLocalDirPassword(
                appendFilePassword(url, sourcePath, true),
                sourceLockPath
              ),
              dirPath,
              'target_local_dir_password'
            );
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
            const copiedItemName = copiedPath || sourcePath;
            if (String((result && result.task_id) || '')) {
              closeFolderContextMenu();
              closeFileContextMenu();
              setCopyTaskProgress('local-copy', 0, t('准备粘贴...'), { name: copiedItemName, state: 'running', progress: 0, size: 0, copied: 0 });
              const copyResult = await pollLocalDiskCopyTask(String((result && result.task_id) || ''), dirPath, copiedItemName);
              if (String((copyResult && copyResult.state) || '') !== 'done') {
                return;
              }
            }
          }
          localDiskClipboardPaths = [];
          localDiskClipboardDirectoryFlags = [];
          localDiskClipboardPath = '';
          localDiskClipboardDirectory = false;
          localDiskTreeCache.delete(dirPath);
          await loadLocalDisk(dirPath, { resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, dirPath) });
          showStatus(clipboardPaths.length > 1 ? (t('已粘贴 ') + clipboardPaths.length + t(' 个项目到：') + dirPath) : (t('已粘贴到：') + dirPath), 'ok');
          return;
        }
        if (action === 'lock') {
          const password = await askLockPassword({
            title: t('加锁本地目录'),
            description: t('请为本地目录「') + dirPath + t('」设置锁密码。加锁后需要输入密码才能访问。'),
            placeholder: t('请输入新锁密码'),
            errorMessage: t('加锁失败，请重新输入密码。'),
            statusErrorMessage: t('加锁失败：密码错误或验证失败')
          });
          if (password === null) {
            return;
          }
          await fetchJson(api.fileLock + '?local=1&dir=1&path=' + encodeURIComponent(dirPath) + '&password=' + encodeURIComponent(password), { method: 'POST' });
          deleteUnlockedLocalDirPassword(dirPath);
          setLocalDiskDirLockedState(dirPath, true);
          invalidateLocalDiskDirLockCache(dirPath);
          const nextPath = localDiskPathContains(dirPath, activeLocalDiskPath)
            ? localDiskParentPath(dirPath)
            : (activeLocalDiskPath || '');
          await loadLocalDisk(nextPath, { resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, nextPath) });
          showStatus(t('本地目录已加锁：') + dirPath, 'ok');
          return;
        }
        if (action === 'session-unlock') {
          const password = await askLockPassword({
            title: t('解锁本地目录'),
            description: t('请输入本地目录「') + dirPath + t('」的锁密码。'),
            onSubmit: async function (passwordText) {
              await fetchJson(api.fileLockVerify + '?local=1&dir=1&path=' + encodeURIComponent(dirPath) + '&password=' + encodeURIComponent(passwordText), { method: 'POST' });
            }
          });
          if (password === null) {
            return;
          }
          setUnlockedLocalDirPassword(dirPath, password);
          await loadLocalDisk(dirPath, { resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, dirPath) });
          showStatus(t('本地目录已解锁（当前会话）：') + dirPath, 'ok');
          return;
        }
        if (action === 'session-lock') {
          deleteUnlockedLocalDirPassword(dirPath);
          if (localDiskPathContains(dirPath, activeLocalDiskPath)) {
            await loadLocalDisk(localDiskParentPath(dirPath), { resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, localDiskParentPath(dirPath)) });
          } else {
            renderLocalDiskItems(activeLocalDiskItems);
          }
          showStatus(t('本地目录已重新加锁：') + dirPath, 'ok');
          return;
        }
        if (action === 'remove-lock') {
          const password = await askLockPassword({
            title: t('去锁本地目录'),
            description: t('请输入本地目录「') + dirPath + t('」的锁密码。验证成功后会永久移除该目录锁。'),
            errorMessage: t('密码错误或去锁失败，请重新输入。'),
            statusErrorMessage: t('去锁失败：密码错误或验证失败'),
            onSubmit: async function (passwordText) {
              await fetchJson(api.fileUnlock + '?local=1&dir=1&path=' + encodeURIComponent(dirPath) + '&password=' + encodeURIComponent(passwordText), { method: 'POST' });
            }
          });
          if (password === null) {
            return;
          }
          deleteUnlockedLocalDirPassword(dirPath);
          setLocalDiskDirLockedState(dirPath, false);
          await loadLocalDisk(activeLocalDiskPath || '', { resetTreeRoot: false });
          showStatus(t('本地目录已去锁：') + dirPath, 'ok');
          return;
        }
        if (action === 'rename') {
          if (locked && !getLocalDirPassword(dirPath) && !(await ensureLocalDirUnlocked(dirPath))) {
            return;
          }
          const currentName = localBaseName(dirPath);
          const nextName = window.prompt(t('请输入新的目录名称'), currentName);
          if (nextName === null) {
            return;
          }
          const cleanName = String(nextName || '').trim();
          if (!cleanName || cleanName === currentName) {
            return;
          }
          const url = appendLocalDirPassword(api.localDiskRename
            + '?path=' + encodeURIComponent(dirPath)
            + '&name=' + encodeURIComponent(cleanName), dirPath);
          const result = await fetchJson(url, { method: 'POST' });
          const nextPath = String((result && result.path) || localDiskParentPath(dirPath));
          localDiskTreeCache.delete(dirPath);
          expandedLocalDiskTreePaths.delete(dirPath);
          await loadLocalDisk(nextPath, { resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, nextPath) });
          showStatus(t('本地目录已改名：') + dirPath + ' -> ' + nextPath, 'ok');
          return;
        }
        if (action === 'delete') {
          if (actionPaths.length > 1) {
            if (!confirm(t('确认将选中的 ') + actionPaths.length + t(' 个本地目录移至回收站？'))) {
              return;
            }
            for (let i = 0; i < actionPaths.length; i += 1) {
              const targetPath = actionPaths[i];
              await fetchJson(appendLocalDirPassword(api.localDiskDelete + '?path=' + encodeURIComponent(targetPath), targetPath), { method: 'POST' });
              localDiskTreeCache.delete(targetPath);
              expandedLocalDiskTreePaths.delete(targetPath);
              selectedLocalDiskPaths.delete(targetPath);
            }
            await loadLocalDisk(activeLocalDiskPath || localDiskParentPath(dirPath), { resetTreeRoot: false });
            showStatus(t('已移除 ') + actionPaths.length + t(' 个本地目录到回收站'), 'warn');
            return;
          }
          if (locked && !getLocalDirPassword(dirPath) && !(await ensureLocalDirUnlocked(dirPath))) {
            return;
          }
          if (!confirm(t('确认将本地目录移至回收站：') + dirPath + t(' ？'))) {
            return;
          }
          const url = appendLocalDirPassword(api.localDiskDelete + '?path=' + encodeURIComponent(dirPath), dirPath);
          await fetchJson(url, { method: 'POST' });
          localDiskTreeCache.delete(dirPath);
          expandedLocalDiskTreePaths.delete(dirPath);
          const nextPath = localDiskParentPath(dirPath);
          await loadLocalDisk(nextPath, { resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, nextPath) });
          showStatus(t('本地目录已移至回收站：') + dirPath, 'warn');
          return;
        }
      }

      function makeTagId() {
        return 'tag_' + Date.now().toString(36) + '_' + Math.random().toString(36).slice(2, 9);
      }

      function updateTranscodeProgress(encodedName, percent, text) {
        const fill = statusBox.querySelector('[data-progress-fill="' + encodedName + '"]');
        const label = statusBox.querySelector('[data-progress-text="' + encodedName + '"]');
        if (fill) {
          fill.style.width = Math.max(0, Math.min(100, percent || 0)) + '%';
        }
        if (label) {
          label.textContent = text || '';
        }
      }

      function applyLocalImportProgressWindowState() {
        if (localImportProgressDialog) {
          localImportProgressDialog.classList.toggle('is-minimized', !!localImportProgressMinimized);
        }
        const showWindowControls = isCopyTaskWindowMode(localImportProgressWindowMode);
        if (localImportProgressMinimize) {
          localImportProgressMinimize.hidden = !showWindowControls || !!localImportProgressMinimized;
        }
        if (localImportProgressRestore) {
          localImportProgressRestore.hidden = !showWindowControls || !localImportProgressMinimized;
        }
      }

      function stepImagePreviewWindow(win, delta) {
        if (!win || !Array.isArray(win.__imageGallery) || !win.__imageGallery.length) {
          return;
        }
        const nextIndex = Number(win.__imageIndex || 0) + Number(delta || 0);
        if (nextIndex < 0 || nextIndex >= win.__imageGallery.length) {
          return;
        }
        updateImagePreviewWindow(win, win.__imageGallery, nextIndex);
      }

      function getSelectedLocalDiskPaths() {
        const visible = getVisibleLocalDiskPathSet();
        return Array.from(selectedLocalDiskPaths).filter(function (path) {
          return visible.has(path);
        });
      }

      async function loadAdminStoragePath() {
        if (!adminStoragePath) {
          return;
        }
        try {
          const data = await fetchJson(api.adminStorage);
          currentAdminStoragePath = String(data.path || '');
          adminStoragePath.value = currentAdminStoragePath;
        } catch (err) {
          showStatus(t('加载存储路径失败：') + err.message, 'err');
        }
      }

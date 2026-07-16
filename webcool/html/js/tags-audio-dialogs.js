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
        if (localImportProgressPause) {
          localImportProgressPause.hidden = true;
        }
        if (localImportProgressResume) {
          localImportProgressResume.hidden = true;
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
        audioEl.addEventListener('loadedmetadata', function () {
          syncVolumeAndRate();
          syncCurrentTime(true);
          if (!videoEl.paused && !videoEl.ended) {
            audioEl.play().catch(function () {});
          }
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

      async function removeSelectedLocalDiskFiles() {
        const paths = getSelectedLocalDiskFilePaths();
        if (!paths.length) {
          return;
        }
        const confirmed = await askDeleteConfirmDialog({
          title: t('删除'),
          description: t('选中的本地文件将移至系统回收站。'),
          highlight: paths.length + t(' 个文件'),
          confirmText: t('移除')
        });
        if (!confirmed) {
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
        return /\.(rm|rmvb|avi|mov|wmv|mpg|mpeg)$/i.test(String(name || ''));
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
        setUploadProgressControls('idle');
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
        if (isMovVideoName(fileName)) {
          return;
        }
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
          const speedState = {
            lastTime: Date.now(),
            lastCopied: 0,
            speed: 0,
            fileCopied: new Map(),
            fileSpeeds: new Map()
          };
          const timer = setInterval(function () {
            fetchJson(api.localDiskImportProgress + '?task_id=' + encodeURIComponent(taskId))
              .then(function (data) {
                const progress = Math.max(0, Math.min(100, Number(data.progress || 0)));
                const state = String(data.state || '');
                setLocalImportProgressControls(state);
                const now = Date.now();
                const copiedBytes = Number(data.copied_bytes || 0);
                const elapsed = Math.max(1, now - speedState.lastTime);
                if (copiedBytes >= speedState.lastCopied && elapsed >= 250) {
                  speedState.speed = ((copiedBytes - speedState.lastCopied) * 1000) / elapsed;
                  const files = Array.isArray(data.files) ? data.files : [];
                  files.forEach(function (file, index) {
                    const key = String((file && (file.path || file.remote_path || file.name)) || index);
                    const copied = Number((file && file.copied) || 0);
                    const last = speedState.fileCopied.has(key) ? Number(speedState.fileCopied.get(key) || 0) : copied;
                    const fileSpeed = copied >= last ? ((copied - last) * 1000) / elapsed : 0;
                    speedState.fileCopied.set(key, copied);
                    speedState.fileSpeeds.set(key, fileSpeed);
                  });
                  speedState.lastCopied = copiedBytes;
                  speedState.lastTime = now;
                }
                setLocalImportProgress(progress, appendSpeedText((data.message || t('上传中')) + ' ' + Math.round(progress) + '%', speedState.speed));
                renderLocalImportProgressFiles(data.files, speedState.fileSpeeds);
                if (state === 'done') {
                  clearInterval(timer);
                  activeLocalImportTaskId = '';
                  activeLocalImportCancelRequested = false;
                  finishLocalImportProgress(t('上传完成 100%'));
                  resolve(data);
                } else if (state === 'failed' || state === 'cancelled') {
                  clearInterval(timer);
                  activeLocalImportTaskId = '';
                  activeLocalImportCancelRequested = false;
                  reject(new Error(data.error || (state === 'cancelled' ? t('上传已取消') : t('上传失败'))));
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
        win.__imageCropOperation = '';
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
        if (!key || isMovVideoName(key)) {
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
          } else if (type === 'enhance') {
            openImageEnhanceDialog(win.__imageEnhancePath, !!win.__imageEnhanceLocal);
          } else if (type === 'red-eye') {
            openImageEnhanceDialog(win.__imageEnhancePath, !!win.__imageEnhanceLocal, 'red_eye');
          } else if (type === 'crop') {
            win.__imageCropOperation = 'crop';
            setPreviewCropMode(win, true);
          } else if (type === 'watermark') {
            win.__imageCropOperation = 'watermark';
            win.__imageCropRect = null;
            cropRect.hidden = true;
            cropRect.removeAttribute('style');
            setPreviewCropMode(win, true);
          } else if (type === 'apply-crop') {
            if (win.__imageCropOperation === 'watermark') {
              removePreviewImageWatermark(win);
            } else {
              applyPreviewImageCrop(win);
            }
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

      async function removePreviewImageWatermark(win) {
        if (!win || win.__imageWatermarkBusy) return;
        const rect = win.__imageCropRect;
        const img = win.querySelector('.preview-image');
        const path = String(win.__imageEnhancePath || '');
        const local = !!win.__imageEnhanceLocal;
        if (!rect || !img || !img.naturalWidth || !path) {
          setImageEditHint(win, t('请先在图片上拖拽框选水印区域。'), true);
          return;
        }
        if (img.naturalWidth < 4 || img.naturalHeight < 4) {
          setImageEditHint(win, t('图片尺寸太小，无法去水印。'), true);
          return;
        }
        const x = Math.max(0, Math.min(img.naturalWidth - 4, Math.floor(rect.x)));
        const y = Math.max(0, Math.min(img.naturalHeight - 4, Math.floor(rect.y)));
        const width = Math.min(img.naturalWidth - x, Math.max(4, Math.round(rect.width)));
        const height = Math.min(img.naturalHeight - y, Math.max(4, Math.round(rect.height)));
        if (width < 4 || height < 4) {
          setImageEditHint(win, t('水印框选区域太小，请重新框选。'), true);
          return;
        }

        win.__imageWatermarkBusy = true;
        setImageEditHint(win, t('正在启动去水印处理…'));
        try {
          let startUrl = (local ? api.localDiskImageEnhance : api.imageEnhance)
            + '?' + (local ? 'path=' : 'file=') + encodeURIComponent(path)
            + '&method=watermark'
            + '&watermark_x=' + encodeURIComponent(String(x))
            + '&watermark_y=' + encodeURIComponent(String(y))
            + '&watermark_width=' + encodeURIComponent(String(width))
            + '&watermark_height=' + encodeURIComponent(String(height));
          if (local) {
            startUrl = appendLocalDirPassword(appendFilePassword(startUrl, path, true), localDiskParentPath(path));
          } else {
            startUrl = appendFilePassword(withFolderPassword(startUrl, parentFolderPathFromFilePath(path)), path, false);
          }
          const started = await fetchJson(startUrl, { method: 'POST' });
          const taskId = String(started.task_id || '');
          if (!taskId) throw new Error(t('无法启动图片转换'));
          const progressBase = local ? api.localDiskImageEnhanceProgress : api.imageEnhanceProgress;
          const poll = async function () {
            let progressUrl = progressBase + '?task_id=' + encodeURIComponent(taskId);
            if (local) progressUrl = appendLocalDirPassword(progressUrl, localDiskParentPath(path));
            const task = await fetchJson(progressUrl);
            if (win.isConnected) {
              setImageEditHint(win, Math.max(0, Math.min(100, Math.round(Number(task.progress) || 0)))
                + '% · ' + String(task.message || t('正在去水印')));
            }
            if (!task.done) {
              await new Promise(function (resolve) { window.setTimeout(resolve, 700); });
              return poll();
            }
            if (!task.success) throw new Error(task.error || task.message || t('图片去水印失败'));
            if (win.isConnected) {
              cancelPreviewImageCrop(win);
              setImageEditHint(win, t('去水印完成，已生成新图片：') + String(task.name || started.name || ''));
            }
            if (local) await loadLocalDisk(activeLocalDiskPath || localDiskParentPath(path) || '');
            else await loadFiles();
          };
          await poll();
        } catch (err) {
          if (win.isConnected) setImageEditHint(win, t('图片去水印失败：') + (err && err.message ? err.message : err), true);
        } finally {
          win.__imageWatermarkBusy = false;
        }
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

      function isLocalDiskDragEvent(e) {
        if (activeLocalDiskDragPaths.length) {
          return true;
        }
        const dt = e && e.dataTransfer;
        if (!dt || !dt.types) {
          return false;
        }
        const types = dt.types;
        if (typeof types.contains === 'function') {
          return types.contains('application/webcool-local-disk-paths');
        }
        return Array.prototype.indexOf.call(types, 'application/webcool-local-disk-paths') >= 0;
      }

      function parseLocalDiskDragPaths(dataTransfer) {
        let paths = [];
        if (dataTransfer) {
          try {
            paths = JSON.parse(dataTransfer.getData('application/webcool-local-disk-paths') || '[]');
          } catch (_) {
            paths = [];
          }
        }
        if (!paths.length && activeLocalDiskDragPaths.length) {
          paths = activeLocalDiskDragPaths.slice();
        }
        return paths.map(function (path) { return String(path || ''); }).filter(Boolean);
      }

      function handleLocalDiskDragStart(e) {
        const dragItem = e.target.closest('[data-local-drag]');
        if (!dragItem) {
          return;
        }
        const inExplorer = localDiskExplorer && localDiskExplorer.contains(dragItem);
        const inTable = localDiskList && localDiskList.contains(dragItem);
        if (!inExplorer && !inTable) {
          return;
        }
        if (e.target.closest('.local-disk-select, .local-mkdir-btn, .local-delete-btn, .local-preview-btn, .local-file-tag-btn, .file-rename-input')) {
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
          e.dataTransfer.effectAllowed = 'copyMove';
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
          const sourcePath = String(list[i] || '');
          await fetchJson(
            withFolderPassword(
              appendFilePassword(withFolderPassword(api.fileMove + '?file=' + encodeURIComponent(sourcePath) + '&folder=' + encodeURIComponent(folderPath || ''), parentFolderPathFromFilePath(sourcePath)), sourcePath, false),
              folderPath || '',
              'target_folder_password'
            ),
            { method: 'POST' }
          );
          selectedFileNames.delete(sourcePath);
        }
        await refreshFolderRowsForChangedFiles(list, folderPath || '');
        await loadFiles();
        showStatus(list.length > 1 ? ('已移动 ' + list.length + ' 个文件') : '文件已移动', 'ok');
      }

      function summaryImageResolutionFromRecord(file) {
        const width = Math.round(Number(file && (file.width || file.image_width || file.pixel_width)) || 0);
        const height = Math.round(Number(file && (file.height || file.image_height || file.pixel_height)) || 0);
        return width > 0 && height > 0 ? (width + ' × ' + height + ' px') : '';
      }

      function loadSummaryImageResolution(dialog, url) {
        const value = dialog && dialog.querySelector('[data-file-summary-resolution]');
        if (!value || !url) return;
        const apply = function (width, height) {
          if (!dialog.isConnected) return;
          const w = Math.max(0, Math.round(Number(width) || 0));
          const h = Math.max(0, Math.round(Number(height) || 0));
          value.textContent = w && h ? (w + ' × ' + h + ' px') : '-';
        };
        const image = new Image();
        image.decoding = 'async';
        image.onload = function () { apply(image.naturalWidth, image.naturalHeight); };
        image.onerror = function () {
          if (typeof createImageBitmap !== 'function') { apply(0, 0); return; }
          fetch(url).then(function (response) {
            if (!response.ok) throw new Error('image fetch failed');
            return response.blob();
          }).then(function (blob) {
            return createImageBitmap(blob);
          }).then(function (bitmap) {
            apply(bitmap.width, bitmap.height);
            bitmap.close();
          }).catch(function () { apply(0, 0); });
        };
        image.src = url;
      }

      function fileSummaryResolutionHtml(label, value) {
        return '<div class="file-summary-row file-summary-resolution-row">' +
          '<dt>' + escapeHtml(label) + '</dt>' +
          '<dd><span class="file-summary-resolution-value" data-file-summary-resolution>' + escapeHtml(value) + '</span></dd>' +
        '</div>';
      }

      function fileSummaryHeaderHtml(path) {
        return '<div class="tag-dialog-head file-summary-head" data-file-summary-drag-handle>' +
          '<div class="file-summary-head-copy">' +
            '<h2 id="file-summary-title">' + escapeHtml(t('文件摘要')) + '</h2>' +
            '<p>' + escapeHtml(path) + '</p>' +
          '</div>' +
          '<div class="file-summary-window-actions">' +
            '<button type="button" data-file-summary-minimize title="' + escapeHtml(t('最小化')) + '" aria-label="' + escapeHtml(t('最小化')) + '">−</button>' +
            '<button type="button" data-file-summary-close="1" title="' + escapeHtml(t('关闭')) + '" aria-label="' + escapeHtml(t('关闭')) + '">×</button>' +
          '</div>' +
        '</div>';
      }

      function bindFileSummaryWindow(dialog) {
        const card = dialog && dialog.querySelector('.file-summary-card');
        const handle = dialog && dialog.querySelector('[data-file-summary-drag-handle]');
        const minimizeButton = dialog && dialog.querySelector('[data-file-summary-minimize]');
        if (!card || !handle || !minimizeButton) return;
        const placeCard = function (left, top) {
          const maxLeft = Math.max(8, window.innerWidth - card.offsetWidth - 8);
          const maxTop = Math.max(8, window.innerHeight - card.offsetHeight - 8);
          card.style.position = 'fixed';
          card.style.left = Math.max(8, Math.min(maxLeft, left)) + 'px';
          card.style.top = Math.max(8, Math.min(maxTop, top)) + 'px';
          card.style.right = 'auto';
          card.style.bottom = 'auto';
        };
        minimizeButton.addEventListener('click', function (event) {
          event.preventDefault();
          event.stopPropagation();
          const rect = card.getBoundingClientRect();
          const minimized = !dialog.classList.contains('is-minimized');
          dialog.classList.toggle('is-minimized', minimized);
          card.setAttribute('aria-modal', minimized ? 'false' : 'true');
          minimizeButton.textContent = minimized ? '□' : '−';
          minimizeButton.title = t(minimized ? '还原' : '最小化');
          minimizeButton.setAttribute('aria-label', minimizeButton.title);
          placeCard(rect.left, rect.top);
        });
        handle.addEventListener('pointerdown', function (event) {
          if (event.button !== 0 || event.target.closest('button, select, input, a')) return;
          const rect = card.getBoundingClientRect();
          const startX = event.clientX;
          const startY = event.clientY;
          const startLeft = rect.left;
          const startTop = rect.top;
          handle.classList.add('is-dragging');
          handle.setPointerCapture(event.pointerId);
          const move = function (moveEvent) {
            placeCard(startLeft + moveEvent.clientX - startX, startTop + moveEvent.clientY - startY);
          };
          const stop = function () {
            handle.classList.remove('is-dragging');
            handle.removeEventListener('pointermove', move);
            handle.removeEventListener('pointerup', stop);
            handle.removeEventListener('pointercancel', stop);
          };
          handle.addEventListener('pointermove', move);
          handle.addEventListener('pointerup', stop);
          handle.addEventListener('pointercancel', stop);
          event.preventDefault();
        });
        window.addEventListener('resize', function () {
          if (!dialog.isConnected || card.style.position !== 'fixed') return;
          const rect = card.getBoundingClientRect();
          placeCard(rect.left, rect.top);
        });
      }

      function bindFileSummaryEnhance(dialog, path, local, initialMethod) {
        const button = dialog && dialog.querySelector('[data-file-summary-enhance-convert]');
        const select = dialog && dialog.querySelector('[data-file-summary-enhance]');
        const settingsButton = dialog && dialog.querySelector('[data-file-summary-ai-settings]');
        const status = dialog && dialog.querySelector('[data-file-summary-enhance-status]');
        const progress = dialog && dialog.querySelector('[data-file-summary-enhance-progress]');
        const message = dialog && dialog.querySelector('[data-file-summary-enhance-message]');
        if (!button || !select || !settingsButton || !status || !progress || !message) return;
        const macPlatform = /Mac/i.test(navigator.platform || navigator.userAgent || '');
        const aiSettings = {
          model: macPlatform ? 'coreml-x2plus' : 'realesrgan-x4plus',
          scale: macPlatform ? 2 : 4,
          denoise: 0,
          sharpen: 0,
          computeUnits: 'auto',
          tile: 0,
          overlap: 'balanced',
          restormerMode: 'motion',
          restormerStrength: 35,
          faceRestoration: 'none',
          codeformerMode: 'whole',
          faceFidelity: 90,
          faceOnlyCenter: true
        };
        const redEyeSettings = { strength: 80, onlyCenterFace: false };
        const brightnessSettings = { amount: 20 };
        const codeformerSettings = {
          mode: 'aligned',
          fidelity: 50,
          onlyCenterFace: true,
          upscale: false
        };
        if (initialMethod && select.querySelector('option[value="' + initialMethod + '"]')) {
          select.value = initialMethod;
        }
        const openRedEyeSettings = function () {
          if (document.querySelector('.image-red-eye-settings-dialog')) return;
          const settingsDialog = document.createElement('div');
          settingsDialog.className = 'video-screenshot-ai-dialog image-ai-settings-dialog image-red-eye-settings-dialog';
          settingsDialog.innerHTML = '<section class="video-screenshot-ai-card" role="dialog" aria-modal="true" aria-label="' + escapeHtml(t('去红眼设置')) + '">' +
            '<header><h3>' + escapeHtml(t('去红眼设置')) + '</h3><button type="button" data-red-eye-close aria-label="' + escapeHtml(t('关闭')) + '">×</button></header>' +
            '<div class="video-screenshot-ai-body">' +
              '<label><span>' + escapeHtml(t('校正强度')) + '</span><span class="video-screenshot-ai-range"><input type="range" min="0" max="100" step="5" data-red-eye-strength><output data-red-eye-strength-value></output></span></label>' +
              '<label><span>' + escapeHtml(t('仅处理主体人脸')) + '</span><input type="checkbox" data-red-eye-center></label>' +
              '<p>' + escapeHtml(t('仅在检测到的瞳孔区域内校正红色像素，不会生成或重绘五官。')) + '</p>' +
            '</div><footer><button type="button" data-red-eye-cancel>' + escapeHtml(t('取消')) + '</button><button type="button" class="primary" data-red-eye-save>' + escapeHtml(t('保存设置')) + '</button></footer>' +
          '</section>';
          document.body.appendChild(settingsDialog);
          const strength = settingsDialog.querySelector('[data-red-eye-strength]');
          const value = settingsDialog.querySelector('[data-red-eye-strength-value]');
          const center = settingsDialog.querySelector('[data-red-eye-center]');
          strength.value = String(redEyeSettings.strength);
          center.checked = redEyeSettings.onlyCenterFace;
          const sync = function () { value.textContent = strength.value + '%'; };
          strength.addEventListener('input', sync);
          sync();
          const close = function () { settingsDialog.remove(); };
          settingsDialog.querySelector('[data-red-eye-close]').addEventListener('click', close);
          settingsDialog.querySelector('[data-red-eye-cancel]').addEventListener('click', close);
          settingsDialog.addEventListener('click', function (event) { if (event.target === settingsDialog) close(); });
          settingsDialog.querySelector('[data-red-eye-save]').addEventListener('click', function () {
            redEyeSettings.strength = Number(strength.value) || 0;
            redEyeSettings.onlyCenterFace = center.checked;
            close();
          });
        };
        const openBrightnessSettings = function () {
          if (document.querySelector('.image-brightness-settings-dialog')) return;
          const settingsDialog = document.createElement('div');
          settingsDialog.className = 'video-screenshot-ai-dialog image-ai-settings-dialog image-brightness-settings-dialog';
          settingsDialog.innerHTML = '<section class="video-screenshot-ai-card" role="dialog" aria-modal="true" aria-label="' + escapeHtml(t('亮度设置')) + '">' +
            '<header><h3>' + escapeHtml(t('亮度设置')) + '</h3><button type="button" data-brightness-close aria-label="' + escapeHtml(t('关闭')) + '">×</button></header>' +
            '<div class="video-screenshot-ai-body">' +
              '<label><span>' + escapeHtml(t('亮度')) + '</span><span class="video-screenshot-ai-range"><input type="range" min="-100" max="100" step="5" data-brightness-amount><output data-brightness-value></output></span></label>' +
              '<p>' + escapeHtml(t('正值提亮照片，负值压暗照片；建议先从20%开始。')) + '</p>' +
            '</div><footer><button type="button" data-brightness-cancel>' + escapeHtml(t('取消')) + '</button><button type="button" class="primary" data-brightness-save>' + escapeHtml(t('保存设置')) + '</button></footer>' +
          '</section>';
          document.body.appendChild(settingsDialog);
          const amount = settingsDialog.querySelector('[data-brightness-amount]');
          const value = settingsDialog.querySelector('[data-brightness-value]');
          amount.value = String(brightnessSettings.amount);
          const sync = function () {
            const current = Number(amount.value) || 0;
            value.textContent = (current > 0 ? '+' : '') + current + '%';
          };
          amount.addEventListener('input', sync);
          sync();
          const close = function () { settingsDialog.remove(); };
          settingsDialog.querySelector('[data-brightness-close]').addEventListener('click', close);
          settingsDialog.querySelector('[data-brightness-cancel]').addEventListener('click', close);
          settingsDialog.addEventListener('click', function (event) { if (event.target === settingsDialog) close(); });
          settingsDialog.querySelector('[data-brightness-save]').addEventListener('click', function () {
            brightnessSettings.amount = Number(amount.value) || 0;
            close();
          });
        };
        const openCodeformerSettings = function () {
          if (document.querySelector('.image-codeformer-settings-dialog')) return;
          const settingsDialog = document.createElement('div');
          settingsDialog.className = 'video-screenshot-ai-dialog image-ai-settings-dialog image-codeformer-settings-dialog';
          settingsDialog.innerHTML = '<section class="video-screenshot-ai-card" role="dialog" aria-modal="true" aria-label="' + escapeHtml(t('CodeFormer人脸重建设置')) + '">' +
            '<header><h3>' + escapeHtml(t('CodeFormer人脸重建设置')) + '</h3><button type="button" data-codeformer-close aria-label="' + escapeHtml(t('关闭')) + '">×</button></header>' +
            '<div class="video-screenshot-ai-body">' +
              '<label><span>' + escapeHtml(t('修复模式')) + '</span><select data-codeformer-mode><option value="aligned">' + escapeHtml(t('白色遮挡人脸重建')) + '</option><option value="whole">' + escapeHtml(t('普通照片人脸修复')) + '</option></select></label>' +
              '<label data-codeformer-fidelity-row><span>' + escapeHtml(t('人脸保真度（越高越接近原貌）')) + '</span><span class="video-screenshot-ai-range"><input type="range" min="0" max="100" step="5" data-codeformer-fidelity><output data-codeformer-fidelity-value></output></span></label>' +
              '<label data-codeformer-center-row><span>' + escapeHtml(t('仅修复主体人脸（防止局部误识别）')) + '</span><input type="checkbox" data-codeformer-center></label>' +
              '<label><span>' + escapeHtml(t('修复后继续AI超分')) + '</span><input type="checkbox" data-codeformer-upscale></label>' +
              '<p data-codeformer-mode-hint></p>' +
              '<p>' + escapeHtml(t('遮挡区域没有真实信息，模型重建结果可能与原人物不同。')) + '</p>' +
            '</div><footer><button type="button" data-codeformer-cancel>' + escapeHtml(t('取消')) + '</button><button type="button" class="primary" data-codeformer-save>' + escapeHtml(t('保存设置')) + '</button></footer>' +
          '</section>';
          document.body.appendChild(settingsDialog);
          const mode = settingsDialog.querySelector('[data-codeformer-mode]');
          const fidelity = settingsDialog.querySelector('[data-codeformer-fidelity]');
          const fidelityValue = settingsDialog.querySelector('[data-codeformer-fidelity-value]');
          const fidelityRow = settingsDialog.querySelector('[data-codeformer-fidelity-row]');
          const centerRow = settingsDialog.querySelector('[data-codeformer-center-row]');
          const center = settingsDialog.querySelector('[data-codeformer-center]');
          const upscale = settingsDialog.querySelector('[data-codeformer-upscale]');
          const hint = settingsDialog.querySelector('[data-codeformer-mode-hint]');
          mode.value = codeformerSettings.mode;
          fidelity.value = String(codeformerSettings.fidelity);
          center.checked = codeformerSettings.onlyCenterFace;
          upscale.checked = codeformerSettings.upscale;
          const sync = function () {
            const aligned = mode.value === 'aligned';
            centerRow.hidden = aligned;
            fidelityRow.hidden = aligned;
            fidelityValue.textContent = fidelity.value + '%';
            hint.textContent = aligned
              ? t('使用CodeFormer专用遮挡修复模型，将纯白遮挡区域重建为512×512人脸。')
              : t('先检测照片中的人脸，修复后回贴到原图，保持原图尺寸。');
          };
          mode.addEventListener('change', sync);
          fidelity.addEventListener('input', sync);
          sync();
          const close = function () { settingsDialog.remove(); };
          settingsDialog.querySelector('[data-codeformer-close]').addEventListener('click', close);
          settingsDialog.querySelector('[data-codeformer-cancel]').addEventListener('click', close);
          settingsDialog.addEventListener('click', function (event) { if (event.target === settingsDialog) close(); });
          settingsDialog.querySelector('[data-codeformer-save]').addEventListener('click', function () {
            codeformerSettings.mode = mode.value;
            codeformerSettings.fidelity = Number(fidelity.value) || 0;
            codeformerSettings.onlyCenterFace = center.checked;
            codeformerSettings.upscale = upscale.checked;
            close();
          });
        };
        const openAiSettings = function () {
          if (document.querySelector('.image-ai-settings-dialog')) return;
          const settingsDialog = document.createElement('div');
          settingsDialog.className = 'video-screenshot-ai-dialog image-ai-settings-dialog';
          settingsDialog.innerHTML = '<section class="video-screenshot-ai-card" role="dialog" aria-modal="true" aria-label="' + escapeHtml(t('AI图片超分详细设置')) + '">' +
            '<header><h3>' + escapeHtml(t('AI图片超分详细设置')) + '</h3><button type="button" data-image-ai-close aria-label="' + escapeHtml(t('关闭')) + '">×</button></header>' +
            '<div class="video-screenshot-ai-body">' +
              '<label><span>' + escapeHtml(t('AI模型')) + '</span><select data-image-ai-model>' +
                '<option value="coreml-x2plus">' + escapeHtml(t('真实2×（高质量/较快）')) + '</option>' +
                '<option value="coreml-general-x4v3">' + escapeHtml(t('轻量x4（速度优先）')) + '</option>' +
                '<option value="coreml-general-x4v3-w8a8">' + escapeHtml(t('轻量W8A8 x4（M4实验）')) + '</option>' +
                '<option value="coreml-x4plus-int8">' + escapeHtml(t('M4量化x4（质量优先）')) + '</option>' +
                '<option value="realesrgan-x4plus">RealESRGAN x4plus</option>' +
                '<option value="realesr-animevideov3">RealESRGAN AnimeVideo v3</option>' +
              '</select></label>' +
              '<label data-image-restormer-row><span>' + escapeHtml(t('去模糊类型')) + '</span><select data-image-restormer-mode><option value="motion">' + escapeHtml(t('运动模糊')) + '</option><option value="defocus">' + escapeHtml(t('失焦模糊')) + '</option></select></label>' +
              '<label data-image-restormer-strength-row><span>' + escapeHtml(t('去模糊强度（保留原貌）')) + '</span><span class="video-screenshot-ai-range"><input type="range" min="0" max="100" step="5" data-image-restormer-strength><output data-image-restormer-strength-value></output></span></label>' +
              '<label><span>' + escapeHtml(t('人脸修复')) + '</span><select data-image-face-restoration><option value="none">' + escapeHtml(t('关闭')) + '</option><option value="codeformer">CodeFormer</option></select></label>' +
              '<label data-image-codeformer-mode-row hidden><span>' + escapeHtml(t('CodeFormer修复模式')) + '</span><select data-image-codeformer-mode><option value="whole">' + escapeHtml(t('普通照片人脸修复')) + '</option><option value="inpaint">' + escapeHtml(t('白色遮挡人脸重建')) + '</option></select></label>' +
              '<label data-image-face-fidelity-row hidden><span>' + escapeHtml(t('人脸保真度（越高越接近原貌）')) + '</span><span class="video-screenshot-ai-range"><input type="range" min="0" max="100" step="5" data-image-face-fidelity><output data-image-face-fidelity-value></output></span></label>' +
              '<label data-image-face-center-row hidden><span>' + escapeHtml(t('仅修复主体人脸（防止局部误识别）')) + '</span><input type="checkbox" data-image-face-only-center checked></label>' +
              '<label><span>' + escapeHtml(t('AI放大倍数')) + '</span><select data-image-ai-scale><option value="2">2×</option><option value="4">4×</option></select></label>' +
              '<label><span>' + escapeHtml(t('AI前降噪')) + '</span><select data-image-ai-denoise><option value="0">' + escapeHtml(t('关闭')) + '</option><option value="1">' + escapeHtml(t('轻度')) + '</option><option value="2">' + escapeHtml(t('中度')) + '</option></select></label>' +
              '<label><span>' + escapeHtml(t('轻微去模糊/预锐化')) + '</span><span class="video-screenshot-ai-range"><input type="range" min="0" max="50" step="5" data-image-ai-sharpen><output data-image-ai-sharpen-value></output></span></label>' +
              '<label><span>' + escapeHtml(t('计算单元')) + '</span><select data-image-ai-compute><option value="auto">' + escapeHtml(t('自动（CPU/GPU/ANE）')) + '</option><option value="ane">' + escapeHtml(t('Neural Engine优先')) + '</option><option value="gpu">' + escapeHtml(t('GPU优先')) + '</option><option value="cpu">' + escapeHtml(t('仅CPU（对照）')) + '</option></select></label>' +
              '<label><span>Tile</span><select data-image-ai-tile><option value="0">' + escapeHtml(t('自动')) + '</option><option value="128">128</option><option value="256">256</option><option value="512">512</option></select></label>' +
              '<label><span>' + escapeHtml(t('Tile重叠')) + '</span><select data-image-ai-overlap><option value="low">' + escapeHtml(t('较少（更快）')) + '</option><option value="balanced">' + escapeHtml(t('均衡')) + '</option><option value="quality">' + escapeHtml(t('较多（减少接缝）')) + '</option></select></label>' +
              '<p>' + escapeHtml(t('CodeFormer使用官方独立运行环境；未安装时选择该选项会给出配置提示。')) + '</p>' +
              '<p>' + escapeHtml(t('AI会推测并生成纹理细节，结果不一定与原始真实内容完全一致。')) + '</p>' +
            '</div><footer><button type="button" data-image-ai-cancel>' + escapeHtml(t('取消')) + '</button><button type="button" class="primary" data-image-ai-save>' + escapeHtml(t('保存设置')) + '</button></footer>' +
          '</section>';
          document.body.appendChild(settingsDialog);
          const model = settingsDialog.querySelector('[data-image-ai-model]');
          const scale = settingsDialog.querySelector('[data-image-ai-scale]');
          const restormerRow = settingsDialog.querySelector('[data-image-restormer-row]');
          const restormerMode = settingsDialog.querySelector('[data-image-restormer-mode]');
          const restormerStrengthRow = settingsDialog.querySelector('[data-image-restormer-strength-row]');
          const restormerStrength = settingsDialog.querySelector('[data-image-restormer-strength]');
          const restormerStrengthValue = settingsDialog.querySelector('[data-image-restormer-strength-value]');
          const faceRestoration = settingsDialog.querySelector('[data-image-face-restoration]');
          const codeformerModeRow = settingsDialog.querySelector('[data-image-codeformer-mode-row]');
          const codeformerMode = settingsDialog.querySelector('[data-image-codeformer-mode]');
          const faceFidelityRow = settingsDialog.querySelector('[data-image-face-fidelity-row]');
          const faceFidelity = settingsDialog.querySelector('[data-image-face-fidelity]');
          const faceFidelityValue = settingsDialog.querySelector('[data-image-face-fidelity-value]');
          const faceCenterRow = settingsDialog.querySelector('[data-image-face-center-row]');
          const faceOnlyCenter = settingsDialog.querySelector('[data-image-face-only-center]');
          const denoise = settingsDialog.querySelector('[data-image-ai-denoise]');
          const sharpen = settingsDialog.querySelector('[data-image-ai-sharpen]');
          const sharpenValue = settingsDialog.querySelector('[data-image-ai-sharpen-value]');
          const compute = settingsDialog.querySelector('[data-image-ai-compute]');
          const tile = settingsDialog.querySelector('[data-image-ai-tile]');
          const overlap = settingsDialog.querySelector('[data-image-ai-overlap]');
          model.value = aiSettings.model;
          scale.value = String(aiSettings.scale);
          restormerMode.value = aiSettings.restormerMode;
          restormerStrength.value = String(aiSettings.restormerStrength);
          faceRestoration.value = aiSettings.faceRestoration;
          codeformerMode.value = aiSettings.codeformerMode;
          faceFidelity.value = String(aiSettings.faceFidelity);
          faceOnlyCenter.checked = aiSettings.faceOnlyCenter;
          denoise.value = String(aiSettings.denoise);
          sharpen.value = String(aiSettings.sharpen);
          compute.value = aiSettings.computeUnits;
          tile.value = String(aiSettings.tile);
          overlap.value = aiSettings.overlap;
          const syncModel = function () {
            const coreml = model.value.indexOf('coreml-') === 0;
            if (model.value === 'coreml-x2plus') scale.value = '2';
            else if (coreml || model.value === 'realesrgan-x4plus') scale.value = '4';
            scale.disabled = coreml || model.value === 'realesrgan-x4plus';
            compute.disabled = !coreml;
          };
          restormerRow.hidden = select.value !== 'deblur_ai';
          restormerStrengthRow.hidden = select.value !== 'deblur_ai';
          const syncRestormerStrength = function () { restormerStrengthValue.textContent = restormerStrength.value + '%'; };
          const syncFaceRestoration = function () {
            const codeformer = faceRestoration.value === 'codeformer';
            const inpaint = codeformerMode.value === 'inpaint';
            codeformerModeRow.hidden = !codeformer;
            faceFidelityRow.hidden = !codeformer || inpaint;
            faceCenterRow.hidden = !codeformer || inpaint;
            faceFidelityValue.textContent = faceFidelity.value + '%';
          };
          const syncSharpen = function () { sharpenValue.textContent = sharpen.value + '%'; };
          model.addEventListener('change', syncModel);
          sharpen.addEventListener('input', syncSharpen);
          restormerStrength.addEventListener('input', syncRestormerStrength);
          faceRestoration.addEventListener('change', syncFaceRestoration);
          codeformerMode.addEventListener('change', syncFaceRestoration);
          faceFidelity.addEventListener('input', syncFaceRestoration);
          syncModel();
          syncRestormerStrength();
          syncFaceRestoration();
          syncSharpen();
          const close = function () { settingsDialog.remove(); };
          settingsDialog.querySelector('[data-image-ai-close]').addEventListener('click', close);
          settingsDialog.querySelector('[data-image-ai-cancel]').addEventListener('click', close);
          settingsDialog.addEventListener('click', function (event) { if (event.target === settingsDialog) close(); });
          settingsDialog.querySelector('[data-image-ai-save]').addEventListener('click', function () {
            aiSettings.model = model.value;
            aiSettings.scale = Number(scale.value) || 2;
            aiSettings.restormerMode = restormerMode.value;
            aiSettings.restormerStrength = Number(restormerStrength.value) || 0;
            aiSettings.faceRestoration = faceRestoration.value;
            aiSettings.codeformerMode = codeformerMode.value;
            aiSettings.faceFidelity = Number(faceFidelity.value) || 0;
            aiSettings.faceOnlyCenter = faceOnlyCenter.checked;
            aiSettings.denoise = Number(denoise.value) || 0;
            aiSettings.sharpen = Number(sharpen.value) || 0;
            aiSettings.computeUnits = compute.value;
            aiSettings.tile = Number(tile.value) || 0;
            aiSettings.overlap = overlap.value;
            close();
          });
        };
        select.addEventListener('change', function () {
          const ai = select.value === 'ai' || select.value === 'deblur_ai';
          const redEye = select.value === 'red_eye';
          const brightness = select.value === 'brightness';
          const codeformer = select.value === 'codeformer';
          settingsButton.hidden = !ai && !redEye && !brightness && !codeformer;
          if (ai) openAiSettings();
          else if (redEye) openRedEyeSettings();
          else if (brightness) openBrightnessSettings();
          else if (codeformer) openCodeformerSettings();
        });
        settingsButton.addEventListener('click', function () {
          if (select.value === 'red_eye') openRedEyeSettings();
          else if (select.value === 'brightness') openBrightnessSettings();
          else if (select.value === 'codeformer') openCodeformerSettings();
          else openAiSettings();
        });
        settingsButton.hidden = select.value !== 'ai' && select.value !== 'deblur_ai'
          && select.value !== 'red_eye' && select.value !== 'brightness' && select.value !== 'codeformer';
        const setProgress = function (percent, text, state) {
          const value = Math.max(0, Math.min(100, Math.round(Number(percent) || 0)));
          status.hidden = false;
          status.setAttribute('data-state', state || 'running');
          progress.style.width = value + '%';
          message.textContent = value + '% · ' + String(text || '');
        };
        button.addEventListener('click', async function () {
          const method = select.value;
          button.disabled = true;
          select.disabled = true;
          setProgress(0, method === 'codeformer' ? t('正在启动CodeFormer人脸重建') : (method === 'brightness' ? t('正在启动亮度调整')
            : (method === 'red_eye' ? t('正在启动自动去红眼') : (method === 'deblur_ai' ? t('正在启动去模糊并超分')
            : (method === 'ai' ? t('正在启动AI超分辨率') : t('正在启动图片锐化'))))), 'running');
          try {
            let startUrl = (local ? api.localDiskImageEnhance : api.imageEnhance)
              + '?' + (local ? 'path=' : 'file=') + encodeURIComponent(path)
              + '&method=' + encodeURIComponent(method);
            if (method === 'ai' || method === 'deblur_ai') {
              startUrl += '&ai_model=' + encodeURIComponent(aiSettings.model)
                + '&ai_scale=' + encodeURIComponent(String(aiSettings.scale))
                + '&ai_denoise=' + encodeURIComponent(String(aiSettings.denoise))
                + '&ai_sharpen=' + encodeURIComponent(String(aiSettings.sharpen))
                + '&ai_compute_units=' + encodeURIComponent(aiSettings.computeUnits)
                + '&ai_tile=' + encodeURIComponent(String(aiSettings.tile))
                + '&ai_overlap=' + encodeURIComponent(aiSettings.overlap)
                + '&face_restoration=' + encodeURIComponent(aiSettings.faceRestoration)
                + '&codeformer_aligned=' + encodeURIComponent(aiSettings.codeformerMode === 'inpaint' ? '1' : '0')
                + '&face_fidelity=' + encodeURIComponent(String(aiSettings.faceFidelity))
                + '&face_only_center=' + encodeURIComponent(aiSettings.faceOnlyCenter ? '1' : '0');
              if (method === 'deblur_ai') {
                startUrl += '&restormer_mode=' + encodeURIComponent(aiSettings.restormerMode)
                  + '&restormer_strength=' + encodeURIComponent(String(aiSettings.restormerStrength));
              }
            } else if (method === 'red_eye') {
              startUrl += '&red_eye_strength=' + encodeURIComponent(String(redEyeSettings.strength))
                + '&red_eye_only_center=' + encodeURIComponent(redEyeSettings.onlyCenterFace ? '1' : '0');
            } else if (method === 'brightness') {
              startUrl += '&brightness=' + encodeURIComponent(String(brightnessSettings.amount));
            } else if (method === 'codeformer') {
              startUrl += '&codeformer_aligned=' + encodeURIComponent(codeformerSettings.mode === 'aligned' ? '1' : '0')
                + '&face_fidelity=' + encodeURIComponent(String(codeformerSettings.fidelity))
                + '&face_only_center=' + encodeURIComponent(codeformerSettings.onlyCenterFace ? '1' : '0')
                + '&codeformer_upscale=' + encodeURIComponent(codeformerSettings.upscale ? '1' : '0');
            }
            if (local) {
              startUrl = appendLocalDirPassword(appendFilePassword(startUrl, path, true), localDiskParentPath(path));
            } else {
              startUrl = appendFilePassword(withFolderPassword(startUrl, parentFolderPathFromFilePath(path)), path, false);
            }
            const started = await fetchJson(startUrl, { method: 'POST' });
            const taskId = String(started.task_id || '');
            if (!taskId) throw new Error(t('无法启动图片转换'));
            const progressBase = local ? api.localDiskImageEnhanceProgress : api.imageEnhanceProgress;
            const poll = async function () {
              let progressUrl = progressBase + '?task_id=' + encodeURIComponent(taskId);
              if (local) progressUrl = appendLocalDirPassword(progressUrl, localDiskParentPath(path));
              const task = await fetchJson(progressUrl);
              setProgress(task.progress, task.message || t('正在转换图片'), task.done ? (task.success ? 'success' : 'failed') : 'running');
              if (!task.done) {
                window.setTimeout(function () {
                  poll().catch(function (err) {
                    setProgress(0, t('图片转换失败：') + err.message, 'failed');
                    button.disabled = false;
                    select.disabled = false;
                  });
                }, 700);
                return;
              }
              button.disabled = false;
              select.disabled = false;
              if (!task.success) throw new Error(task.error || task.message || t('图片转换失败'));
              setProgress(100, task.message || (t('图片转换完成：') + task.name), 'success');
              if (local) await loadLocalDisk(activeLocalDiskPath || localDiskParentPath(path) || '');
              else await loadFiles();
            };
            await poll();
          } catch (err) {
            button.disabled = false;
            select.disabled = false;
            setProgress(0, t('图片转换失败：') + (err && err.message ? err.message : err), 'failed');
          }
        });
      }

      function openImageEnhanceDialog(path, local, initialMethod) {
        const filePath = String(path || '');
        if (!filePath) return;
        const oldDialog = document.getElementById('image-enhance-dialog');
        if (oldDialog) oldDialog.remove();
        const dialog = document.createElement('div');
        dialog.className = 'tag-dialog image-enhance-dialog';
        dialog.id = 'image-enhance-dialog';
        dialog.innerHTML =
          '<div class="tag-dialog-backdrop" data-image-enhance-close></div>' +
          '<div class="tag-dialog-card image-enhance-card" role="dialog" aria-modal="true" aria-labelledby="image-enhance-title">' +
            '<div class="tag-dialog-head image-enhance-head">' +
              '<div><h2 id="image-enhance-title">' + escapeHtml(t('图片编辑')) + '</h2><p>' + escapeHtml(filePath) + '</p></div>' +
              '<button type="button" class="image-enhance-close" data-image-enhance-close aria-label="' + escapeHtml(t('关闭')) + '">×</button>' +
            '</div>' +
            '<div class="image-enhance-options">' +
              '<label class="file-summary-enhance-control">' +
                '<span>' + escapeHtml(t('处理方式')) + '</span>' +
                '<select data-file-summary-enhance aria-label="' + escapeHtml(t('处理方式')) + '">' +
                  '<option value="brightness">' + escapeHtml(t('亮度调整')) + '</option>' +
                  '<option value="codeformer">' + escapeHtml(t('CodeFormer人脸重建')) + '</option>' +
                  '<option value="sharpen">' + escapeHtml(t('锐化')) + '</option>' +
                  '<option value="ai">' + escapeHtml(t('AI超分辨率')) + '</option>' +
                  '<option value="deblur_ai">' + escapeHtml(t('去模糊后AI超分')) + '</option>' +
                  '<option value="red_eye">' + escapeHtml(t('自动去红眼')) + '</option>' +
                '</select>' +
                '<button type="button" class="file-summary-ai-settings" data-file-summary-ai-settings hidden>' + escapeHtml(t('详细设置')) + '</button>' +
                '<button type="button" data-file-summary-enhance-convert>' + escapeHtml(t('转换')) + '</button>' +
              '</label>' +
              '<div class="file-summary-enhance-status" data-file-summary-enhance-status hidden>' +
                '<span class="file-summary-enhance-progress"><i data-file-summary-enhance-progress></i></span>' +
                '<span data-file-summary-enhance-message></span>' +
              '</div>' +
              '<p class="image-enhance-hint">' + escapeHtml(t('转换会生成新图片，不会覆盖原图。')) + '</p>' +
            '</div>' +
            '<div class="tag-dialog-actions"><button type="button" class="tag-dialog-btn secondary" data-image-enhance-close>' + escapeHtml(t('关闭')) + '</button></div>' +
          '</div>';
        document.body.appendChild(dialog);
        bindFileSummaryEnhance(dialog, filePath, !!local, initialMethod);
        dialog.querySelectorAll('[data-image-enhance-close]').forEach(function (node) {
          node.addEventListener('click', function () { dialog.remove(); });
        });
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
        const isImage = !file.directory && isImageName(name);
        const resolutionText = isImage ? summaryImageResolutionFromRecord(file) : '';
        const rows = [
          [t('文件名'), name],
          [t('文件大小'), sizeText],
          [t('文件类型'), inferFileTypeLabel(file)],
          ...(isImage ? [[t('分辨率'), resolutionText || t('读取中…'), 'resolution']] : []),
          [t('创建时间'), createdText],
          [t('修改时间'), modifiedText]
        ];
        const dialog = document.createElement('div');
        dialog.className = 'tag-dialog file-summary-dialog';
        dialog.id = 'file-summary-dialog';
        dialog.innerHTML =
          '<div class="tag-dialog-backdrop" data-file-summary-close="1"></div>' +
          '<div class="tag-dialog-card file-summary-card" role="dialog" aria-modal="true" aria-labelledby="file-summary-title">' +
            fileSummaryHeaderHtml(path) +
            '<dl class="file-summary-list">' + rows.map(function (row) {
              if (row[2] === 'resolution') {
                return fileSummaryResolutionHtml(row[0], row[1]);
              }
              return '<div class="file-summary-row"><dt>' + escapeHtml(row[0]) + '</dt><dd>' + escapeHtml(row[1]) + '</dd></div>';
            }).join('') + '</dl>' +
            '<div class="tag-dialog-actions">' +
              '<button type="button" class="tag-dialog-btn" data-file-summary-close="1">' + escapeHtml(t('确定')) + '</button>' +
            '</div>' +
          '</div>';
        document.body.appendChild(dialog);
        bindFileSummaryWindow(dialog);
        if (isImage && !resolutionText) {
          const imageUrl = file.local ? localDiskDownloadUrl(path) : downloadUrlForFile(path, true);
          loadSummaryImageResolution(dialog, imageUrl);
        }
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
        const isLocalDisk = name === 'local-disk';
        if (adminStorageTab) {
          adminStorageTab.classList.toggle('active', !isLocalDisk);
        }
        if (adminLocalDiskTab) {
          adminLocalDiskTab.classList.toggle('active', isLocalDisk);
        }
        if (adminStorageView) {
          adminStorageView.hidden = isLocalDisk;
        }
        if (adminLocalDiskView) {
          adminLocalDiskView.hidden = !isLocalDisk;
        }
        if (isLocalDisk) {
          loadAdminLocalDiskSettings();
        } else {
          loadAdminStoragePath();
        }
      }

      function activateAccountView(name) {
        const isPassword = name === 'password';
        const isLanguage = name === 'language';
        const isFontSize = name === 'font-size';
        if (accountPasswordTab) {
          accountPasswordTab.classList.toggle('active', isPassword);
        }
        if (accountLanguageTab) {
          accountLanguageTab.classList.toggle('active', isLanguage);
        }
        if (accountFontSizeTab) {
          accountFontSizeTab.classList.toggle('active', isFontSize);
        }
        if (accountPasswordView) {
          accountPasswordView.hidden = !isPassword;
        }
        if (accountLanguageView) {
          accountLanguageView.hidden = !isLanguage;
        }
        if (accountFontSizeView) {
          accountFontSizeView.hidden = !isFontSize;
        }
        if (isFontSize) {
          syncFontSizePreferenceForCurrentUser();
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
            const confirmedBatch = await askDeleteConfirmDialog({
              title: t('删除目录'),
              description: t('选中的本地目录将移至系统回收站。'),
              highlight: actionPaths.length + t(' 个目录'),
              confirmText: t('移除')
            });
            if (!confirmedBatch) {
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
          const confirmedDir = await askDeleteConfirmDialog({
            title: t('删除目录'),
            description: t('目录将移至系统回收站。'),
            highlight: dirPath,
            confirmText: t('移除')
          });
          if (!confirmedDir) {
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
          currentAdminStorageBackupPaths = Array.isArray(data.backup_paths)
            ? data.backup_paths.map(function (item) {
              const path = String((item && item.path) || item || '');
              return path ? { path: path, enabled: !item || item.enabled !== false } : null;
            }).filter(Boolean)
            : (data.backup_path ? [{ path: String(data.backup_path || ''), enabled: true }] : []);
          currentAdminStorageBackupPath = currentAdminStorageBackupPaths.length ? currentAdminStorageBackupPaths[0].path : '';
          currentAdminStorageUploadAutoSync = data.backup_upload_auto_sync !== false;
          adminStoragePath.value = currentAdminStoragePath;
          if (adminStorageBackupPath) {
            adminStorageBackupPath.value = '';
          }
          renderAdminStorageBackupList();
          renderAdminStorageUploadAutoSync();
        } catch (err) {
          showStatus(t('加载存储路径失败：') + err.message, 'err');
        }
      }

      async function loadAdminLocalDiskSettings() {
        if (!adminLocalDiskAdminCheckbox || !adminLocalDiskUserCheckbox) {
          return;
        }
        try {
          const data = await fetchJson(api.adminLocalDiskSettings);
          adminLocalDiskAdminCheckbox.checked = data.local_disk_admin !== false;
          adminLocalDiskUserCheckbox.checked = data.local_disk_user !== false;
        } catch (err) {
          showStatus(t('加载本地磁盘访问设置失败：') + err.message, 'err');
        }
      }

      async function saveAdminLocalDiskSettings() {
        if (!adminLocalDiskAdminCheckbox || !adminLocalDiskUserCheckbox) {
          return;
        }
        if (adminLocalDiskSettingsSubmit) {
          adminLocalDiskSettingsSubmit.disabled = true;
        }
        try {
          const url = api.adminLocalDiskSettings
            + '?local_disk_admin=' + encodeURIComponent(adminLocalDiskAdminCheckbox.checked ? '1' : '0')
            + '&local_disk_user=' + encodeURIComponent(adminLocalDiskUserCheckbox.checked ? '1' : '0');
          const data = await fetchJson(url, { method: 'POST' });
          adminLocalDiskAdminCheckbox.checked = data.local_disk_admin !== false;
          adminLocalDiskUserCheckbox.checked = data.local_disk_user !== false;
          await refreshAuthStatus();
          showStatus(t('本地磁盘访问设置已保存'), 'ok');
        } catch (err) {
          showStatus(t('保存本地磁盘访问设置失败：') + err.message, 'err');
        } finally {
          if (adminLocalDiskSettingsSubmit) {
            adminLocalDiskSettingsSubmit.disabled = false;
          }
        }
      }

// Event wiring and application bootstrap. Loaded last.

      loadUnlockedFolderPasswords();

      loadUnlockedFilePasswords();

      redirectToSavedLanguageIfNeeded();

      if (window.WebCoolI18n && typeof window.WebCoolI18n.apply === 'function') {
        window.WebCoolI18n.apply(document);
      }

      async function runUploadFormData(formData, options) {
        const opts = options || {};
        activeUploadPaused = false;
        activeUploadCancelled = false;
        activeUploadFormData = formData;
        const formFolder = formData && typeof formData.get === 'function'
          ? formData.get('folder')
          : '';
        const uploadFolder = typeof formFolder === 'string' ? formFolder : activeFolderPath;
        const uploadPassword = getFolderPasswordForPath(uploadFolder);
        if (uploadPassword) {
          formData.set('folder_password', uploadPassword);
        }
        setUploadProgressControls('running');
        try {
          const data = await uploadWithProgress(formData);
          activeUploadRunning = false;
          activeUploadXhr = null;
          activeUploadFormData = null;
          setUploadProgressControls('idle');
          setUploadProgress(100, t('上传完成 100%'));
          showStatus(t('上传完成：成功保存 ') + (data.count || 0) + t(' 个文件'), 'ok');
          if (!opts.keepFormValues) {
            uploadForm.reset();
            updateUploadSelectedSummary([]);
          }
          await loadFiles();

          const videoProbeFails = await verifyUploadedVideos(data.files);
          if (videoProbeFails.length) {
            await promptUploadedTranscodes(videoProbeFails);
          }

          setTimeout(hideUploadProgress, 700);
        } catch (err) {
          activeUploadRunning = false;
          activeUploadXhr = null;
          if (activeUploadPaused) {
            setUploadProgressControls('paused');
            showStatus(t('上传已暂停'), 'warn');
            return;
          }
          if (activeUploadCancelled) {
            activeUploadCancelled = false;
            return;
          }
          activeUploadFormData = null;
          setUploadProgressControls('idle');
          setUploadProgress(0, t('上传失败'));
          showStatus(t('上传失败：') + err.message, 'err');
          setTimeout(hideUploadProgress, 900);
        }
      }

      const UPLOAD_MAX_FILE_COUNT = 50;
      const uploadFilesInput = document.getElementById('upload-files-input');
      const uploadDropZone = document.getElementById('upload-drop-zone');
      const uploadSelectedSummary = document.getElementById('upload-selected-summary');
      const uploadSelectedList = document.getElementById('upload-selected-list');
      const uploadClearSelection = document.getElementById('upload-clear-selection');

      function getUploadFilesFromList(fileList) {
        return Array.prototype.slice.call(fileList || []).filter(function (file) {
          return file && file.name;
        });
      }

      function updateUploadSelectedSummary(files) {
        if (!uploadSelectedSummary) {
          return;
        }
        const list = Array.isArray(files) ? files : [];
        if (uploadDropZone) {
          uploadDropZone.classList.toggle('has-files', list.length > 0);
        }
        if (!list.length) {
          uploadSelectedSummary.textContent = t('尚未选择文件');
          if (uploadClearSelection) {
            uploadClearSelection.hidden = true;
          }
          if (uploadSelectedList) {
            uploadSelectedList.hidden = true;
            uploadSelectedList.innerHTML = '';
          }
          return;
        }
        uploadSelectedSummary.textContent = t('已选择 ') + list.length + t(' 个文件');
        if (uploadClearSelection) {
          uploadClearSelection.hidden = false;
        }
        if (uploadSelectedList) {
          uploadSelectedList.hidden = false;
          uploadSelectedList.innerHTML = list.map(function (file, index) {
            const size = Number(file.size || 0);
            return '<div class="upload-selected-file">' +
              '<span class="upload-selected-file-name" title="' + escapeHtml(file.name) + '">' + escapeHtml(file.name) + '</span>' +
              '<span class="upload-selected-file-size">' + formatNumber(size) + t(' 字节') + '</span>' +
              '<button type="button" class="upload-selected-remove" data-upload-remove-index="' + index + '" title="' + escapeHtml(t('删除')) + '" aria-label="' + escapeHtml(t('删除')) + '">-</button>' +
            '</div>';
          }).join('');
        }
      }

      function validateUploadFiles(files) {
        const list = Array.isArray(files) ? files : [];
        if (!list.length) {
          showStatus(t('请先选择要上传的文件'), 'err');
          return false;
        }
        if (list.length > UPLOAD_MAX_FILE_COUNT) {
          showStatus(t('一次最多上传 50 个文件'), 'err');
          return false;
        }
        return true;
      }

      function buildUploadFormData(files, folderPath) {
        const formData = new FormData();
        const folder = typeof folderPath === 'string'
          ? folderPath
          : (uploadFolderPathInput ? String(uploadFolderPathInput.value || '') : '');
        formData.set('folder', folder);
        files.forEach(function (file) {
          formData.append('file', file, file.name);
        });
        return formData;
      }

      function browserUploadFileMatches(a, b) {
        return a && b
          && a.name === b.name
          && a.size === b.size
          && a.lastModified === b.lastModified;
      }

      function setPendingBrowserUploadFiles(files) {
        const list = getUploadFilesFromList(files);
        pendingBrowserUploadFiles = list;
        updateUploadSelectedSummary(list);
        return list;
      }

      function appendPendingBrowserUploadFiles(files) {
        const incoming = getUploadFilesFromList(files);
        const list = Array.isArray(pendingBrowserUploadFiles)
          ? pendingBrowserUploadFiles.slice()
          : [];
        let added = 0;
        let skipped = 0;
        incoming.forEach(function (file) {
          if (list.length >= UPLOAD_MAX_FILE_COUNT) {
            skipped += 1;
            return;
          }
          const duplicated = list.some(function (item) {
            return browserUploadFileMatches(item, file);
          });
          if (duplicated) {
            skipped += 1;
            return;
          }
          list.push(file);
          added += 1;
        });
        pendingBrowserUploadFiles = list;
        updateUploadSelectedSummary(list);
        return { added: added, skipped: skipped, list: list };
      }

      function clearPendingBrowserUploadFiles() {
        pendingBrowserUploadFiles = [];
        updateUploadSelectedSummary([]);
        if (uploadFilesInput) {
          uploadFilesInput.value = '';
        }
      }

      function removePendingBrowserUploadFile(index) {
        const list = Array.isArray(pendingBrowserUploadFiles)
          ? pendingBrowserUploadFiles.slice()
          : [];
        if (index < 0 || index >= list.length) {
          return;
        }
        list.splice(index, 1);
        pendingBrowserUploadFiles = list;
        updateUploadSelectedSummary(list);
        if (!list.length && uploadFilesInput) {
          uploadFilesInput.value = '';
        }
      }

      async function openBrowserUploadTargetDialog(files) {
        const list = setPendingBrowserUploadFiles(files);
        if (!validateUploadFiles(list)) {
          return;
        }
        try {
          await loadFolderTreeState();
        } catch (err) {
          showStatus(t('加载远程目录失败：') + err.message, 'err');
          return;
        }
        localImportOverridePaths = null;
        localImportTargetFolderPath = activeFolderPath || '';
        ensureLocalImportFolderPathExpanded(localImportTargetFolderPath);
        const title = document.getElementById('local-import-title');
        const desc = document.getElementById('local-import-desc');
        if (title) {
          title.textContent = t('选择上传目标目录');
        }
        if (desc) {
          desc.textContent = t('请选择这些文件要上传到虚拟磁盘中的哪个目录。');
        }
        renderLocalImportTree();
        if (localImportDialog) {
          localImportDialog.hidden = false;
        }
      }

      async function confirmBrowserUploadFromLocalImportDialog() {
        const list = Array.isArray(pendingBrowserUploadFiles) ? pendingBrowserUploadFiles.slice() : [];
        if (!validateUploadFiles(list)) {
          return;
        }
        if (!(await ensureFolderUnlocked(localImportTargetFolderPath))) {
          return;
        }
        if (localImportConfirmBtn) {
          localImportConfirmBtn.disabled = true;
        }
        try {
          closeLocalImportDialog();
          await uploadBrowserFilesToRemoteFolder(list, localImportTargetFolderPath);
          clearPendingBrowserUploadFiles();
        } finally {
          if (localImportConfirmBtn) {
            localImportConfirmBtn.disabled = false;
          }
        }
      }

      async function uploadBrowserFilesToRemoteFolder(files, targetFolder) {
        const list = getUploadFilesFromList(files);
        if (!validateUploadFiles(list)) {
          return;
        }
        const folder = String(targetFolder || '');
        if (!canUploadLocalDiskToRemoteFolder(folder)) {
          showStatus(t('不能上传到此目录'), 'err');
          return;
        }
        if (!(await ensureFolderUnlocked(folder))) {
          return;
        }
        const folderPassword = getFolderPasswordForPath(folder);
        const formData = buildUploadFormData(list, folder);
        if (folderPassword) {
          formData.set('folder_password', folderPassword);
        }
        if (uploadFolderPathInput) {
          uploadFolderPathInput.value = folder;
        }
        resetStatus();
        setUploadProgress(0, t('开始上传...'));
        await runUploadFormData(formData, { keepFormValues: true });
      }

      function getRemoteUploadStagingCount() {
        return pendingRemoteUploadBrowserFiles.length + pendingRemoteUploadLocalPaths.length;
      }

      function clearRemoteUploadStaging() {
        pendingRemoteUploadBrowserFiles = [];
        pendingRemoteUploadLocalPaths = [];
        renderRemoteUploadStagingPanel();
      }

      function removeRemoteUploadStagingItem(kind, index) {
        const idx = Number(index);
        if (!Number.isFinite(idx) || idx < 0) {
          return;
        }
        if (kind === 'browser') {
          if (idx >= pendingRemoteUploadBrowserFiles.length) {
            return;
          }
          pendingRemoteUploadBrowserFiles.splice(idx, 1);
        } else if (kind === 'local') {
          if (idx >= pendingRemoteUploadLocalPaths.length) {
            return;
          }
          pendingRemoteUploadLocalPaths.splice(idx, 1);
        }
        renderRemoteUploadStagingPanel();
      }

      function addBrowserFilesToRemoteUploadStaging(files, targetFolder) {
        pendingRemoteUploadTargetFolder = String(targetFolder || '');
        const list = getUploadFilesFromList(files);
        let added = 0;
        let skipped = 0;
        list.forEach(function (file) {
          if (getRemoteUploadStagingCount() >= UPLOAD_MAX_FILE_COUNT) {
            skipped += 1;
            return;
          }
          const duplicated = pendingRemoteUploadBrowserFiles.some(function (item) {
            return item.name === file.name
              && item.size === file.size
              && item.lastModified === file.lastModified;
          });
          if (duplicated) {
            skipped += 1;
            return;
          }
          pendingRemoteUploadBrowserFiles.push(file);
          added += 1;
        });
        return { added: added, skipped: skipped };
      }

      function addLocalPathsToRemoteUploadStaging(paths, targetFolder) {
        pendingRemoteUploadTargetFolder = String(targetFolder || '');
        let added = 0;
        let skipped = 0;
        (Array.isArray(paths) ? paths : []).forEach(function (path) {
          const clean = String(path || '');
          if (!clean) {
            return;
          }
          if (getRemoteUploadStagingCount() >= UPLOAD_MAX_FILE_COUNT) {
            skipped += 1;
            return;
          }
          if (pendingRemoteUploadLocalPaths.indexOf(clean) >= 0) {
            skipped += 1;
            return;
          }
          pendingRemoteUploadLocalPaths.push(clean);
          added += 1;
        });
        return { added: added, skipped: skipped };
      }

      function renderRemoteUploadStagingPanel() {
        const count = getRemoteUploadStagingCount();
        const hasItems = count > 0;
        if (remoteBrowserUploadZone) {
          remoteBrowserUploadZone.classList.toggle('has-files', hasItems);
        }
        if (remoteUploadStagingSummary) {
          remoteUploadStagingSummary.hidden = !hasItems;
          if (!hasItems) {
            remoteUploadStagingSummary.textContent = '';
          } else {
            remoteUploadStagingSummary.textContent = t('待上传 ') + count + t(' 项')
              + ' · ' + t('目标：') + getFolderLabel(pendingRemoteUploadTargetFolder);
          }
        }
        if (remoteUploadStagingActions) {
          remoteUploadStagingActions.hidden = !hasItems;
        }
        if (remoteUploadStagingList) {
          if (!hasItems) {
            remoteUploadStagingList.hidden = true;
            remoteUploadStagingList.innerHTML = '';
          } else {
            remoteUploadStagingList.hidden = false;
            const rows = [];
            pendingRemoteUploadBrowserFiles.forEach(function (file, index) {
              rows.push(
                '<div class="upload-selected-file">' +
                  '<span class="upload-selected-file-name" title="' + escapeHtml(file.name) + '">' + escapeHtml(file.name) + '</span>' +
                  '<span class="upload-selected-file-size">' + formatNumber(Number(file.size || 0)) + t(' 字节') + '</span>' +
                  '<button type="button" class="upload-selected-remove" data-staging-remove-kind="browser" data-staging-remove-index="' + index + '" title="' + escapeHtml(t('删除')) + '" aria-label="' + escapeHtml(t('删除')) + '">-</button>' +
                '</div>'
              );
            });
            pendingRemoteUploadLocalPaths.forEach(function (path, index) {
              const name = path.split('/').filter(Boolean).pop() || path;
              rows.push(
                '<div class="upload-selected-file">' +
                  '<span class="upload-selected-file-name" title="' + escapeHtml(path) + '">' + escapeHtml(name) + '</span>' +
                  '<span class="upload-selected-file-size">' + escapeHtml(t('本地磁盘')) + '</span>' +
                  '<button type="button" class="upload-selected-remove" data-staging-remove-kind="local" data-staging-remove-index="' + index + '" title="' + escapeHtml(t('删除')) + '" aria-label="' + escapeHtml(t('删除')) + '">-</button>' +
                '</div>'
              );
            });
            remoteUploadStagingList.innerHTML = rows.join('');
          }
        }
        updateRemoteUploadTargetHints();
      }

      async function uploadRemoteUploadStaging() {
        const browserFiles = pendingRemoteUploadBrowserFiles.slice();
        const localPaths = pendingRemoteUploadLocalPaths.slice();
        if (!browserFiles.length && !localPaths.length) {
          return;
        }
        const folder = String(pendingRemoteUploadTargetFolder || '');
        if (!canUploadLocalDiskToRemoteFolder(folder)) {
          showStatus(t('不能上传到此目录'), 'err');
          return;
        }
        if (!(await ensureFolderUnlocked(folder))) {
          return;
        }
        try {
          if (browserFiles.length) {
            await uploadBrowserFilesToRemoteFolder(browserFiles, folder);
            pendingRemoteUploadBrowserFiles = [];
          }
          if (localPaths.length) {
            await uploadLocalDiskPathsToRemoteFolder(localPaths, folder);
            pendingRemoteUploadLocalPaths = [];
          }
          renderRemoteUploadStagingPanel();
        } catch (err) {
          if (browserFiles.length && !localPaths.length) {
            showStatus(t('上传失败：') + err.message, 'err');
          } else if (!browserFiles.length && localPaths.length) {
            failLocalImportProgress(t('上传失败：') + err.message);
            showStatus(t('上传本地文件失败：') + err.message, 'err');
          } else {
            showStatus(t('上传失败：') + err.message, 'err');
          }
        }
      }

      async function queueRemoteUploadDrop(e, targetFolder, options) {
        const opts = options || {};
        if (typeof opts.beforeQueue === 'function') {
          opts.beforeQueue(e);
        }
        let addedCount = 0;
        let skippedCount = 0;
        if (isBrowserFileDragEvent(e) && e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files.length) {
          const result = addBrowserFilesToRemoteUploadStaging(e.dataTransfer.files, targetFolder);
          addedCount += result.added;
          skippedCount += result.skipped;
        }
        if (isLocalDiskDragEvent(e)) {
          const localPaths = parseLocalDiskDragPaths(e.dataTransfer);
          if (localPaths.length) {
            const result = addLocalPathsToRemoteUploadStaging(localPaths, targetFolder);
            addedCount += result.added;
            skippedCount += result.skipped;
          }
        }
        if (!addedCount) {
          if (skippedCount > 0) {
            showStatus(t('待上传队列已满或文件已存在，最多 ') + UPLOAD_MAX_FILE_COUNT + t(' 项'), 'err');
          }
          return;
        }
        renderRemoteUploadStagingPanel();
        if (skippedCount > 0) {
          showStatus(t('已添加 ') + addedCount + t(' 项，已忽略 ') + skippedCount + t(' 项重复或超出上限'), 'ok');
        }
        const confirmed = await askConfirmDialog({
          title: t('开始上传'),
          description: t('已添加 ') + addedCount + t(' 个待上传项到队列。是否立即开始上传？'),
          confirmText: t('开始上传'),
          cancelText: t('稍后上传'),
          danger: false
        });
        if (confirmed) {
          await uploadRemoteUploadStaging();
        }
      }

      function isBrowserFileDragEvent(e) {
        const dt = e && e.dataTransfer;
        if (!dt || !dt.types) {
          return false;
        }
        const types = dt.types;
        if (typeof types.contains === 'function') {
          return types.contains('Files');
        }
        return Array.prototype.indexOf.call(types, 'Files') >= 0;
      }

      function isRemoteUploadDragEvent(e) {
        return isBrowserFileDragEvent(e) || isLocalDiskDragEvent(e);
      }

      function bindRemoteUploadDropZone(zone, resolveTargetFolder, options) {
        if (!zone || typeof resolveTargetFolder !== 'function') {
          return;
        }
        const opts = options || {};
        ['dragenter', 'dragover'].forEach(function (eventName) {
          zone.addEventListener(eventName, function (e) {
            if (!isRemoteUploadDragEvent(e)) {
              return;
            }
            const targetFolder = resolveTargetFolder(e);
            if (!canUploadLocalDiskToRemoteFolder(targetFolder)) {
              if (e.dataTransfer) {
                e.dataTransfer.dropEffect = 'none';
              }
              return;
            }
            e.preventDefault();
            e.stopPropagation();
            zone.classList.add('is-drag-over');
            if (e.dataTransfer) {
              e.dataTransfer.dropEffect = 'copy';
            }
          });
        });
        zone.addEventListener('dragleave', function (e) {
          if (zone.contains(e.relatedTarget)) {
            return;
          }
          zone.classList.remove('is-drag-over');
        });
        zone.addEventListener('drop', async function (e) {
          zone.classList.remove('is-drag-over');
          if (!isRemoteUploadDragEvent(e)) {
            return;
          }
          const targetFolder = resolveTargetFolder(e);
          if (!canUploadLocalDiskToRemoteFolder(targetFolder)) {
            showStatus(t('请先在左侧目录树中选择目标文件夹'), 'err');
            return;
          }
          e.preventDefault();
          e.stopPropagation();
          try {
            await queueRemoteUploadDrop(e, targetFolder, {
              beforeQueue: opts.beforeQueue || opts.beforeUpload
            });
          } catch (err) {
            showStatus(t('添加待上传文件失败：') + err.message, 'err');
          }
        });
      }

      if (uploadFilesInput) {
        uploadFilesInput.addEventListener('change', function () {
          const result = appendPendingBrowserUploadFiles(uploadFilesInput.files);
          uploadFilesInput.value = '';
          if (!result.added && result.skipped > 0) {
            showStatus(t('待上传队列已满或文件已存在，最多 ') + UPLOAD_MAX_FILE_COUNT + t(' 项'), 'err');
          } else if (result.skipped > 0) {
            showStatus(t('已添加 ') + result.added + t(' 项，已忽略 ') + result.skipped + t(' 项重复或超出上限'), 'ok');
          }
        });
      }

      if (uploadClearSelection) {
        uploadClearSelection.addEventListener('click', function () {
          clearPendingBrowserUploadFiles();
          resetStatus();
        });
      }

      if (uploadSelectedList) {
        uploadSelectedList.addEventListener('click', function (e) {
          const removeBtn = e.target.closest('[data-upload-remove-index]');
          if (!removeBtn) {
            return;
          }
          removePendingBrowserUploadFile(Number(removeBtn.getAttribute('data-upload-remove-index') || -1));
          resetStatus();
        });
      }

      if (uploadDropZone && uploadFilesInput) {
        uploadDropZone.addEventListener('click', function (e) {
          const target = e.target && e.target.closest ? e.target : null;
          if (target && target.closest('input, label, button')) {
            return;
          }
          uploadFilesInput.click();
        });

        ['dragenter', 'dragover'].forEach(function (eventName) {
          uploadDropZone.addEventListener(eventName, function (e) {
            if (!e.dataTransfer || !e.dataTransfer.types || Array.prototype.indexOf.call(e.dataTransfer.types, 'Files') < 0) {
              return;
            }
            e.preventDefault();
            uploadDropZone.classList.add('is-drag-over');
            e.dataTransfer.dropEffect = 'copy';
          });
        });

        ['dragleave', 'drop'].forEach(function (eventName) {
          uploadDropZone.addEventListener(eventName, function (e) {
            if (eventName === 'dragleave' && uploadDropZone.contains(e.relatedTarget)) {
              return;
            }
            uploadDropZone.classList.remove('is-drag-over');
          });
        });

        uploadDropZone.addEventListener('drop', async function (e) {
          if (!e.dataTransfer || !e.dataTransfer.files || !e.dataTransfer.files.length) {
            return;
          }
          e.preventDefault();
          resetStatus();
          const result = appendPendingBrowserUploadFiles(e.dataTransfer.files);
          if (!result.added && result.skipped > 0) {
            showStatus(t('待上传队列已满或文件已存在，最多 ') + UPLOAD_MAX_FILE_COUNT + t(' 项'), 'err');
          } else if (result.skipped > 0) {
            showStatus(t('已添加 ') + result.added + t(' 项，已忽略 ') + result.skipped + t(' 项重复或超出上限'), 'ok');
          }
        });
      }

      function bindLocalDiskRemoteUploadDropZone(zone, resolveTargetFolder, options) {
        bindRemoteUploadDropZone(zone, resolveTargetFolder, options);
      }

      bindRemoteUploadDropZone(localDiskRemoteUploadZone, function () {
        return String(activeFolderPath || '');
      });

      bindRemoteUploadDropZone(remoteBrowserUploadZone, function () {
        return String(activeFolderPath || '');
      });

      if (remoteUploadStagingUploadBtn) {
        remoteUploadStagingUploadBtn.addEventListener('click', function () {
          uploadRemoteUploadStaging().catch(function (err) {
            showStatus(t('上传失败：') + err.message, 'err');
          });
        });
      }

      if (remoteUploadStagingClearBtn) {
        remoteUploadStagingClearBtn.addEventListener('click', function () {
          clearRemoteUploadStaging();
          resetStatus();
        });
      }

      if (remoteUploadStagingList) {
        remoteUploadStagingList.addEventListener('click', function (e) {
          const removeBtn = e.target.closest('[data-staging-remove-kind][data-staging-remove-index]');
          if (!removeBtn) {
            return;
          }
          removeRemoteUploadStagingItem(
            removeBtn.getAttribute('data-staging-remove-kind'),
            removeBtn.getAttribute('data-staging-remove-index')
          );
        });
      }

      updateRemoteUploadTargetHints();

      uploadForm.addEventListener('submit', async function (e) {
        e.preventDefault();
        resetStatus();
        await openBrowserUploadTargetDialog(pendingBrowserUploadFiles);
      });

      if (uploadProgressPauseBtn) {
        uploadProgressPauseBtn.addEventListener('click', pauseActiveUpload);
      }

      if (uploadProgressResumeBtn) {
        uploadProgressResumeBtn.addEventListener('click', function () {
          if (!activeUploadFormData) {
            return;
          }
          resetStatus();
          setUploadProgress(0, t('继续上传...'));
          runUploadFormData(activeUploadFormData, { keepFormValues: true });
        });
      }

      if (uploadProgressCancelBtn) {
        uploadProgressCancelBtn.addEventListener('click', cancelActiveUpload);
      }

      if (localDiskHomeBtn) {
        localDiskHomeBtn.addEventListener('click', function () {
          loadLocalDisk(activeLocalDiskHomePath || '', { resetTreeRoot: true });
        });
      }

      if (localDiskRootBtn) {
        localDiskRootBtn.addEventListener('click', function () {
          loadLocalDisk('/', { resetTreeRoot: true });
        });
      }

      if (localDiskTrashBtn) {
        localDiskTrashBtn.addEventListener('click', function () {
          fetchJson(api.localDiskOpenTrash, { method: 'POST' })
            .then(function () {
              showStatus(t('已打开系统回收站'), 'ok');
            })
            .catch(function (err) {
              showStatus(t('打开系统回收站失败：') + err.message, 'err');
            });
        });
      }

      if (localDiskUpBtn) {
        localDiskUpBtn.addEventListener('click', function () {
          const parent = activeLocalDiskParentPath || '/';
          loadLocalDisk(parent, { resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, parent) });
        });
      }

      if (localDiskExplorer) {
        try {
          const savedDirWidth = Number(window.localStorage.getItem('webcool.localDiskDirWidth') || '0');
          if (savedDirWidth > 0) {
            setLocalDiskDirWidth(savedDirWidth, false);
          }
        } catch (_) {}
      }

      if (localDiskDirResize && localDiskExplorer) {
        localDiskDirResize.addEventListener('pointerdown', function (e) {
          if (window.matchMedia && window.matchMedia('(max-width: 760px)').matches) {
            return;
          }
          const startX = e.clientX;
          const startWidth = localDiskExplorer.querySelector('.local-disk-dir-browser')
            ? localDiskExplorer.querySelector('.local-disk-dir-browser').getBoundingClientRect().width
            : 270;
          localDiskExplorer.classList.add('is-resizing');
          document.body.classList.add('local-disk-dir-resizing');
          if (localDiskDirResize.setPointerCapture) {
            localDiskDirResize.setPointerCapture(e.pointerId);
          }
          const onMove = function (moveEvent) {
            setLocalDiskDirWidth(startWidth + moveEvent.clientX - startX, false);
          };
          const onEnd = function (endEvent) {
            document.removeEventListener('pointermove', onMove);
            document.removeEventListener('pointerup', onEnd);
            document.removeEventListener('pointercancel', onEnd);
            localDiskExplorer.classList.remove('is-resizing');
            document.body.classList.remove('local-disk-dir-resizing');
            if (localDiskDirResize.releasePointerCapture && endEvent && endEvent.pointerId !== undefined) {
              try {
                localDiskDirResize.releasePointerCapture(endEvent.pointerId);
              } catch (_) {}
            }
            const finalWidth = localDiskExplorer.querySelector('.local-disk-dir-browser')
              ? localDiskExplorer.querySelector('.local-disk-dir-browser').getBoundingClientRect().width
              : startWidth;
            setLocalDiskDirWidth(finalWidth, true);
          };
          document.addEventListener('pointermove', onMove);
          document.addEventListener('pointerup', onEnd);
          document.addEventListener('pointercancel', onEnd);
          e.preventDefault();
        });
      }

      if (explorerShell) {
        try {
          const savedFolderWidth = Number(window.localStorage.getItem('webcool.folderBrowserWidth') || '0');
          if (savedFolderWidth > 0) {
            setFolderBrowserWidth(savedFolderWidth, false);
          }
        } catch (_) {}
      }

      if (folderBrowserResize && explorerShell) {
        folderBrowserResize.addEventListener('pointerdown', function (e) {
          if (window.matchMedia && window.matchMedia('(max-width: 760px)').matches) {
            return;
          }
          const startX = e.clientX;
          const startWidth = folderBrowser
            ? folderBrowser.getBoundingClientRect().width
            : 290;
          explorerShell.classList.add('is-resizing');
          document.body.classList.add('folder-browser-resizing');
          if (folderBrowserResize.setPointerCapture) {
            folderBrowserResize.setPointerCapture(e.pointerId);
          }
          const onMove = function (moveEvent) {
            setFolderBrowserWidth(startWidth + moveEvent.clientX - startX, false);
          };
          const onEnd = function (endEvent) {
            document.removeEventListener('pointermove', onMove);
            document.removeEventListener('pointerup', onEnd);
            document.removeEventListener('pointercancel', onEnd);
            explorerShell.classList.remove('is-resizing');
            document.body.classList.remove('folder-browser-resizing');
            if (folderBrowserResize.releasePointerCapture && endEvent && endEvent.pointerId !== undefined) {
              try {
                folderBrowserResize.releasePointerCapture(endEvent.pointerId);
              } catch (_) {}
            }
            const finalWidth = folderBrowser
              ? folderBrowser.getBoundingClientRect().width
              : startWidth;
            setFolderBrowserWidth(finalWidth, true);
          };
          document.addEventListener('pointermove', onMove);
          document.addEventListener('pointerup', onEnd);
          document.addEventListener('pointercancel', onEnd);
          e.preventDefault();
        });
      }

      if (localDiskViewTableBtn) {
        localDiskViewTableBtn.addEventListener('click', function () {
          setLocalDiskViewMode('table');
        });
      }

      if (localDiskViewSplitBtn) {
        localDiskViewSplitBtn.addEventListener('click', function () {
          setLocalDiskViewMode('split');
        });
      }

      if (localDiskShowHidden) {
        localDiskShowHidden.addEventListener('change', function () {
          loadLocalDisk(activeLocalDiskPath || '', { resetTreeRoot: false });
        });
      }

      if (remoteDiskShowHidden) {
        remoteDiskShowHidden.addEventListener('change', function () {
          selectedFileNames.clear();
          activeFileRenamePath = '';
          loadFiles();
        });
      }

      if (localDiskImportBtn) {
        localDiskImportBtn.addEventListener('click', function () {
          openLocalImportDialog();
        });
      }

      if (localImportCancelBtn) {
        localImportCancelBtn.addEventListener('click', closeLocalImportDialog);
      }

      if (localImportConfirmBtn) {
        localImportConfirmBtn.addEventListener('click', function () {
          confirmLocalImport();
        });
      }

      if (localImportProgressClose) {
        localImportProgressClose.addEventListener('click', closeLocalImportProgressDialog);
      }

      if (localImportProgressPause) {
        localImportProgressPause.addEventListener('click', function () {
          controlLocalImportTask('pause').catch(function (err) {
            showStatus(t('暂停上传失败：') + err.message, 'err');
          });
        });
      }

      if (localImportProgressResume) {
        localImportProgressResume.addEventListener('click', function () {
          controlLocalImportTask('resume').catch(function (err) {
            showStatus(t('继续上传失败：') + err.message, 'err');
          });
        });
      }

      if (localImportProgressCancel) {
        localImportProgressCancel.addEventListener('click', function () {
          if (localImportProgressWindowMode === 'local-import') {
            controlLocalImportTask('cancel').catch(function (err) {
              showStatus(t('取消上传失败：') + err.message, 'err');
            });
          } else {
            cancelRemoteCopyTask().catch(function (err) {
              showStatus(t('取消粘贴失败：') + err.message, 'err');
            });
          }
        });
      }

      if (localImportProgressMinimize) {
        localImportProgressMinimize.addEventListener('click', minimizeLocalImportProgressWindow);
      }

      if (localImportProgressRestore) {
        localImportProgressRestore.addEventListener('click', restoreLocalImportProgressWindow);
      }

      if (localImportDialog) {
        localImportDialog.addEventListener('click', function (e) {
          if (e.target.closest('[data-local-import-close]')) {
            closeLocalImportDialog();
          }
        });
      }

      if (localImportTree) {
        localImportTree.addEventListener('click', function (e) {
          const createBtn = e.target.closest('[data-local-import-create]');
          if (createBtn) {
            e.preventDefault();
            e.stopPropagation();
            const path = createBtn.getAttribute('data-local-import-create') || '';
            createLocalImportSubfolder(path).catch(function (err) {
              showStatus(t('创建文件夹失败：') + err.message, 'err');
            });
            return;
          }
          const toggle = e.target.closest('[data-local-import-toggle]');
          if (toggle) {
            const path = toggle.getAttribute('data-local-import-toggle') || '';
            if (localImportExpandedFolderPaths.has(path)) {
              localImportExpandedFolderPaths.delete(path);
            } else {
              localImportExpandedFolderPaths.add(path);
            }
            renderLocalImportTree();
            return;
          }
          const entry = e.target.closest('[data-local-import-select]');
          if (!entry) {
            return;
          }
          localImportTargetFolderPath = entry.getAttribute('data-local-import-select') || '';
          ensureLocalImportFolderPathExpanded(localImportTargetFolderPath);
          renderLocalImportTree();
        });
      }

      if (localDiskBulkRemoveBtn) {
        localDiskBulkRemoveBtn.addEventListener('click', function () {
          removeSelectedLocalDiskFiles();
        });
      }

      if (localDiskTableBulkRemoveBtn) {
        localDiskTableBulkRemoveBtn.addEventListener('click', function () {
          removeSelectedLocalDiskFiles();
        });
      }

      if (localDiskBulkTagBtn) {
        localDiskBulkTagBtn.addEventListener('click', function () {
          openSelectedLocalDiskTagMenu(localDiskBulkTagBtn);
        });
      }

      if (localDiskTableBulkTagBtn) {
        localDiskTableBulkTagBtn.addEventListener('click', function () {
          openSelectedLocalDiskTagMenu(localDiskTableBulkTagBtn);
        });
      }

      if (localDiskSelectAll) {
        localDiskSelectAll.addEventListener('change', function () {
          setVisibleLocalDiskFilesSelected(localDiskSelectAll.checked);
        });
      }

      if (localDiskTableSelectAll) {
        localDiskTableSelectAll.addEventListener('change', function () {
          setVisibleLocalDiskFilesSelected(localDiskTableSelectAll.checked);
        });
      }

      localSortButtons.forEach(function (btn) {
        btn.addEventListener('click', function () {
          const nextKey = btn.getAttribute('data-local-sort-key') || 'name';
          if (localDiskSortKey === nextKey) {
            localDiskSortOrder = localDiskSortOrder === 'asc' ? 'desc' : 'asc';
          } else {
            localDiskSortKey = nextKey;
            localDiskSortOrder = 'asc';
          }
          renderLocalDiskItems(activeLocalDiskItems);
        });
      });

      if (localDiskList) {
        localDiskList.addEventListener('click', handleLocalDiskClickEvent);
        localDiskList.addEventListener('dblclick', handleLocalDiskFileDoubleClickEvent);
        localDiskList.addEventListener('keydown', handleLocalDiskFileRenameKeydownEvent);
        localDiskList.addEventListener('focusout', handleLocalDiskFileRenameFocusoutEvent);
        localDiskList.addEventListener('dragstart', handleLocalDiskDragStart);
        localDiskList.addEventListener('dragend', handleLocalDiskDragEnd);
        localDiskList.addEventListener('contextmenu', function (e) {
          const fileRow = e.target.closest('[data-local-file-context]');
          if (fileRow && localDiskList.contains(fileRow)) {
            e.preventDefault();
            const filePath = decodeURIComponent(fileRow.getAttribute('data-local-file-context') || '');
            const selectedFiles = getSelectedLocalDiskFilePaths();
            const actionFiles = selectedFiles.indexOf(filePath) >= 0 ? selectedFiles : [filePath];
            openFileContextMenu(
              filePath,
              true,
              fileRow.getAttribute('data-file-locked') === '1',
              fileRow.getAttribute('data-file-video') === '1',
              e.clientX,
              e.clientY,
              {
                localDiskList: true,
                localRename: true,
                deleteLabel: '移除',
                selectedPaths: actionFiles
              }
            );
            return;
          }
          const dirRow = e.target.closest('[data-local-dir-context]');
          if (dirRow && localDiskList.contains(dirRow)) {
            e.preventDefault();
            const dirPath = decodeURIComponent(dirRow.getAttribute('data-local-dir-context') || '');
            openLocalDirContextMenu(
              dirPath,
              dirRow.getAttribute('data-local-dir-locked') === '1',
              e.clientX,
              e.clientY
            );
          }
        });
      }

      if (localDiskExplorer) {
        localDiskExplorer.addEventListener('click', handleLocalDiskClickEvent);
        localDiskExplorer.addEventListener('dblclick', handleLocalDiskFileDoubleClickEvent);
        localDiskExplorer.addEventListener('keydown', handleLocalDiskFileRenameKeydownEvent);
        localDiskExplorer.addEventListener('focusout', handleLocalDiskFileRenameFocusoutEvent);
        localDiskExplorer.addEventListener('contextmenu', function (e) {
          const fileRow = e.target.closest('[data-local-file-context]');
          if (fileRow && localDiskExplorer.contains(fileRow)) {
            e.preventDefault();
            const filePath = decodeURIComponent(fileRow.getAttribute('data-local-file-context') || '');
            const selectedFiles = getSelectedLocalDiskFilePaths();
            const actionFiles = selectedFiles.indexOf(filePath) >= 0 ? selectedFiles : [filePath];
            openFileContextMenu(
              filePath,
              true,
              fileRow.getAttribute('data-file-locked') === '1',
              fileRow.getAttribute('data-file-video') === '1',
              e.clientX,
              e.clientY,
              {
                localDiskList: true,
                localRename: true,
                deleteLabel: '移除',
                selectedPaths: actionFiles
              }
            );
            return;
          }
          const dirRow = e.target.closest('[data-local-dir-context]');
          if (dirRow && localDiskExplorer.contains(dirRow)) {
            e.preventDefault();
            const dirPath = decodeURIComponent(dirRow.getAttribute('data-local-dir-context') || '');
            openLocalDirContextMenu(
              dirPath,
              dirRow.getAttribute('data-local-dir-locked') === '1',
              e.clientX,
              e.clientY
            );
          }
        });
        localDiskExplorer.addEventListener('dragstart', handleLocalDiskDragStart);
        localDiskExplorer.addEventListener('dragover', handleLocalDiskDragOver);
        localDiskExplorer.addEventListener('dragleave', handleLocalDiskDragLeave);
        localDiskExplorer.addEventListener('drop', handleLocalDiskDrop);
        localDiskExplorer.addEventListener('dragend', handleLocalDiskDragEnd);
      }

      sortKey.addEventListener('change', function () {
        renderFiles(activeSourceFiles);
      });

      sortOrder.addEventListener('change', function () {
        renderFiles(activeSourceFiles);
      });

      if (tagViewListBtn) {
        tagViewListBtn.addEventListener('click', function () {
          if (!isActiveTagPreviewEnabled()) return;
          if (tagFileViewMode === 'list') return;
          tagFileViewMode = 'list';
          tagViewListBtn.classList.add('active');
          if (tagViewPreviewBtn) tagViewPreviewBtn.classList.remove('active');
          renderFiles(activeSourceFiles);
        });
      }

      if (tagViewPreviewBtn) {
        tagViewPreviewBtn.addEventListener('click', function () {
          if (!isActiveTagPreviewEnabled()) return;
          if (tagFileViewMode === 'preview') return;
          tagFileViewMode = 'preview';
          tagViewPreviewBtn.classList.add('active');
          if (tagViewListBtn) tagViewListBtn.classList.remove('active');
          renderFiles(activeSourceFiles);
        });
      }

      if (tagImagePreviewWrap) {
        tagImagePreviewWrap.addEventListener('click', function (e) {
          const thumb = e.target.closest('.tag-img-thumb[data-thumb-file]');
          if (thumb) {
            const pfile = thumb.getAttribute('data-thumb-file');
            const pname = thumb.getAttribute('data-thumb-name') || '';
            const pindex = Number(thumb.getAttribute('data-thumb-index') || 0);
            if (pfile) {
              openPreview('image', pfile, pname, {
                previewKey: 'tag-gallery:' + String(activeFilterTagId || 'default'),
                gallery: activeTagPreviewImages.slice(),
                galleryIndex: pindex
              });
            }
          }
        });
        tagImagePreviewWrap.addEventListener('keydown', function (e) {
          if (e.key === 'Enter' || e.key === ' ') {
            const thumb = e.target.closest('.tag-img-thumb[data-thumb-file]');
            if (thumb) {
              e.preventDefault();
              const pfile = thumb.getAttribute('data-thumb-file');
              const pname = thumb.getAttribute('data-thumb-name') || '';
              const pindex = Number(thumb.getAttribute('data-thumb-index') || 0);
              if (pfile) {
                openPreview('image', pfile, pname, {
                  previewKey: 'tag-gallery:' + String(activeFilterTagId || 'default'),
                  gallery: activeTagPreviewImages.slice(),
                  galleryIndex: pindex
                });
              }
            }
          }
        });
      }

      sortButtons.forEach(function (btn) {
        btn.addEventListener('click', function () {
          const key = btn.getAttribute('data-sort-key');
          if (!key) {
            return;
          }

          if (sortKey.value === key) {
            sortOrder.value = sortOrder.value === 'asc' ? 'desc' : 'asc';
          } else {
            sortKey.value = key;
            sortOrder.value = 'asc';
          }

          renderFiles(activeSourceFiles);
        });
      });

      if (adminStorageTab) {
        adminStorageTab.addEventListener('click', function () {
          activateAdminView('storage');
        });
      }

      if (adminLocalDiskTab) {
        adminLocalDiskTab.addEventListener('click', function () {
          activateAdminView('local-disk');
        });
      }

      if (adminLocalDiskSettingsForm) {
        adminLocalDiskSettingsForm.addEventListener('submit', function (e) {
          e.preventDefault();
          saveAdminLocalDiskSettings();
        });
      }

      if (adminLanguageSelect) {
        adminLanguageSelect.value = authState.authenticated
          ? getCurrentUserPrefs().ui_language
          : UI_LANG;
      }

      if (authLanguageSelect) {
        authLanguageSelect.value = UI_LANG;
        authLanguageSelect.addEventListener('change', function () {
          applyLanguageSelection(authLanguageSelect, false);
        });
      }

      if (adminLanguageApplyBtn) {
        adminLanguageApplyBtn.addEventListener('click', applyLanguageSetting);
      }

      if (adminFontSizeSelect) {
        adminFontSizeSelect.value = loadFontSizePreference(authState.username || '');
      }

      if (adminFontSizeApplyBtn) {
        adminFontSizeApplyBtn.addEventListener('click', applyFontSizeSetting);
      }

      if (accountPasswordTab) {
        accountPasswordTab.addEventListener('click', function () {
          activateAccountView('password');
        });
      }

      if (accountLanguageTab) {
        accountLanguageTab.addEventListener('click', function () {
          activateAccountView('language');
        });
      }

      if (accountFontSizeTab) {
        accountFontSizeTab.addEventListener('click', function () {
          activateAccountView('font-size');
        });
      }

      if (authForm) {
        authForm.addEventListener('submit', function (e) {
          e.preventDefault();
          submitAuthForm();
        });
      }

      if (authLogoutBtn) {
        authLogoutBtn.addEventListener('click', async function () {
          try {
            await fetchJson(api.authLogout, { method: 'POST' });
          } catch (_) {}
          requireLoginAgain();
        });
      }

      if (adminUserCreateForm) {
        adminUserCreateForm.addEventListener('submit', function (e) {
          e.preventDefault();
          createNormalUser();
        });
      }

      if (accountPasswordForm) {
        accountPasswordForm.addEventListener('submit', function (e) {
          e.preventDefault();
          changeOwnPassword();
        });
      }

      if (adminUsersList) {
        adminUsersList.addEventListener('click', function (e) {
          const editBtn = e.target.closest('[data-user-edit]');
          if (editBtn) {
            updateNormalUser(editBtn.getAttribute('data-user-edit') || '');
            return;
          }
          const deleteBtn = e.target.closest('[data-user-delete]');
          if (deleteBtn) {
            deleteNormalUser(deleteBtn.getAttribute('data-user-delete') || '');
          }
        });
      }

      if (adminStorageBrowseBtn) {
        adminStorageBrowseBtn.addEventListener('click', function () {
          adminStoragePickerTarget = 'storage';
          openAdminStoragePickerDialog();
        });
      }

      if (adminStorageChooseBtn) {
        adminStorageChooseBtn.addEventListener('click', function () {
          applyAdminStoragePathChange();
        });
      }

      if (adminStorageSyncBtn) {
        adminStorageSyncBtn.addEventListener('click', function () {
          syncAdminStorageBackupPaths();
        });
      }

      if (adminStorageBackupSetBtn) {
        adminStorageBackupSetBtn.addEventListener('click', function () {
          applyAdminStorageBackupPathChange();
        });
      }

      if (adminStorageUploadSyncToggleBtn) {
        adminStorageUploadSyncToggleBtn.addEventListener('click', function () {
          toggleAdminStorageUploadAutoSync();
        });
      }

      if (adminStorageBackupBrowseBtn) {
        adminStorageBackupBrowseBtn.addEventListener('click', function () {
          openAdminStorageBackupPickerDialog();
        });
      }

      if (adminStorageBackupList) {
        adminStorageBackupList.addEventListener('click', function (e) {
          const swapBtn = e.target.closest('[data-storage-backup-swap]');
          if (swapBtn) {
            swapAdminStorageBackupPath(decodeURIComponent(swapBtn.getAttribute('data-storage-backup-swap') || ''));
            return;
          }
          const removeBtn = e.target.closest('[data-storage-backup-remove]');
          if (removeBtn) {
            removeAdminStorageBackupPath(decodeURIComponent(removeBtn.getAttribute('data-storage-backup-remove') || ''));
            return;
          }
          const toggleBtn = e.target.closest('[data-storage-backup-toggle]');
          if (toggleBtn) {
            toggleAdminStorageBackupPath(decodeURIComponent(toggleBtn.getAttribute('data-storage-backup-toggle') || ''));
          }
        });
      }

      if (adminStoragePath) {
        adminStoragePath.addEventListener('keydown', function (e) {
          if (e.key === 'Enter') {
            e.preventDefault();
            applyAdminStoragePathChange();
          }
        });
      }

      if (adminStorageProgressPauseBtn) {
        adminStorageProgressPauseBtn.addEventListener('click', function () {
          controlAdminStorageMigration('pause').catch(function (err) {
            showStatus((activeAdminStorageProgressMode === 'backup' ? t('暂停备份失败：') : t('暂停迁移失败：')) + err.message, 'err');
          });
        });
      }

      if (adminStorageProgressResumeBtn) {
        adminStorageProgressResumeBtn.addEventListener('click', function () {
          controlAdminStorageMigration('resume').catch(function (err) {
            showStatus((activeAdminStorageProgressMode === 'backup' ? t('继续备份失败：') : t('继续迁移失败：')) + err.message, 'err');
          });
        });
      }

      if (adminStorageProgressCancelBtn) {
        adminStorageProgressCancelBtn.addEventListener('click', async function () {
          const backupMode = activeAdminStorageProgressMode === 'backup';
          const confirmed = await askConfirmDialog({
            title: backupMode ? t('停止备份') : t('取消迁移'),
            description: backupMode ? t('确定要停止本次备份同步吗？') : t('确定要取消本次迁移吗？'),
            confirmText: backupMode ? t('停止备份') : t('确认取消'),
            cancelText: backupMode ? t('继续备份') : t('继续迁移'),
            danger: true
          });
          if (confirmed) {
            controlAdminStorageMigration('cancel').catch(function (err) {
              showStatus((backupMode ? t('停止备份失败：') : t('取消迁移失败：')) + err.message, 'err');
            });
          }
        });
      }

      if (adminStoragePickerCancelBtn) {
        adminStoragePickerCancelBtn.addEventListener('click', function () {
          adminStoragePickerTarget = 'storage';
          closeAdminStoragePickerDialog();
        });
      }

      if (adminStoragePickerRootBtn) {
        adminStoragePickerRootBtn.addEventListener('click', jumpAdminStoragePickerRoot);
      }

      if (adminStoragePickerHomeBtn) {
        adminStoragePickerHomeBtn.addEventListener('click', jumpAdminStoragePickerHome);
      }

      if (adminStoragePickerConfirmBtn) {
        adminStoragePickerConfirmBtn.addEventListener('click', function () {
          if (adminStoragePickerTarget === 'backup' && adminStorageBackupPath && adminStoragePickerSelectedPath) {
            adminStorageBackupPath.value = adminStoragePickerSelectedPath;
          } else if (adminStoragePath && adminStoragePickerSelectedPath) {
            adminStoragePath.value = adminStoragePickerSelectedPath;
          }
          adminStoragePickerTarget = 'storage';
          closeAdminStoragePickerDialog();
        });
      }

      if (adminStoragePickerDialog) {
        adminStoragePickerDialog.addEventListener('click', function (e) {
          if (e.target.closest('[data-admin-storage-picker-close]')) {
            adminStoragePickerTarget = 'storage';
            closeAdminStoragePickerDialog();
          }
        });
      }

      if (adminStoragePickerTree) {
        adminStoragePickerTree.addEventListener('click', function (e) {
          const toggle = e.target.closest('[data-admin-storage-picker-toggle]');
          if (toggle) {
            const path = decodeURIComponent(toggle.getAttribute('data-admin-storage-picker-toggle') || '');
            toggleAdminStoragePickerPath(path);
            return;
          }
          const entry = e.target.closest('[data-admin-storage-picker-select]');
          if (!entry) {
            return;
          }
          adminStoragePickerSelectedPath = decodeURIComponent(entry.getAttribute('data-admin-storage-picker-select') || '');
          renderAdminStoragePicker();
        });
      }

      if (reloadBtn) {
        reloadBtn.addEventListener('click', async function () {
          resetStatus();
          try {
            await fetchJson(api.reloadTpl);
            showStatus('模板缓存已刷新', 'warn');
          } catch (err) {
            showStatus('刷新模板缓存失败：' + err.message, 'err');
          }
        });
      }

      statusBox.addEventListener('click', function (e) {
        const btn = e.target.closest('.transcode-btn[data-transcode-file]');
        if (btn && !btn.disabled) {
          const encoded = btn.getAttribute('data-transcode-file');
          const mode = btn.getAttribute('data-transcode-mode') || 'auto';
          if (encoded) {
            startManualTranscode(encoded, mode);
          }
          return;
        }

        const cancelBtn = e.target.closest('.transcode-cancel-btn[data-cancel-file]');
        if (cancelBtn && !cancelBtn.disabled) {
          const encoded = cancelBtn.getAttribute('data-cancel-file');
          if (encoded) {
            cancelManualTranscode(encoded);
          }
          return;
        }

        const confirmBtn = e.target.closest('.transcode-confirm-btn[data-confirm-transcode]');
        if (confirmBtn && !confirmBtn.hidden) {
          const encoded = confirmBtn.getAttribute('data-confirm-transcode');
          if (encoded) {
            dismissTranscodeItem(encoded);
          }
        }
      });

      fileList.addEventListener('click', async function (e) {
        const renameInput = e.target.closest('.file-rename-input');
        if (renameInput) {
          return;
        }

        const fileNameClick = e.target.closest('.file-name-action[data-file-name-click]');
        if (fileNameClick) {
          e.preventDefault();
          e.stopPropagation();
          handleFileNameClick(
            decodeURIComponent(fileNameClick.getAttribute('data-file-name-click') || ''),
            fileNameClick.getAttribute('data-file-local') === '1'
          );
          return;
        }

        if (e.target.closest('.file-select-input')) {
          return;
        }

        const fileLockIcon = e.target.closest('.file-lock-inline');
        if (fileLockIcon) {
          const node = fileLockIcon.closest('[data-file-context]');
          if (!node) {
            return;
          }
          e.preventDefault();
          e.stopPropagation();
          const path = decodeURIComponent(node.getAttribute('data-file-context') || '');
          const local = node.getAttribute('data-file-local') === '1';
          const action = getFilePassword(path, local) ? 'session-lock' : 'session-unlock';
          try {
            await handleFileContextAction(action, path, local);
          } catch (err) {
            showStatus('文件锁操作失败：' + err.message, 'err');
          }
          return;
        }

        const quickTagBtn = e.target.closest('.file-tag-quick-btn[data-tag-file]');
        if (quickTagBtn) {
          e.preventDefault();
          e.stopPropagation();
          const fileName = decodeURIComponent(quickTagBtn.getAttribute('data-tag-file') || '');
          try {
            await openFileTagMenu(quickTagBtn, fileName);
          } catch (err) {
            showStatus('打开标签选择失败：' + err.message, 'err');
          }
          return;
        }

        const localPreview = e.target.closest('.local-preview-btn[data-local-file][data-kind]');
        if (localPreview) {
          const path = decodeURIComponent(localPreview.getAttribute('data-local-file') || '');
          const kind = localPreview.getAttribute('data-kind') || 'image';
          const name = localPreview.getAttribute('data-local-name') || path;
          if (kind === 'image') {
            openCurrentFileImagePreview(path, name);
            return;
          }
          openPreview(kind, encodeURIComponent(path), name, {
            local: true,
            url: localDiskDownloadUrl(path),
            previewKey: 'local:' + path
          });
          return;
        }

        const pdf = e.target.closest('.pdf-btn');
        if (pdf) {
          const pfile = pdf.getAttribute('data-pdf-file');
          const pname = pdf.getAttribute('data-pdf-name') || '';
          if (pfile) {
            openPreview('pdf', pfile, pname);
          }
          return;
        }

        const office = e.target.closest('.office-btn');
        if (office) {
          const ofile = office.getAttribute('data-office-file');
          const oname = office.getAttribute('data-office-name') || '';
          if (ofile) {
            openPreview('office', ofile, oname);
          }
          return;
        }

        const mindmap = e.target.closest('.mindmap-btn');
        if (mindmap) {
          const mfile = mindmap.getAttribute('data-mindmap-file');
          const mname = mindmap.getAttribute('data-mindmap-name') || '';
          if (mfile) {
            openPreview('mindmap', mfile, mname);
          }
          return;
        }

        const preview = e.target.closest('.preview-btn');
        if (preview) {
          const pfile = preview.getAttribute('data-preview-file');
          const pname = preview.getAttribute('data-preview-name') || '';
          if (pfile) {
            openCurrentFileImagePreview(decodeURIComponent(pfile), pname);
          }
          return;
        }

        const video = e.target.closest('.video-btn');
        if (video) {
          const vfile = video.getAttribute('data-video-file');
          const vname = video.getAttribute('data-video-name') || '';
          if (vfile) {
            openPreview('video', vfile, vname);
          }
          return;
        }

        const audio = e.target.closest('.audio-btn');
        if (audio) {
          const afile = audio.getAttribute('data-audio-file');
          const aname = audio.getAttribute('data-audio-name') || '';
          if (afile) {
            openPreview('audio', afile, aname);
          }
          return;
        }

        const text = e.target.closest('.text-btn');
        if (text) {
          const tfile = text.getAttribute('data-text-file');
          const tname = text.getAttribute('data-text-name') || '';
          if (tfile) {
            openPreview('text', tfile, tname);
          }
          return;
        }

        const btn = e.target.closest('.delete-btn');
    const restoreBtn = e.target.closest('.restore-btn');
    if (restoreBtn) {
      const restoreFile = restoreBtn.getAttribute('data-file');
      const restoreName = decodeURIComponent(restoreFile || '') || (restoreBtn.getAttribute('data-name') || '');
      if (!restoreFile) {
      return;
      }
      if (!confirm('确认恢复文件：' + restoreName + ' ？将恢复到原路径（如冲突会自动改名）。')) {
      return;
      }
      resetStatus();
      try {
      const result = await fetchJson(withFolderPassword(api.restore + '?file=' + restoreFile, activeFolderPath), { method: 'POST' });
      const targetPath = String((result && result.path) || '');
      showStatus('已恢复：' + restoreName + (targetPath ? (' -> ' + targetPath) : ''), 'ok');
      await loadFiles();
      } catch (err) {
      showStatus('恢复失败：' + err.message, 'err');
      }
      return;
    }
        if (!btn) {
          return;
        }

        const file = btn.getAttribute('data-file');
        const name = decodeURIComponent(file || '') || (btn.getAttribute('data-name') || '');
        if (!file) {
          return;
        }

        try {
          await handleFileDeleteOrRemove(name, btn.getAttribute('data-local-tag-file') === '1');
        } catch (err) {
          showStatus(t('删除失败：') + err.message, 'err');
        }
      });

      fileList.addEventListener('dblclick', function (e) {
        const fileNameClick = e.target.closest('.file-name-action[data-file-name-click]');
        if (!fileNameClick) {
          return;
        }
        e.preventDefault();
        e.stopPropagation();
        handleFileNameDoubleClick(
          decodeURIComponent(fileNameClick.getAttribute('data-file-name-click') || ''),
          fileNameClick.getAttribute('data-file-local') === '1'
        );
      });

      fileList.addEventListener('keydown', function (e) {
        const input = e.target.closest('.file-rename-input');
        if (!input) {
          return;
        }
        if (e.key === 'Enter') {
          e.preventDefault();
          submitFileRename(input);
        } else if (e.key === 'Escape') {
          e.preventDefault();
          cancelFileRename();
        }
      });

      fileList.addEventListener('focusout', function (e) {
        const input = e.target.closest('.file-rename-input');
        if (!input) {
          return;
        }
        window.setTimeout(function () {
          if (document.activeElement !== input) {
            submitFileRename(input);
          }
        }, 0);
      });

      fileList.addEventListener('contextmenu', function (e) {
        const cell = e.target.closest('[data-file-context]');
        if (!cell || !fileList.contains(cell)) {
          return;
        }
        e.preventDefault();
        const filePath = decodeURIComponent(cell.getAttribute('data-file-context') || '');
        const fileLocal = cell.getAttribute('data-file-local') === '1';
        const selectedNames = getSelectedVisibleFileNames();
        const selectedSameKind = selectedNames.filter(function (name) {
          return isCurrentFileLocal(name) === fileLocal;
        });
        const actionFiles = selectedSameKind.indexOf(filePath) >= 0 ? selectedSameKind : [filePath];
        openFileContextMenu(
          filePath,
          fileLocal,
          cell.getAttribute('data-file-locked') === '1',
          cell.getAttribute('data-file-video') === '1',
          e.clientX,
          e.clientY,
          {
            remoteList: true,
            tagMode: !!activeFilterTagId,
            recycleMode: !activeFilterTagId && isRecycleFolderPath(activeFolderPath),
            selectedPaths: actionFiles
          }
        );
      });

      fileList.addEventListener('change', function (e) {
        const checkbox = e.target.closest('.file-select-input[data-select-file]');
        if (!checkbox) {
          return;
        }
        const fileName = decodeURIComponent(checkbox.getAttribute('data-select-file') || '');
        if (!fileName) {
          return;
        }
        if (checkbox.checked) {
          selectedFileNames.add(fileName);
        } else {
          selectedFileNames.delete(fileName);
        }
        updateFileSelectAllState();
        updateFileBulkActionButton();
      });

      fileList.addEventListener('dragstart', function (e) {
        if (e.target.closest('.file-rename-input')) {
          e.preventDefault();
          return;
        }
        const row = e.target.closest('tr[data-drag-file]');
        if (!row || !e.dataTransfer) {
          return;
        }
        const encoded = row.getAttribute('data-drag-file') || '';
        const fileName = decodeURIComponent(encoded);
        if (!fileName) {
          return;
        }
        const selectedNames = selectedFileNames.has(fileName)
          ? getSelectedVisibleFileNames()
          : [fileName];
        e.dataTransfer.effectAllowed = 'copyMove';
        e.dataTransfer.setData('text/plain', fileName);
        e.dataTransfer.setData('application/webcool-file-list', JSON.stringify(selectedNames));
      });

      fileList.addEventListener('dragend', function () {
        clearDropHighlight();
      });

      if (fileSelectAll) {
        fileSelectAll.addEventListener('change', function () {
          (Array.isArray(currentFiles) ? currentFiles : []).forEach(function (file) {
            const fileName = getFilePath(file);
            if (!fileName) {
              return;
            }
            if (fileSelectAll.checked) {
              selectedFileNames.add(fileName);
            } else {
              selectedFileNames.delete(fileName);
            }
          });
          renderFiles(activeSourceFiles);
        });
      }

      if (fileBulkAction) {
        fileBulkAction.addEventListener('click', async function () {
          const fileNames = getSelectedVisibleFileNames();
          if (!fileNames.length) {
            updateFileBulkActionButton();
            return;
          }

          const activeTagId = activeFilterTagId;
          const isRecycleMode = !activeTagId && isRecycleFolderPath(activeFolderPath);
          const actionLabel = activeTagId ? '移除' : (isRecycleMode ? '恢复' : '删除');
          let confirmed = false;
          if (activeTagId) {
            confirmed = await askDeleteConfirmDialog({
              title: t('移除标签引用'),
              description: t('此操作只解除标签引用，不会删除文件。'),
              highlight: fileNames.length + t(' 个文件'),
              confirmText: t('移除'),
              variant: 'warn',
              icon: '🏷'
            });
          } else if (isRecycleMode) {
            confirmed = await askConfirmDialog({
              title: t('恢复文件'),
              description: t('将恢复到原路径；若目标位置已有同名文件，会自动改名。'),
              highlight: fileNames.length + t(' 个文件'),
              confirmText: t('恢复'),
              danger: false,
              variant: 'info',
              icon: '↩️'
            });
          } else {
            confirmed = await askDeleteConfirmDialog({
              title: t('删除'),
              description: t('选中的文件将先移入回收站。'),
              highlight: fileNames.length + t(' 个文件'),
              note: t('文件将移入回收站，不会立即永久删除。'),
              confirmText: t('移入回收站')
            });
          }
          if (!confirmed) {
            return;
          }

          resetStatus();
          let completedCount = 0;
          try {
            if (activeTagId) {
              for (let i = 0; i < fileNames.length; i += 1) {
                const ok = await unbindFileFromTag(activeTagId, fileNames[i], { local: isCurrentFileLocal(fileNames[i]) });
                if (!ok) {
                  throw new Error('关联不存在');
                }
                completedCount += 1;
              }
              fileNames.forEach(function (name) {
                selectedFileNames.delete(name);
              });
              await showFilesForTag(activeTagId);
            } else if (isRecycleMode) {
              for (let i = 0; i < fileNames.length; i += 1) {
                await fetchJson(withFolderPassword(api.restore + '?file=' + encodeURIComponent(fileNames[i]), activeFolderPath), { method: 'POST' });
                completedCount += 1;
              }
              fileNames.forEach(function (name) {
                selectedFileNames.delete(name);
              });
              await loadFiles();
            } else {
              for (let i = 0; i < fileNames.length; i += 1) {
                await fetchJson(appendFilePassword(withFolderPassword(api.del + '?file=' + encodeURIComponent(fileNames[i]), parentFolderPathFromFilePath(fileNames[i])), fileNames[i], false));
                completedCount += 1;
              }
              fileNames.forEach(function (name) {
                selectedFileNames.delete(name);
              });
              await loadFiles();
            }

            showStatus(t('已批量') + actionLabel + ' ' + completedCount + t(' 个文件'), activeTagId ? 'warn' : 'warn');
          } catch (err) {
            if (completedCount > 0) {
              if (activeTagId) {
                await showFilesForTag(activeTagId);
              } else {
                await loadFiles();
              }
              showStatus(t('批量') + actionLabel + t('在处理 ') + completedCount + t(' 个文件后失败：') + err.message, 'err');
              return;
            }
            showStatus(t('批量') + actionLabel + t('失败：') + err.message, 'err');
          }
        });
      }

      if (fileBulkTagAction) {
        fileBulkTagAction.addEventListener('click', async function () {
          const fileNames = getSelectedVisibleFileNames();
          if (!fileNames.length) {
            showStatus(t('请先选择要加标签的文件'), 'err');
            return;
          }
          try {
            await openFilesTagMenu(fileBulkTagAction, fileNames);
          } catch (err) {
            showStatus(t('打开标签选择失败：') + err.message, 'err');
          }
        });
      }

      if (fileBulkDeleteAction) {
        fileBulkDeleteAction.addEventListener('click', async function () {
          const fileNames = getSelectedVisibleFileNames();
          if (!fileNames.length) {
            updateFileBulkActionButton();
            return;
          }

          const isRecycleMode = !activeFilterTagId && isRecycleFolderPath(activeFolderPath);
          if (!isRecycleMode) {
            return;
          }

          const selectedTypes = summarizeSelectedFileTypes(fileNames);
          const confirmedPermanent = await askDeleteConfirmDialog({
            title: t('彻底删除'),
            description: selectedTypes.folderCount > 0
              ? t('选中的文件夹会连同其全部内容一起删除。此操作不可恢复。')
              : t('此操作不可恢复，请确认是否继续。'),
            highlight: fileNames.length + t(' 个') + selectedTypes.label,
            confirmText: t('彻底删除')
          });
          if (!confirmedPermanent) {
            return;
          }

          resetStatus();
          let completedCount = 0;
          try {
            for (let i = 0; i < fileNames.length; i += 1) {
              await fetchJson(appendFilePassword(withFolderPassword(api.del + '?file=' + encodeURIComponent(fileNames[i]), parentFolderPathFromFilePath(fileNames[i])), fileNames[i], false));
              completedCount += 1;
            }
            fileNames.forEach(function (name) {
              selectedFileNames.delete(name);
            });
            await loadFiles();
            showStatus(t('已批量彻底删除 ') + completedCount + t(' 个') + selectedTypes.label, 'warn');
          } catch (err) {
            if (completedCount > 0) {
              await loadFiles();
              showStatus(t('批量彻底删除在处理 ') + completedCount + t(' 个文件后失败：') + err.message, 'err');
              return;
            }
            showStatus(t('批量彻底删除失败：') + err.message, 'err');
          }
        });
      }

      if (folderCreateBtn) {
        folderCreateBtn.addEventListener('click', async function () {
          try {
            await createFolderAtCurrentPath();
            await loadFiles();
          } catch (err) {
            showStatus(t('创建文件夹失败：') + err.message, 'err');
          }
        });
      }

      if (folderDeleteBtn) {
        folderDeleteBtn.addEventListener('click', async function () {
          try {
            await deleteCurrentFolder();
            await loadFiles();
          } catch (err) {
            showStatus(t('删除文件夹失败：') + err.message, 'err');
          }
        });
      }

      if (folderRestoreBtn) {
        folderRestoreBtn.addEventListener('click', async function () {
          try {
            await restoreCurrentRecycleFolder();
          } catch (err) {
            showStatus(t('恢复文件夹失败：') + err.message, 'err');
          }
        });
      }

      if (folderTree) {
        folderTree.addEventListener('click', async function (e) {
          if (e.target.closest('.folder-rename-input')) {
            return;
          }
          const lockToggle = e.target.closest('.folder-lock-icon.unlocked[data-folder-lock-toggle]');
          if (lockToggle) {
            e.preventDefault();
            e.stopPropagation();
            try {
              await relockFolderInSession(lockToggle.getAttribute('data-folder-lock-toggle') || '');
            } catch (err) {
              showStatus(t('重新加锁失败：') + err.message, 'err');
            }
            return;
          }
          const toggle = e.target.closest('.folder-tree-toggle[data-folder-toggle]');
          if (toggle) {
            const path = toggle.getAttribute('data-folder-toggle') || '';
            if (expandedFolderPaths.has(path)) {
              expandedFolderPaths.delete(path);
            } else {
              expandedFolderPaths.add(path);
              try {
                await loadFolderChildren(path);
              } catch (err) {
                expandedFolderPaths.delete(path);
                showStatus(t('加载文件夹失败：') + err.message, 'err');
              }
            }
            renderFolderTree();
            return;
          }

          const entry = e.target.closest('.folder-tree-entry[data-folder-select]');
          if (!entry) {
            return;
          }
          const path = entry.getAttribute('data-folder-select') || '';
          if (e.detail >= 2 && canRenameFolderPath(path)) {
            e.preventDefault();
            const wasUnlocked = isFolderUnlockedInSession(path);
            if (!(await ensureFolderUnlocked(path))) {
              return;
            }
            activeFolderPath = path;
            ensureFolderPathExpanded(activeFolderPath);
            if (!wasUnlocked && getFolderPasswordForPath(path)) {
              await loadFiles();
            }
            startFolderRename(path);
            renderFiles(activeSourceFiles);
            return;
          }
          if (!(await ensureFolderUnlocked(path))) {
            return;
          }
          activeFolderPath = path;
          selectFolderPath(path, e.shiftKey);
          ensureFolderParentPathExpanded(activeFolderPath);
          if (expandedFolderPaths.has(path)) {
            expandedFolderPaths.delete(path);
          } else {
            expandedFolderPaths.add(path);
            try {
              await loadFolderChildren(path);
            } catch (err) {
              expandedFolderPaths.delete(path);
              showStatus(t('加载文件夹失败：') + err.message, 'err');
            }
          }
          await loadFiles();
        });

        folderTree.addEventListener('contextmenu', function (e) {
          if (e.target.closest('.folder-rename-input')) {
            return;
          }
          const entry = e.target.closest('.folder-tree-entry[data-folder-select]');
          if (!entry) {
            return;
          }
          const path = entry.getAttribute('data-folder-select') || '';
          if (path && !canRenameFolderPath(path)
            && !isRecycleRootFolderPath(path)
            && !isSharedRootFolderPath(path)
            && !isReservedFixedFolderPath(path)) {
            return;
          }
          e.preventDefault();
          openFolderContextMenu(path, e.clientX, e.clientY);
        });

        folderTree.addEventListener('dblclick', function (e) {
          if (e.target.closest('.folder-rename-input')) {
            return;
          }
          const entry = e.target.closest('.folder-tree-entry[data-folder-select]');
          if (!entry) {
            return;
          }
          const path = entry.getAttribute('data-folder-select') || '';
          if (!canRenameFolderPath(path)) {
            return;
          }
          e.preventDefault();
          startFolderRename(path);
        });

        folderTree.addEventListener('keydown', function (e) {
          const input = e.target.closest('.folder-rename-input[data-folder-rename-input]');
          if (!input) {
            return;
          }
          if (e.key === 'Escape') {
            e.preventDefault();
            cancelFolderRename();
            return;
          }
          if (e.key === 'Enter') {
            e.preventDefault();
            submitFolderRename(input);
          }
        });

        folderTree.addEventListener('focusout', function (e) {
          const input = e.target.closest('.folder-rename-input[data-folder-rename-input]');
          if (!input) {
            return;
          }
          submitFolderRename(input);
        });

        folderTree.addEventListener('dragstart', function (e) {
          if (e.target.closest('.folder-rename-input')) {
            e.preventDefault();
            return;
          }
          const entry = e.target.closest('.folder-tree-entry[data-drag-folder]');
          if (!entry || !e.dataTransfer) {
            return;
          }
          const folderPath = entry.getAttribute('data-drag-folder') || '';
          if (!folderPath || isRecycleFolderPath(folderPath)
            || isSharedRootFolderPath(folderPath)
            || isReservedFixedFolderPath(folderPath)) {
            e.preventDefault();
            return;
          }
          const dragPaths = getFolderDragPaths(folderPath);
          e.dataTransfer.effectAllowed = 'move';
          e.dataTransfer.setData('text/plain', folderPath);
          e.dataTransfer.setData('application/webcool-folder-list', JSON.stringify(dragPaths));
        });

        folderTree.addEventListener('dragend', function () {
          clearFolderAutoExpandTimer();
          activeDropFolderPath = null;
          syncFolderDropHighlight();
        });

        folderTree.addEventListener('dragover', function (e) {
          const node = e.target.closest('.folder-tree-node[data-folder-path]');
          if (!node || !e.dataTransfer) {
            return;
          }
          const nextDropPath = node.getAttribute('data-folder-path') || '';
          if (isBrowserFileDragEvent(e) || isLocalDiskDragEvent(e)) {
            if (!canUploadLocalDiskToRemoteFolder(nextDropPath)) {
              return;
            }
            e.preventDefault();
            e.stopPropagation();
            if (activeDropFolderPath !== nextDropPath) {
              activeDropFolderPath = nextDropPath;
              syncFolderDropHighlight();
              scrollFolderPathIntoView(nextDropPath);
            }
            scheduleFolderAutoExpand(nextDropPath);
            e.dataTransfer.dropEffect = 'copy';
            return;
          }
          e.preventDefault();
          if (activeDropFolderPath !== nextDropPath) {
            activeDropFolderPath = nextDropPath;
            syncFolderDropHighlight();
            scrollFolderPathIntoView(nextDropPath);
          }
          scheduleFolderAutoExpand(nextDropPath);
          e.dataTransfer.dropEffect = 'move';
        });

        folderTree.addEventListener('dragleave', function (e) {
          if (!folderTree.contains(e.relatedTarget)) {
            clearFolderAutoExpandTimer();
            activeDropFolderPath = null;
            syncFolderDropHighlight();
          }
        });

        folderTree.addEventListener('drop', async function (e) {
          const node = e.target.closest('.folder-tree-node[data-folder-path]');
          clearFolderAutoExpandTimer();
          activeDropFolderPath = null;
          syncFolderDropHighlight();
          if (!node) {
            return;
          }
          const targetFolder = node.getAttribute('data-folder-path') || '';
          const isRemoteUploadDrop = (isBrowserFileDragEvent(e) && e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files.length)
            || isLocalDiskDragEvent(e);
          if (isRemoteUploadDrop) {
            e.preventDefault();
            e.stopPropagation();
            try {
              await queueRemoteUploadDrop(e, targetFolder);
            } catch (err) {
              showStatus(t('添加待上传文件失败：') + err.message, 'err');
            }
            return;
          }
          let folderPaths = [];
          let fileNames = [];
          if (e.dataTransfer) {
            try {
              folderPaths = JSON.parse(e.dataTransfer.getData('application/webcool-folder-list') || '[]');
            } catch (_) {
              folderPaths = [];
            }
            try {
              fileNames = JSON.parse(e.dataTransfer.getData('application/webcool-file-list') || '[]');
            } catch (_) {
              fileNames = [];
            }
            if (!folderPaths.length && !fileNames.length) {
              const fallbackName = String(e.dataTransfer.getData('text/plain') || '');
              if (fallbackName) {
                fileNames = [fallbackName];
              }
            }
          }
          e.preventDefault();
          if (folderPaths.length) {
            try {
              const summary = isRecycleRootFolderPath(targetFolder)
                ? await moveFoldersToRecycle(folderPaths)
                : await moveFoldersToFolder(folderPaths, targetFolder);
              if (summary.cancelled) {
                return;
              }
              let message = isRecycleRootFolderPath(targetFolder)
                ? (summary.movedCount > 1 ? ('已将 ' + summary.movedCount + ' 个文件夹移入回收站') : '文件夹已移入回收站')
                : (summary.movedCount > 1 ? ('已移动 ' + summary.movedCount + ' 个文件夹') : '文件夹已移动');
              if (summary.ignoredCount > 0) {
                message += '，已忽略 ' + summary.ignoredCount + ' 个重复子文件夹';
              }
              showStatus(message, isRecycleRootFolderPath(targetFolder) ? 'warn' : 'ok');
            } catch (err) {
              showStatus((isRecycleRootFolderPath(targetFolder) ? t('移入回收站失败：') : t('移动文件夹失败：')) + err.message, 'err');
            }
            return;
          }
          if (!fileNames.length) {
            return;
          }
          try {
            await moveFilesToFolder(fileNames, targetFolder);
          } catch (err) {
            showStatus(t('移动文件失败：') + err.message, 'err');
          }
        });
      }

      function resolveRemoteUploadFolderFromDropEvent(e) {
        const treeNode = e.target.closest('.folder-tree-node[data-folder-path]');
        if (treeNode) {
          return treeNode.getAttribute('data-folder-path') || '';
        }
        return String(activeFolderPath || '');
      }

      if (explorerShell) {
        bindRemoteUploadDropZone(explorerShell, function (e) {
          return resolveRemoteUploadFolderFromDropEvent(e);
        });
        explorerShell.addEventListener('dragenter', function (e) {
          if (isRemoteUploadDragEvent(e)) {
            explorerShell.classList.add('remote-upload-drop-active');
          }
        });
        explorerShell.addEventListener('dragleave', function (e) {
          if (!explorerShell.contains(e.relatedTarget)) {
            explorerShell.classList.remove('remote-upload-drop-active');
          }
        });
        explorerShell.addEventListener('drop', function () {
          explorerShell.classList.remove('remote-upload-drop-active');
        });
      }

      const rightPane = document.querySelector('.right-pane');
      if (rightPane) {
        rightPane.addEventListener('dragenter', function (e) {
          if (!isRemoteUploadDragEvent(e)) {
            return;
          }
          const filesPanel = document.getElementById('panel-files');
          if (filesPanel && !filesPanel.classList.contains('active')) {
            activatePanel('panel-files', { skipLoadFiles: true });
          }
        });
      }

      const filesMenuBtn = document.querySelector('.menu-btn[data-panel="panel-files"]');
      if (filesMenuBtn) {
        bindRemoteUploadDropZone(filesMenuBtn, function () {
          return String(activeFolderPath || '');
        }, {
          beforeUpload: function () {
            activatePanel('panel-files', { skipLoadFiles: true });
          },
          beforeQueue: function () {
            activatePanel('panel-files', { skipLoadFiles: true });
          }
        });
      }

      if (filesTagToggleBtn) {
        filesTagToggleBtn.addEventListener('click', async function () {
          const rootName = await askTagName({
            title: t('新建一级标签'),
            description: t('标签会显示在左侧树的第一层。'),
            placeholder: t('请输入一级标签名称')
          });
          if (rootName === null) {
            return;
          }
          const result = await addTagNode('', rootName);
          if (!result.ok) {
            showStatus(t('创建标签失败：') + result.message, 'err');
            return;
          }
          await loadTagTreeState();
          renderTagTree();
          showStatus(t('一级标签已创建'), 'ok');
        });
      }

      if (tagManager) {
        tagManager.addEventListener('click', async function (e) {
          if (e.target.closest('.tag-rename-input')) {
            return;
          }
          const tagLockIcon = e.target.closest('.tag-lock-inline[data-tag-lock-toggle]');
          if (tagLockIcon) {
            e.preventDefault();
            e.stopPropagation();
            const tagId = tagLockIcon.getAttribute('data-tag-lock-toggle') || '';
            const action = getTagPassword(tagId) ? 'session-lock' : 'session-unlock';
            try {
              await handleTagLockAction(action, tagId);
            } catch (err) {
              showStatus(t('标签锁操作失败：') + err.message, 'err');
            }
            return;
          }
          const tagNameEl = e.target.closest('.tag-node-name[data-tag-id]');
          if (tagNameEl) {
            e.stopPropagation();
            const tagId = tagNameEl.getAttribute('data-tag-id') || '';
            if (!tagId) {
              return;
            }
            if (e.detail >= 2) {
              const meta = findTagMetaById(tagId);
              if (meta && canRenameTagNode(meta.node, meta.level)) {
                e.preventDefault();
                startTagRename(tagId);
              }
              return;
            }
            try {
              if (activeFilterTagId === tagId) {
                clearTagFileFilter();
              } else {
                if (!(await ensureTagUnlocked(tagId))) {
                  return;
                }
                await showFilesForTag(tagId);
              }
            } catch (err) {
              showStatus(t('加载标签文件失败：') + err.message, 'err');
            }
            return;
          }

          const deleteBtn = e.target.closest('.tag-inline-btn[data-tag-delete]');
          if (deleteBtn) {
            e.stopPropagation();
            const tagId = deleteBtn.getAttribute('data-tag-delete') || '';
            const meta = findTagMetaById(tagId);
            if (meta && isProtectedRestrictedRootTag(meta.node, meta.level)) {
              showStatus(t('受限一级标签不能删除'), 'err');
              renderTagTree();
              return;
            }
            if (!confirm(t('确认删除该标签节点及其子节点？仅会删除标签引用关系，不会删除文件。'))) {
              return;
            }
            const removedNode = await removeTagNode(tagId);
            if (!removedNode || removedNode.ok === false) {
              showStatus(t('删除标签失败：') + ((removedNode && removedNode.error) ? removedNode.error : t('节点不存在')), 'err');
              return;
            }
            expandedTagNodeIds.delete(tagId);
            if (activeFilterTagId === tagId) {
              clearTagFileFilter();
            }
            await loadTagTreeState();
            renderTagTree();
            showStatus(t('标签节点已删除（未删除任何文件）'), 'warn');
            return;
          }

          const createBtn = e.target.closest('.tag-inline-btn[data-tag-create][data-tag-level]');
          if (createBtn) {
            e.stopPropagation();
            const tagId = createBtn.getAttribute('data-tag-create') || '';
            const level = Number(createBtn.getAttribute('data-tag-level') || '0');
            if (level <= 0 || level >= TAG_MAX_LEVEL) {
              return;
            }

            const childName = await askTagName({
              title: t('新建子标签'),
              description: t('当前节点下最多支持三级标签。'),
              placeholder: t('请输入子标签名称')
            });
            if (childName === null) {
              return;
            }
            const addResult = await addTagNode(tagId, childName);
            if (!addResult.ok) {
              showStatus(t('创建子标签失败：') + addResult.message, 'err');
              return;
            }
            expandedTagNodeIds.add(tagId);
            await loadTagTreeState();
            renderTagTree();
            showStatus(t('子标签已创建'), 'ok');
            return;
          }

          const nodeToggleBtn = e.target.closest('.tag-node-toggle[data-tag-id]');
          if (nodeToggleBtn) {
            const tagId = nodeToggleBtn.getAttribute('data-tag-id') || '';

            const meta = findTagMetaById(tagId);
            if (!meta || !meta.node) {
              showStatus(t('节点不存在，可能已被删除'), 'err');
              return;
            }

            if (hasTagChildren(meta.node)) {
              if (expandedTagNodeIds.has(tagId)) {
                expandedTagNodeIds.delete(tagId);
              } else {
                expandedTagNodeIds.add(tagId);
              }
              renderTagTree();
              return;
            }
          }

          const unbindBtn = e.target.closest('.tag-unbind-btn[data-tag-id][data-file]');
          if (unbindBtn) {
            const tagId = unbindBtn.getAttribute('data-tag-id') || '';
            const fileName = decodeURIComponent(unbindBtn.getAttribute('data-file') || '');
            if (!(await unbindFileFromTag(tagId, fileName))) {
              showStatus(t('解引用失败：关联不存在'), 'err');
              return;
            }
            await loadTagTreeState();
            renderTagTree();
            if (activeFilterTagId === tagId) {
              await showFilesForTag(tagId);
            }
            showStatus(t('文件已解引用'), 'warn');
          }
        });

        tagManager.addEventListener('keydown', function (e) {
          const input = e.target.closest('.tag-rename-input[data-tag-rename-input]');
          if (!input) {
            return;
          }
          if (e.key === 'Escape') {
            e.preventDefault();
            cancelTagRename();
            return;
          }
          if (e.key === 'Enter') {
            e.preventDefault();
            submitTagRename(input);
          }
        });

        tagManager.addEventListener('focusout', function (e) {
          const input = e.target.closest('.tag-rename-input[data-tag-rename-input]');
          if (!input) {
            return;
          }
          submitTagRename(input);
        });

        tagManager.addEventListener('contextmenu', function (e) {
          if (e.target.closest('.tag-rename-input')) {
            return;
          }
          const tagNodeEl = e.target.closest('.tag-node[data-tag-id]');
          const tagNameEl = e.target.closest('.tag-node-name[data-tag-id], .tag-lock-inline[data-tag-lock-toggle]');
          if (!tagNameEl) {
            closeFileContextMenu();
            closeAudioTagContextMenu();
            return;
          }
          const tagId = tagNameEl.getAttribute('data-tag-id') || tagNameEl.getAttribute('data-tag-lock-toggle') || '';
          const meta = findTagMetaById(tagId);
          const isAudioConstraint = !!(tagId && getTagFileTypeConstraint(tagId) === 'audio');
          const isLockIconClick = !!e.target.closest('.tag-lock-inline[data-tag-lock-toggle]');
          if (isLockIconClick) {
            if (meta && canLockTagNode(meta.node, meta.level)) {
              e.preventDefault();
              e.stopPropagation();
              openTagLockContextMenu(tagId, !!meta.node.locked, e.clientX, e.clientY);
            }
            return;
          }
          if (!tagId || !isAudioConstraint || !tagNodeEl) {
            if (meta && canLockTagNode(meta.node, meta.level)) {
              e.preventDefault();
              e.stopPropagation();
              openTagLockContextMenu(tagId, !!meta.node.locked, e.clientX, e.clientY);
              return;
            }
            closeFileContextMenu();
            closeAudioTagContextMenu();
            return;
          }
          e.preventDefault();
          e.stopPropagation();
          const lockInfo = (meta && canLockTagNode(meta.node, meta.level))
            ? { locked: !!meta.node.locked }
            : null;
          openAudioTagContextMenu(tagId, String(tagNameEl.textContent || '').trim(), e.clientX, e.clientY, lockInfo);
        });

        tagManager.addEventListener('dragover', function (e) {
          const nodeEl = e.target.closest('.tag-node[data-tag-id]');
          if (!nodeEl) {
            clearDropHighlight();
            return;
          }
          const tagId = nodeEl.getAttribute('data-tag-id') || '';
          let fileNames = [];
          if (e.dataTransfer) {
            try {
              fileNames = JSON.parse(e.dataTransfer.getData('application/webcool-file-list') || '[]');
            } catch (_) {
              fileNames = [];
            }
            if (!fileNames.length) {
              const fallbackName = String(e.dataTransfer.getData('text/plain') || '');
              if (fallbackName) {
                fileNames = [fallbackName];
              }
            }
          }
          const invalidFile = fileNames.find(function (fileName) {
            return !canBindFileToTagOnClient(tagId, fileName).ok;
          }) || '';
          if (invalidFile) {
            clearDropHighlight();
            if (e.dataTransfer) {
              e.dataTransfer.dropEffect = 'none';
            }
            return;
          }
          e.preventDefault();
          if (e.dataTransfer) {
            e.dataTransfer.dropEffect = 'copy';
          }
          setDropHighlight(nodeEl);
        });

        tagManager.addEventListener('dragleave', function (e) {
          if (!tagManager.contains(e.relatedTarget)) {
            clearDropHighlight();
          }
        });

        tagManager.addEventListener('drop', async function (e) {
          const nodeEl = e.target.closest('.tag-node[data-tag-id]');
          let fileNames = [];
          if (e.dataTransfer) {
            try {
              fileNames = JSON.parse(e.dataTransfer.getData('application/webcool-file-list') || '[]');
            } catch (_) {
              fileNames = [];
            }
            if (!fileNames.length) {
              const fallbackName = String(e.dataTransfer.getData('text/plain') || '');
              if (fallbackName) {
                fileNames = [fallbackName];
              }
            }
          }
          clearDropHighlight();
          if (!nodeEl || !fileNames.length) {
            return;
          }
          e.preventDefault();

          const tagId = nodeEl.getAttribute('data-tag-id') || '';
          const invalidFile = fileNames.find(function (fileName) {
            return !canBindFileToTagOnClient(tagId, fileName).ok;
          }) || '';
          if (invalidFile) {
            showStatus(t('拖拽引用失败：') + canBindFileToTagOnClient(tagId, invalidFile).message, 'err');
            return;
          }
          const result = await bindFilesToTag(tagId, fileNames);
          if (!result.ok) {
            showStatus(t('拖拽引用失败：') + result.message, 'err');
            return;
          }

          fileNames.forEach(function (name) {
            selectedFileNames.delete(name);
          });
          await loadTagTreeState();
          renderTagTree();
          if (activeFilterTagId === tagId) {
            await showFilesForTag(tagId);
          } else {
            renderFiles(activeSourceFiles);
          }
          showStatus(fileNames.length > 1
            ? (t('已批量移动 ') + fileNames.length + t(' 个文件到标签'))
            : t('已通过拖拽移动文件到标签'), 'ok');
        });
      }

      document.addEventListener('click', function (e) {
        const quickTagCreateChild = e.target.closest('.quick-tag-create-child[data-quick-tag-create-child]');
        if (quickTagCreateChild && activeFileTagMenu && activeFileTagMenu.contains(quickTagCreateChild)) {
          const parentTagId = quickTagCreateChild.getAttribute('data-quick-tag-create-child') || '';
          const parentTagName = quickTagCreateChild.getAttribute('data-quick-tag-name') || '';
          const isLocalTagFiles = activeFileTagMenu.getAttribute('data-quick-tag-local') === '1';
          const fileNames = String(activeFileTagMenu.getAttribute('data-quick-tag-files') || '')
            .split('\n')
            .map(function (name) { return String(name || ''); })
            .filter(Boolean);
          closeFileTagMenu();
          askTagName({
            title: t('新建子标签'),
            description: t('创建在「') + parentTagName + t('」下，创建后会立即把当前文件加入该标签。'),
            placeholder: t('请输入子标签名称')
          }).then(async function (childName) {
            if (childName === null) {
              return;
            }
            const addResult = await addTagNode(parentTagId, childName);
            if (!addResult.ok) {
              showStatus(t('创建子标签失败：') + addResult.message, 'err');
              return;
            }
            const tagId = String(addResult.id || '');
            const bindResult = await bindFilesToTag(tagId, fileNames, { local: isLocalTagFiles });
            if (!bindResult.ok) {
              showStatus(t('加入标签失败：') + bindResult.message, 'err');
              return;
            }
            await loadTagTreeState();
            renderTagTree();
            if (activeFilterTagId === tagId) {
              await showFilesForTag(tagId);
            }
            showStatus(fileNames.length > 1 ? (t('已创建子标签并将 ') + fileNames.length + t(' 个文件加入标签')) : t('已创建子标签并加入文件'), 'ok');
          }).catch(function (err) {
            showStatus(t('创建子标签失败：') + err.message, 'err');
          });
          return;
        }

        const quickTagItem = e.target.closest('.quick-tag-item[data-quick-tag-id]');
        if (quickTagItem && activeFileTagMenu && activeFileTagMenu.contains(quickTagItem)) {
          const tagId = quickTagItem.getAttribute('data-quick-tag-id') || '';
          const isLocalTagFiles = activeFileTagMenu.getAttribute('data-quick-tag-local') === '1';
          const fileNames = String(activeFileTagMenu.getAttribute('data-quick-tag-files') || '')
            .split('\n')
            .map(function (name) { return String(name || ''); })
            .filter(Boolean);
          closeFileTagMenu();
          const check = canBindFilesToTagOnClient(tagId, fileNames);
          if (!check.ok) {
            showStatus(t('加入标签失败：') + check.message, 'err');
            return;
          }
          bindFilesToTag(tagId, fileNames, { local: isLocalTagFiles }).then(async function (result) {
            if (!result.ok) {
              showStatus(t('加入标签失败：') + result.message, 'err');
              return;
            }
            await loadTagTreeState();
            if (activeFilterTagId === tagId) {
              await showFilesForTag(tagId);
            } else {
              renderTagTree();
            }
            showStatus(fileNames.length > 1 ? (t('已将 ') + fileNames.length + t(' 个文件加入标签')) : t('文件已加入标签'), 'ok');
          }).catch(function (err) {
            showStatus(t('加入标签失败：') + err.message, 'err');
          });
          return;
        }

        if (e.target.closest('[data-file-summary-close]')) {
          const dialog = document.getElementById('file-summary-dialog');
          if (dialog && dialog.parentNode) {
            dialog.parentNode.removeChild(dialog);
          }
          return;
        }

        const folderMenuItem = e.target.closest('.folder-context-item[data-folder-menu-action]');
        if (folderMenuItem && activeFolderContextMenu && activeFolderContextMenu.contains(folderMenuItem)) {
          const menu = activeFolderContextMenu;
          const action = folderMenuItem.getAttribute('data-folder-menu-action') || '';
          const path = menu.getAttribute('data-folder-path') || '';
          closeFolderContextMenu();
          handleFolderContextAction(action, path).catch(function (err) {
            showStatus(t('目录操作失败：') + err.message, 'err');
          });
          return;
        }
        const fileMenuItem = e.target.closest('.folder-context-item[data-file-menu-action]');
        if (fileMenuItem && activeFileContextMenu && activeFileContextMenu.contains(fileMenuItem)) {
          const menu = activeFileContextMenu;
          const action = fileMenuItem.getAttribute('data-file-menu-action') || '';
          const path = menu.getAttribute('data-file-path') || '';
          const paths = String(menu.getAttribute('data-file-paths') || path).split('\n').filter(Boolean);
          const local = menu.getAttribute('data-file-local') === '1';
          const localDiskListMenu = menu.getAttribute('data-local-disk-list') === '1';
          closeFileContextMenu();
          if (action === 'summary') {
            if (localDiskListMenu) {
              showLocalDiskFileSummaryDialog(path);
            } else {
              showFileSummaryDialog(path);
            }
          } else if (action === 'image-enhance') {
            openImageEnhanceDialog(path, local);
          } else if (action === 'download') {
            downloadRemoteListFile(path, local);
          } else if (action === 'copy-local') {
            localDiskClipboardPaths = paths.slice();
            localDiskClipboardDirectoryFlags = paths.map(function () { return false; });
            localDiskClipboardPath = localDiskClipboardPaths[0] || '';
            localDiskClipboardDirectory = false;
            showStatus(paths.length > 1 ? (t('已拷贝 ') + paths.length + t(' 个本地文件')) : (t('已拷贝本地文件路径：') + path), 'ok');
          } else if (action === 'copy-remote') {
            remoteDiskClipboardPaths = paths.slice();
            remoteDiskClipboardDirectoryFlags = paths.map(function () { return false; });
            remoteDiskClipboardPath = remoteDiskClipboardPaths[0] || '';
            remoteDiskClipboardDirectory = false;
            showStatus(paths.length > 1 ? (t('已拷贝 ') + paths.length + t(' 个虚拟磁盘文件')) : (t('已拷贝远程文件路径：') + path), 'ok');
          } else if (action === 'upload-local') {
            openLocalImportDialog(paths).catch(function (err) {
              showStatus(t('打开上传目标选择失败：') + err.message, 'err');
            });
          } else if (action === 'rename') {
            if (localDiskListMenu) {
              startLocalDiskFileRename(path);
            } else {
              startFileRename(path);
            }
          } else if (action === 'delete') {
            if (localDiskListMenu) {
              Promise.resolve(paths.length > 1 ? removeSelectedLocalDiskFiles() : handleLocalDiskFileDeleteOrRemove(path)).catch(function (err) {
                showStatus(t('删除失败：') + err.message, 'err');
              });
            } else {
              Promise.resolve((paths.length > 1 && !local) ? (async function () {
                const activeTagId = activeFilterTagId;
                const isRecycleMode = !activeTagId && isRecycleFolderPath(activeFolderPath);
                const actionLabel = activeTagId ? t('移除') : (isRecycleMode ? t('彻底删除') : t('删除'));
                let confirmed = false;
                if (activeTagId) {
                  confirmed = await askDeleteConfirmDialog({
                    title: t('移除标签引用'),
                    description: t('此操作只解除标签引用，不会删除文件。'),
                    highlight: paths.length + t(' 个文件'),
                    confirmText: t('移除'),
                    variant: 'warn',
                    icon: '🏷'
                  });
                } else if (isRecycleMode) {
                  confirmed = await askDeleteConfirmDialog({
                    title: t('彻底删除'),
                    description: t('此操作不可恢复，请确认是否继续。'),
                    highlight: paths.length + t(' 个文件'),
                    confirmText: t('彻底删除')
                  });
                } else {
                  confirmed = await askDeleteConfirmDialog({
                    title: t('删除'),
                    description: t('选中的文件将先移入回收站。'),
                    highlight: paths.length + t(' 个文件'),
                    note: t('文件将移入回收站，不会立即永久删除。'),
                    confirmText: t('移入回收站')
                  });
                }
                if (!confirmed) {
                  return;
                }
                let completedCount = 0;
                resetStatus();
                if (activeTagId) {
                  for (let i = 0; i < paths.length; i += 1) {
                    const ok = await unbindFileFromTag(activeTagId, paths[i], { local: false });
                    if (!ok) {
                      throw new Error(t('关联不存在'));
                    }
                    completedCount += 1;
                  }
                  paths.forEach(function (name) { selectedFileNames.delete(name); });
                  await showFilesForTag(activeTagId);
                } else {
                  for (let i = 0; i < paths.length; i += 1) {
                    await fetchJson(appendFilePassword(withFolderPassword(api.del + '?file=' + encodeURIComponent(paths[i]), parentFolderPathFromFilePath(paths[i])), paths[i], false));
                    completedCount += 1;
                  }
                  paths.forEach(function (name) { selectedFileNames.delete(name); });
                  await loadFiles();
                }
                showStatus(t('已批量') + actionLabel + ' ' + completedCount + t(' 个文件'), 'warn');
              })() : handleFileDeleteOrRemove(path, local)).catch(function (err) {
                showStatus(t('删除失败：') + err.message, 'err');
              });
            }
          } else {
            handleFileContextAction(action, path, local).catch(function (err) {
              showStatus(t('文件锁操作失败：') + err.message, 'err');
            });
          }
          return;
        }
        const localDirMenuItem = e.target.closest('.folder-context-item[data-local-dir-menu-action]');
        if (localDirMenuItem && activeFileContextMenu && activeFileContextMenu.contains(localDirMenuItem)) {
          const menu = activeFileContextMenu;
          const action = localDirMenuItem.getAttribute('data-local-dir-menu-action') || '';
          const path = menu.getAttribute('data-local-dir-path') || '';
          const paths = String(menu.getAttribute('data-local-dir-paths') || path).split('\n').filter(Boolean);
          const locked = menu.getAttribute('data-local-dir-locked') === '1';
          closeFileContextMenu();
          handleLocalDirContextAction(action, path, locked, paths).catch(function (err) {
            showStatus(t('本地目录锁操作失败：') + err.message, 'err');
          });
          return;
        }
        const tagLockMenuItem = e.target.closest('.folder-context-item[data-tag-lock-action]');
        if (tagLockMenuItem && activeFileContextMenu && activeFileContextMenu.contains(tagLockMenuItem)) {
          const menu = activeFileContextMenu;
          const action = tagLockMenuItem.getAttribute('data-tag-lock-action') || '';
          const tagId = menu.getAttribute('data-tag-lock-id') || '';
          closeFileContextMenu();
          closeAudioTagContextMenu();
          handleTagLockAction(action, tagId).catch(function (err) {
            showStatus(t('标签锁操作失败：') + err.message, 'err');
          });
          return;
        }
        if (activeFolderContextMenu && !e.target.closest('.folder-context-menu')) {
          closeFolderContextMenu();
        }
        if (activeFileContextMenu && !e.target.closest('.file-context-menu')) {
          closeFileContextMenu();
        }

        if (activeAudioTagContextMenu && !e.target.closest('.tag-context-menu')) {
          closeAudioTagContextMenu();
        }

        if (activeFileTagMenu && !e.target.closest('.quick-tag-menu') && !e.target.closest('.file-tag-quick-btn')) {
          closeFileTagMenu();
        }

        if (tagDialog && !tagDialog.hidden) {
          const closeTarget = e.target.closest('[data-tag-dialog-close="1"]');
          if (closeTarget) {
            closeTagDialog(null);
            return;
          }
        }

        if (lockDialog && !lockDialog.hidden) {
          const closeTarget = e.target.closest('[data-lock-dialog-close="1"]');
          if (closeTarget) {
            closeLockDialog(null);
            return;
          }
        }

        if (confirmDialog && !confirmDialog.hidden) {
          const closeTarget = e.target.closest('[data-confirm-dialog-close="1"]');
          if (closeTarget) {
            closeConfirmDialog(false);
            return;
          }
        }
      });

      if (tagDialogForm) {
        tagDialogForm.addEventListener('submit', function (e) {
          e.preventDefault();
          closeTagDialog(tagDialogInput ? tagDialogInput.value : '');
        });
      }

      if (tagDialogCancelBtn) {
        tagDialogCancelBtn.addEventListener('click', function () {
          closeTagDialog(null);
        });
      }

      if (lockDialogForm) {
        lockDialogForm.addEventListener('submit', async function (e) {
          e.preventDefault();
          if (!activeLockDialogState) {
            return;
          }
          const password = lockDialogInput ? lockDialogInput.value : '';
          if (!password) {
            setLockDialogError(t('请输入锁密码'));
            if (lockDialogInput) {
              lockDialogInput.focus();
            }
            return;
          }
          setLockDialogError('');
          if (lockDialogConfirmBtn) {
            lockDialogConfirmBtn.disabled = true;
              lockDialogConfirmBtn.textContent = t('验证中...');
          }
          try {
            if (activeLockDialogState.onSubmit) {
              await activeLockDialogState.onSubmit(password);
            }
            closeLockDialog(password);
          } catch (err) {
            showStatus(activeLockDialogState.statusErrorMessage || t('解锁失败：密码错误或验证失败'), 'err');
            setLockDialogError(activeLockDialogState.errorMessage || t('密码错误或验证失败，请重新输入。'));
            if (lockDialogInput) {
              lockDialogInput.focus();
              lockDialogInput.select();
            }
          } finally {
            if (lockDialogConfirmBtn) {
              lockDialogConfirmBtn.disabled = false;
              lockDialogConfirmBtn.textContent = t('确认');
            }
          }
        });
      }

      if (lockDialogCancelBtn) {
        lockDialogCancelBtn.addEventListener('click', function () {
          closeLockDialog(null);
        });
      }

      if (confirmDialogCancelBtn) {
        confirmDialogCancelBtn.addEventListener('click', function () {
          closeConfirmDialog(false);
        });
      }

      if (confirmDialogExtraBtn) {
        confirmDialogExtraBtn.addEventListener('click', function () {
          closeConfirmDialog(confirmDialogExtraBtn.getAttribute('data-confirm-extra-value') || 'extra');
        });
      }

      if (confirmDialogExtra2Btn) {
        confirmDialogExtra2Btn.addEventListener('click', function () {
          closeConfirmDialog(confirmDialogExtra2Btn.getAttribute('data-confirm-extra-value') || 'extra2');
        });
      }

      if (confirmDialogExtra3Btn) {
        confirmDialogExtra3Btn.addEventListener('click', function () {
          closeConfirmDialog(confirmDialogExtra3Btn.getAttribute('data-confirm-extra-value') || 'extra3');
        });
      }

      if (confirmDialogConfirmBtn) {
        confirmDialogConfirmBtn.addEventListener('click', function () {
          closeConfirmDialog(true);
        });
      }

      document.addEventListener('keydown', function (e) {
        if (e.key === 'Escape' && confirmDialog && !confirmDialog.hidden) {
          e.preventDefault();
          closeConfirmDialog(false);
          return;
        }
        if (e.key === 'Escape' && adminStoragePickerDialog && !adminStoragePickerDialog.hidden) {
          e.preventDefault();
          adminStoragePickerTarget = 'storage';
          closeAdminStoragePickerDialog();
          return;
        }
        if (e.key === 'Escape' && lockDialog && !lockDialog.hidden) {
          e.preventDefault();
          closeLockDialog(null);
          return;
        }
        if (e.key === 'Escape' && activeAudioTagContextMenu) {
          e.preventDefault();
          closeAudioTagContextMenu();
          return;
        }
        if (e.key === 'Escape' && tagDialog && !tagDialog.hidden) {
          e.preventDefault();
          closeTagDialog(null);
          return;
        }
        if ((e.key === 'ArrowLeft' || e.key === 'ArrowRight') && !e.target.closest('input, textarea, select')) {
          const imageWin = getTopImagePreviewWindow();
          if (imageWin) {
            e.preventDefault();
            stepImagePreviewWindow(imageWin, e.key === 'ArrowLeft' ? -1 : 1);
          }
        }
      });

      document.addEventListener('scroll', function (e) {
        closeAudioTagContextMenu();
        if (activeFileTagMenu && activeFileTagMenu.contains(e.target)) {
          return;
        }
        closeFileTagMenu();
      }, true);

      document.body.addEventListener('click', async function (e) {
        const actionBtn = e.target.closest('.tag-context-item[data-audio-tag-action][data-tag-id]');
        if (!actionBtn) {
          return;
        }
        e.preventDefault();
        e.stopPropagation();
        const tagId = actionBtn.getAttribute('data-tag-id') || '';
        const mode = actionBtn.getAttribute('data-audio-tag-action') || '';
        const menuEl = actionBtn.closest('.tag-context-menu');
        const tagName = menuEl ? (menuEl.getAttribute('data-tag-name') || '') : '';
        closeAudioTagContextMenu();
        if (!tagId || !AUDIO_PLAY_MODE_LABELS[mode]) {
          return;
        }
        try {
          await startAudioPlaylistFromTag(tagId, tagName, mode);
        } catch (err) {
          showStatus(t('打开音频播放列表失败：') + err.message, 'err');
        }
      });

      document.addEventListener('mousemove', function (e) {
        if (!activeDrag || !activeDrag.win) {
          return;
        }

        const dx = e.clientX - activeDrag.startX;
        const dy = e.clientY - activeDrag.startY;
        activeDrag.win.style.left = Math.round(activeDrag.left + dx) + 'px';
        activeDrag.win.style.top = Math.round(activeDrag.top + dy) + 'px';
      });

      document.addEventListener('mouseup', function () {
        if (!activeDrag || !activeDrag.win) {
          return;
        }
        clampWindowPosition(activeDrag.win);
        activeDrag = null;
      });

      document.addEventListener('keydown', function (e) {
        if (e.key !== 'Escape') {
          return;
        }

        const wins = Array.from(previewLayer.querySelectorAll('.floating-preview'));
        if (!wins.length) {
          return;
        }

        wins.sort(function (a, b) {
          return (parseInt(a.style.zIndex || '0', 10) || 0)
            - (parseInt(b.style.zIndex || '0', 10) || 0);
        });

        closePreviewWindow(wins[wins.length - 1]);
      });

      window.addEventListener('resize', function () {
        const wins = previewLayer.querySelectorAll('.floating-preview');
        Array.prototype.forEach.call(wins, function (win) {
          clampWindowPosition(win);
          fitPreviewImageToWindow(win);
        });
      });

      if (sidebarToggleBtn) {
        sidebarToggleBtn.addEventListener('click', function () {
          const nextCollapsed = !(shell && shell.classList.contains('sidebar-collapsed'));
          setSidebarCollapsed(nextCollapsed);
          try {
            localStorage.setItem(SIDEBAR_COLLAPSED_STORAGE_KEY, nextCollapsed ? '1' : '0');
          } catch (err) {}
        });
      }

      try {
        setSidebarCollapsed(localStorage.getItem(SIDEBAR_COLLAPSED_STORAGE_KEY) === '1');
      } catch (err) {
        setSidebarCollapsed(false);
      }

      menuButtons.forEach(function (btn) {
        btn.addEventListener('click', function () {
          const panelId = btn.getAttribute('data-panel');
          if ((panelId === 'panel-admin' || panelId === 'panel-users') && !authState.admin) {
            return;
          }
          if (panelId === 'panel-files') {
            activeFilterTagId = '';
          } else if (panelId === 'panel-local-disk') {
            activeFilterTagId = '';
          } else if (panelId === 'panel-users') {
            activeFilterTagId = '';
            loadAdminUsers();
          } else if (panelId === 'panel-account') {
            activeFilterTagId = '';
            activateAccountView('password');
            syncFontSizePreferenceForCurrentUser();
            if (accountOldPassword) {
              accountOldPassword.value = '';
            }
            if (accountNewPassword) {
              accountNewPassword.value = '';
            }
            if (accountConfirmPassword) {
              accountConfirmPassword.value = '';
            }
          }
          if (panelId) {
            activatePanel(panelId);
          }
        });
      });

      applyAuthUi();

      refreshAuthStatus()
        .then(function () {
          if (!authState.authenticated) {
            if (authUsername) {
              authUsername.focus();
            }
            return null;
          }
          activatePanel('panel-files', { skipLoadFiles: true });
          return loadFiles().then(function () {
            if (authState.admin) {
              return loadAdminUsers();
            }
            return null;
          });
        })
        .catch(function (err) {
          setAuthError(err.message || t('认证状态检查失败'));
          requireLoginAgain();
        });

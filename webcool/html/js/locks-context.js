function loadUnlockedFilePasswords() {
        try {
          const raw = sessionStorage.getItem(FILE_UNLOCK_SESSION_STORAGE_KEY);
          const entries = raw ? JSON.parse(raw) : [];
          if (!Array.isArray(entries)) {
            return;
          }
          entries.forEach(function (entry) {
            if (Array.isArray(entry) && entry[0] && entry[1]) {
              unlockedFilePasswords.set(String(entry[0]), String(entry[1]));
            }
          });
        } catch (_) {}
      }

      async function ensureLocalDirUnlocked(path) {
        const dirPath = String(path || '');
        if (!dirPath) {
          return true;
        }
        if (getLocalDirPassword(dirPath)) {
          return true;
        }
        const password = await askLockPassword({
          title: t('解锁本地目录'),
          description: t('请输入本地目录「') + dirPath + t('」的锁密码。'),
          onSubmit: async function (passwordText) {
            await fetchJson(api.fileLockVerify + '?local=1&dir=1&path=' + encodeURIComponent(dirPath) + '&password=' + encodeURIComponent(passwordText), { method: 'POST' });
          }
        });
        if (password === null) {
          return false;
        }
        setUnlockedLocalDirPassword(dirPath, password);
        return true;
      }

      function loadLegacyTagTreeState() {
        try {
          const raw = localStorage.getItem(TAG_TREE_STORAGE_KEY);
          if (!raw) {
            return [];
          }

          const parsed = JSON.parse(raw);
          if (!Array.isArray(parsed)) {
            return [];
          }

          const normalized = [];
          for (let i = 0; i < parsed.length; i += 1) {
            const node = normalizeTagNode(parsed[i], 1);
            if (node) {
              normalized.push(node);
            }
          }
          return normalized;
        } catch (_) {
          return [];
        }
      }

      function setTranscodeVisualState(encodedName, state) {
        const item = statusBox.querySelector('[data-transcode-item="' + encodedName + '"]');
        const fill = statusBox.querySelector('[data-progress-fill="' + encodedName + '"]');
        if (item) {
          item.classList.remove('state-running', 'state-done', 'state-failed', 'state-cancelled');
          if (state) {
            item.classList.add('state-' + state);
          }
        }
        if (fill) {
          fill.classList.remove('state-done', 'state-failed', 'state-cancelled');
          if (state === 'done' || state === 'failed' || state === 'cancelled') {
            fill.classList.add('state-' + state);
          }
        }
      }

      function updateLocalImportProgressSummary(text) {
        const desc = document.getElementById('local-import-progress-desc');
        if (!desc || !isCopyTaskWindowMode(localImportProgressWindowMode) || !localImportProgressMinimized) {
          return;
        }
        desc.textContent = text || t('粘贴中');
      }

      function getTopImagePreviewWindow() {
        if (!previewLayer) {
          return null;
        }
        return Array.from(previewLayer.querySelectorAll('.floating-preview')).filter(function (win) {
          return win && win.isConnected && Array.isArray(win.__imageGallery) && win.__imageGallery.length > 1;
        }).sort(function (a, b) {
          return Number(b.style.zIndex || 0) - Number(a.style.zIndex || 0);
        })[0] || null;
      }

      function getSelectedLocalDiskFilePaths() {
        return activeLocalDiskItems.filter(function (item) {
          return item && !item.directory && selectedLocalDiskPaths.has(String(item.path || ''));
        }).map(function (item) {
          return String(item.path || '');
        }).filter(Boolean);
      }

      function stopAdminStorageProgressPolling() {
        if (adminStorageProgressTimer) {
          clearInterval(adminStorageProgressTimer);
          adminStorageProgressTimer = null;
        }
        activeAdminStorageMigrateTaskId = '';
        setAdminStorageProgressControls('done');
      }

      function getLocalDirPasswordForPath(path) {
        const text = String(path || '');
        if (!text) {
          return '';
        }
        const parts = text.split('/').filter(Boolean);
        for (let i = parts.length; i >= 0; i -= 1) {
          const candidate = i === 0 ? '/' : ('/' + parts.slice(0, i).join('/'));
          const password = getLocalDirPassword(candidate);
          if (password) {
            return password;
          }
        }
        return '';
      }

      function setUploadTargetFolder(path) {
        if (uploadFolderPathInput) {
          uploadFolderPathInput.value = String(path || '');
        }
        updateRemoteUploadTargetHints();
      }

      function updateRemoteUploadTargetHints() {
        const stagingCount = pendingRemoteUploadBrowserFiles.length + pendingRemoteUploadLocalPaths.length;
        const folder = stagingCount > 0
          ? String(pendingRemoteUploadTargetFolder || '')
          : String(activeFolderPath || '');
        const folderLabel = getFolderLabel(folder);
        const canUpload = canUploadLocalDiskToRemoteFolder(folder);

        if (localDiskRemoteUploadTarget) {
          if (!canUpload && stagingCount === 0) {
            localDiskRemoteUploadTarget.textContent = t('请先在「虚拟磁盘」中选择目标文件夹');
            if (localDiskRemoteUploadZone) {
              localDiskRemoteUploadZone.classList.add('is-disabled');
            }
          } else {
            if (localDiskRemoteUploadZone) {
              localDiskRemoteUploadZone.classList.remove('is-disabled');
            }
            localDiskRemoteUploadTarget.textContent = t('上传到：') + folderLabel;
          }
        }

        if (remoteBrowserUploadTarget) {
          remoteBrowserUploadTarget.textContent = canUpload || stagingCount > 0
            ? (t('当前目录：') + folderLabel)
            : t('请先在左侧目录树中选择目标文件夹');
        }
        if (remoteBrowserUploadZone) {
          remoteBrowserUploadZone.classList.toggle('is-disabled', !canUpload && stagingCount === 0);
        }
      }

      function updateRemoteLocalUploadTargetHint() {
        updateRemoteUploadTargetHints();
      }

      function renderTagTree() {
        if (!tagManager) {
          return;
        }

        updateFilesTagToggleButton();
        if (!tagTree.length) {
          tagManager.classList.remove('open');
          tagManager.innerHTML = '';
          return;
        }

        const treeHtml = '<div class="tag-tree">' + tagTree.map(function (node) {
          return buildTagNodeHtml(node, 1);
        }).join('') + '</div>';

        tagManager.classList.add('open');
        tagManager.innerHTML = treeHtml;
        focusActiveTagRenameInput();
      }

      function stopTranscodePolling(encodedName) {
        const timer = transcodeProgressTimers.get(encodedName);
        if (timer) {
          clearInterval(timer);
          transcodeProgressTimers.delete(encodedName);
        }
      }

      function failLocalImportProgress(text) {
        setLocalImportProgress(0, text || t('上传失败'));
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

      function currentImageGalleryPreviewKey() {
        if (activeFilterTagId) {
          return 'image-gallery:tag:' + String(activeFilterTagId || 'default');
        }
        return 'image-gallery:folder:' + String(activeFolderPath || 'root');
      }

      function updateLocalDiskSelection(path, checked) {
        if (!path) {
          return;
        }
        if (checked) {
          selectedLocalDiskPaths.add(path);
        } else {
          selectedLocalDiskPaths.delete(path);
        }
        updateLocalDiskBulkRemoveButton();
      }

      async function jumpAdminStoragePickerPath(path) {
        const target = String(path || '/');
        try {
          adminStoragePickerRootPath = '';
          adminStoragePickerSelectedPath = '';
          adminStoragePickerCache.clear();
          adminStoragePickerExpandedPaths.clear();
          const rootPath = await loadAdminStoragePickerPath(target);
          adminStoragePickerRootPath = rootPath;
          adminStoragePickerSelectedPath = rootPath;
          adminStoragePickerExpandedPaths.add(rootPath);
          renderAdminStoragePicker();
        } catch (err) {
          showStatus(t('加载本地目录树失败：') + err.message, 'err');
        }
      }

      function setUnlockedLocalDirPassword(path, password) {
        unlockedFilePasswords.set(localDirLockKey(path), String(password || ''));
        saveUnlockedFilePasswords();
      }

      function setSidebarCollapsed(collapsed) {
        if (!shell || !sidebarToggleBtn) {
          return;
        }
        const isCollapsed = !!collapsed;
        shell.classList.toggle('sidebar-collapsed', isCollapsed);
        sidebarToggleBtn.textContent = isCollapsed ? '▶' : '◀';
        sidebarToggleBtn.setAttribute('aria-label', isCollapsed ? t('展开左侧栏') : t('收起左侧栏'));
        sidebarToggleBtn.setAttribute('title', isCollapsed ? t('展开左侧栏') : t('收起左侧栏'));
      }

      function canBindFilesToTagOnClient(tagId, fileNames) {
        const names = Array.isArray(fileNames) ? fileNames : [];
        for (let i = 0; i < names.length; i += 1) {
          const check = canBindFileToTagOnClient(tagId, names[i]);
          if (!check.ok) {
            return check;
          }
        }
        return { ok: true, message: '' };
      }

      async function pollTranscodeProgress(encodedName, taskId) {
        try {
          const data = await fetchJson(api.convertProgress + '?task_id=' + encodeURIComponent(taskId));
          const progress = Number(data.progress || 0);
          updateTranscodeProgress(encodedName, progress, (data.message || t('转码中')) + ' ' + Math.max(0, Math.min(100, Math.round(progress))) + '%');

          if (data.done) {
            stopTranscodePolling(encodedName);
            setTranscodeTaskId(encodedName, '');
            if (data.success) {
              setTranscodeVisualState(encodedName, 'done');
              updateTranscodeProgress(encodedName, 100, t('已完成'));
              const outputs = [String(data.name || decodeURIComponent(encodedName))];
              if (data.secondary_name) {
                outputs.push(String(data.secondary_name));
              }
              setTranscodeReason(encodedName, t('状态：已完成，输出文件 ') + outputs.join(' , '));
              setTranscodeButtons(encodedName, { startDisabled: true, cancelDisabled: true, confirmVisible: true });
              if (data.local) {
                await loadLocalDisk(activeLocalDiskPath || localDiskParentPath(String(data.name || '')) || '');
              } else {
                await loadFiles();
              }
            } else {
              const cancelled = data.cancel_requested || String(data.message || '').indexOf('取消') >= 0;
              setTranscodeVisualState(encodedName, cancelled ? 'cancelled' : 'failed');
              updateTranscodeProgress(encodedName, progress, cancelled ? t('已取消') : t('失败'));
              setTranscodeReason(encodedName, cancelled
                ? t('状态：已取消')
                : t('状态：失败，') + String(data.error || data.message || t('未知错误')));
              setTranscodeButtons(encodedName, { startDisabled: false, cancelDisabled: true });
            }
          }
        } catch (err) {
          stopTranscodePolling(encodedName);
          setTranscodeTaskId(encodedName, '');
          setTranscodeVisualState(encodedName, 'failed');
          updateTranscodeProgress(encodedName, 0, t('进度查询失败'));
          setTranscodeReason(encodedName, t('状态：进度查询失败，') + err.message);
          setTranscodeButtons(encodedName, { startDisabled: false, cancelDisabled: true });
        }
      }

      function closeLocalImportProgressDialog() {
        if (localImportProgressDialog) {
          localImportProgressDialog.hidden = true;
          localImportProgressDialog.classList.remove('non-modal-progress');
        }
        activeRemoteCopyCancelRequested = false;
        localImportProgressWindowMode = '';
        localImportProgressMinimized = false;
        applyLocalImportProgressWindowState();
        if (localImportProgressFill) {
          localImportProgressFill.style.width = '0%';
        }
        if (localImportProgressText) {
          localImportProgressText.textContent = t('准备上传...');
        }
        if (localImportProgressFiles) {
          localImportProgressFiles.innerHTML = '';
        }
        if (localImportProgressPause) {
          localImportProgressPause.hidden = true;
          localImportProgressPause.disabled = false;
        }
        if (localImportProgressResume) {
          localImportProgressResume.hidden = true;
          localImportProgressResume.disabled = false;
        }
        if (localImportProgressCancel) {
          localImportProgressCancel.hidden = true;
          localImportProgressCancel.disabled = false;
          localImportProgressCancel.textContent = t('取消');
        }
        activeLocalImportTaskId = '';
        activeLocalImportCancelRequested = false;
      }

      function openCurrentFileImagePreview(filePath, displayName) {
        const rawPath = String(filePath || '');
        if (!rawPath) {
          return;
        }
        const gallery = buildImageGalleryFromFiles(getSortedCurrentFiles());
        const index = imageGalleryIndexOf(gallery, rawPath);
        const current = gallery[index] || { file: rawPath, name: displayName || rawPath, local: false };
        const opts = {
          previewKey: currentImageGalleryPreviewKey(),
          gallery: gallery.length ? gallery : [current],
          galleryIndex: index,
          local: !!current.local
        };
        if (current.local) {
          opts.url = localDiskDownloadUrl(rawPath);
        }
        openPreview('image', encodeURIComponent(rawPath), displayName || current.name || rawPath, opts);
      }

      function clearLocalDiskSelection() {
        selectedLocalDiskPaths.clear();
        updateLocalDiskBulkRemoveButton();
      }

      function jumpAdminStoragePickerRoot() {
        jumpAdminStoragePickerPath('/');
      }

      function normalizeFileRecord(file) {
        const source = file && typeof file === 'object' ? file : {};
        const path = String(source.path || source.name || '');
        const slash = path.lastIndexOf('/');
        const derivedFolder = slash >= 0 ? path.slice(0, slash) : '';
        const derivedName = slash >= 0 ? path.slice(slash + 1) : path;
        return Object.assign({}, source, {
          path: path,
          folder_path: String(source.folder_path || derivedFolder || ''),
          name: String(source.name || derivedName || ''),
          display_path: path,
          directory: !!source.directory,
          locked: !!source.locked
        });
      }

      function toVttSidecarName(name) {
        const text = String(name || '');
        const slash = Math.max(text.lastIndexOf('/'), text.lastIndexOf('\\\\'));
        const dot = text.lastIndexOf('.');
        if (dot <= slash) {
          return text + '.vtt';
        }
        return text.slice(0, dot) + '.vtt';
      }

      function setDropHighlight(nodeEl) {
        if (activeDropTagNode && activeDropTagNode !== nodeEl) {
          activeDropTagNode.classList.remove('drop-target');
        }
        activeDropTagNode = nodeEl || null;
        if (activeDropTagNode) {
          activeDropTagNode.classList.add('drop-target');
        }
      }

      function safeSize(file) {
        const n = Number(file.size || 0);
        return Number.isFinite(n) ? n : 0;
      }

      async function uploadLocalDiskPathsToRemoteFolder(paths, targetFolder) {
        const cleanPaths = (Array.isArray(paths) ? paths : [])
          .map(function (path) { return String(path || ''); })
          .filter(Boolean);
        if (!cleanPaths.length) {
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
        const password = getFolderPasswordForPath(folder);
        resetStatus();
        const title = document.getElementById('local-import-progress-title');
        const desc = document.getElementById('local-import-progress-desc');
        setLocalImportProgressWindowMode('local-import');
        if (localImportProgressDialog) {
          localImportProgressDialog.classList.remove('non-modal-progress');
        }
        if (title) {
          title.textContent = t('上传中');
        }
        if (desc) {
          desc.textContent = t('正在上传本地文件到虚拟磁盘。');
        }
        activeLocalImportTaskId = '';
        activeLocalImportCancelRequested = false;
        if (localImportProgressPause) {
          localImportProgressPause.hidden = true;
          localImportProgressPause.disabled = false;
        }
        if (localImportProgressResume) {
          localImportProgressResume.hidden = true;
          localImportProgressResume.disabled = false;
        }
        if (localImportProgressCancel) {
          localImportProgressCancel.hidden = true;
          localImportProgressCancel.disabled = false;
          localImportProgressCancel.textContent = t('取消');
        }
        let url = api.localDiskImport + '?folder=' + encodeURIComponent(folder);
        if (password) {
          url += '&folder_password=' + encodeURIComponent(password);
        }
        const started = await fetchJson(url, {
          method: 'POST',
          headers: { 'Content-Type': 'text/plain; charset=utf-8' },
          body: cleanPaths.join('\n')
        });
        activeLocalImportTaskId = String(started.task_id || '');
        setLocalImportProgressControls('running');
        setLocalImportProgress(0, t('准备上传...'));
        const data = await pollLocalImportProgress(activeLocalImportTaskId);
        clearLocalDiskSelection();
        await loadFiles();
        showStatus(t('已上传 ') + Number(data.saved_count || 0) + t(' 个本地文件到虚拟磁盘'), 'ok');
        const videoProbeFails = await verifyUploadedVideos(data.files);
        if (videoProbeFails.length) {
          await promptUploadedTranscodes(videoProbeFails);
        }
      }

      async function confirmLocalImport() {
        if (Array.isArray(pendingBrowserUploadFiles) && pendingBrowserUploadFiles.length) {
          if (typeof confirmBrowserUploadFromLocalImportDialog === 'function') {
            await confirmBrowserUploadFromLocalImportDialog();
          }
          return;
        }
        const paths = Array.isArray(localImportOverridePaths) ? localImportOverridePaths : getSelectedLocalDiskImportPaths();
        if (!paths.length) {
          closeLocalImportDialog();
          return;
        }
        if (localImportConfirmBtn) {
          localImportConfirmBtn.disabled = true;
        }
        try {
          await uploadLocalDiskPathsToRemoteFolder(paths, localImportTargetFolderPath);
          closeLocalImportDialog();
        } catch (err) {
          failLocalImportProgress(t('上传失败：') + err.message);
          showStatus(t('上传本地文件失败：') + err.message, 'err');
        } finally {
          localImportOverridePaths = null;
          if (localImportConfirmBtn) {
            localImportConfirmBtn.disabled = false;
          }
        }
      }

      function canvasToBlob(canvas, type, quality) {
        return new Promise(function (resolve, reject) {
          canvas.toBlob(function (blob) {
            if (blob) {
              resolve(blob);
            } else {
              reject(new Error('生成图片失败'));
            }
          }, type || 'image/png', quality || 0.92);
        });
      }

      function cacheLocalDiskTreeNode(path, items) {
        const dirs = (Array.isArray(items) ? items : []).filter(function (item) {
          return !!(item && item.directory);
        }).sort(function (a, b) {
          return String((a && a.name) || '').localeCompare(String((b && b.name) || ''), 'zh-CN');
        });
        localDiskTreeCache.set(String(path || '/'), dirs);
      }

      function clearScheduledVideoResumeSave(fileName) {
        const key = String(fileName || '');
        if (!key) {
          return;
        }

        const existed = videoResumeSaveTimers.get(key);
        if (!existed) {
          return;
        }

        clearTimeout(existed.timer);
        videoResumeSaveTimers.delete(key);
      }

      function isSameOrChildFolderPath(basePath, testPath) {
        const base = String(basePath || '');
        const test = String(testPath || '');
        if (!base) {
          return true;
        }
        return test === base || test.indexOf(base + '/') === 0;
      }

      function highlightCodeText(text, lang) {
        const escaped = escapeHtml(text || '');
        const langKeywords = {
          c: ['if', 'else', 'switch', 'case', 'default', 'break', 'continue', 'return', 'for', 'while', 'do', 'typedef', 'struct', 'enum', 'union', 'const', 'static', 'extern', 'volatile', 'unsigned', 'signed', 'sizeof', 'void', 'char', 'short', 'int', 'long', 'float', 'double'],
          cpp: ['if', 'else', 'switch', 'case', 'default', 'break', 'continue', 'return', 'for', 'while', 'do', 'class', 'struct', 'enum', 'namespace', 'template', 'typename', 'using', 'public', 'private', 'protected', 'virtual', 'override', 'const', 'static', 'inline', 'new', 'delete', 'try', 'catch', 'throw', 'auto', 'nullptr', 'this', 'true', 'false', 'void', 'char', 'short', 'int', 'long', 'float', 'double', 'bool'],
          java: ['if', 'else', 'switch', 'case', 'default', 'break', 'continue', 'return', 'for', 'while', 'do', 'class', 'interface', 'enum', 'package', 'import', 'public', 'private', 'protected', 'static', 'final', 'abstract', 'extends', 'implements', 'new', 'this', 'super', 'try', 'catch', 'finally', 'throw', 'throws', 'void', 'boolean', 'byte', 'short', 'int', 'long', 'float', 'double', 'char', 'null', 'true', 'false'],
          javascript: ['if', 'else', 'switch', 'case', 'default', 'break', 'continue', 'return', 'for', 'while', 'do', 'function', 'class', 'extends', 'const', 'let', 'var', 'new', 'this', 'try', 'catch', 'finally', 'throw', 'import', 'export', 'from', 'async', 'await', 'true', 'false', 'null', 'undefined'],
          typescript: ['if', 'else', 'switch', 'case', 'default', 'break', 'continue', 'return', 'for', 'while', 'do', 'function', 'class', 'extends', 'implements', 'interface', 'type', 'enum', 'namespace', 'const', 'let', 'var', 'new', 'this', 'public', 'private', 'protected', 'readonly', 'import', 'export', 'from', 'async', 'await', 'true', 'false', 'null', 'undefined'],
          python: ['if', 'elif', 'else', 'for', 'while', 'break', 'continue', 'return', 'def', 'class', 'import', 'from', 'as', 'try', 'except', 'finally', 'raise', 'with', 'lambda', 'pass', 'yield', 'global', 'nonlocal', 'True', 'False', 'None'],
          go: ['if', 'else', 'switch', 'case', 'default', 'fallthrough', 'for', 'range', 'break', 'continue', 'return', 'func', 'type', 'struct', 'interface', 'map', 'chan', 'select', 'go', 'defer', 'package', 'import', 'const', 'var', 'nil', 'true', 'false'],
          sql: ['select', 'from', 'where', 'insert', 'into', 'values', 'update', 'set', 'delete', 'create', 'table', 'alter', 'drop', 'join', 'left', 'right', 'inner', 'outer', 'on', 'and', 'or', 'not', 'null', 'as', 'group', 'by', 'order', 'limit', 'having', 'distinct', 'union'],
          shell: ['if', 'then', 'else', 'fi', 'for', 'in', 'do', 'done', 'while', 'case', 'esac', 'function', 'return', 'break', 'continue', 'export', 'local'],
          proto: ['syntax', 'package', 'import', 'option', 'message', 'enum', 'service', 'rpc', 'returns', 'repeated', 'optional', 'required', 'oneof', 'map', 'reserved']
        };

        const list = langKeywords[lang];
        if (!list || !list.length) {
          return escaped;
        }

        const pattern = new RegExp('\\b(' + list.map(escapeRegExp).join('|') + ')\\b', 'g');
        return escaped.replace(pattern, '<span class="kw">$1</span>');
      }

      function isActiveTagImageType() {
        return !!(activeFilterTagId && getTagFileTypeConstraint(activeFilterTagId) === 'image');
      }

      function clearFolderAutoExpandTimer() {
        if (folderAutoExpandTimer) {
          clearTimeout(folderAutoExpandTimer);
          folderAutoExpandTimer = null;
        }
        activeFolderAutoExpandPath = '';
      }

      function getFileRecordByPath(fileName) {
        const target = String(fileName || '');
        return (Array.isArray(currentFiles) ? currentFiles : []).find(function (file) {
          return getFilePath(file) === target;
        }) || null;
      }

      function scalePreviewImage(win, factor) {
        const base = previewBaseCanvas(win);
        if (!base) {
          setImageEditHint(win, '图片还没有加载完成，请稍后再试。', true);
          return;
        }
        const minScale = previewImageFitScale(win);
        const currentScale = Math.max(minScale, Math.min(1, Number(win.__imageDisplayScale || minScale || 1)));
        const nextScale = Math.max(minScale, Math.min(1, currentScale * Number(factor || 1)));
        const nextWidth = Math.max(1, Math.round(base.width * nextScale));
        const nextHeight = Math.max(1, Math.round(base.height * nextScale));
        win.__imageUserZoom = true;
        setPreviewImageDisplayScale(
          win,
          nextScale,
          (Number(factor || 1) > 1 ? '已等比例放大至 ' : '已等比例缩小至 ') + nextWidth + ' x ' + nextHeight + '。'
        );
      }

      function updateLocalDiskSortIndicator() {
        localSortButtons.forEach(function (btn) {
          const key = btn.getAttribute('data-local-sort-key') || '';
          const arrow = btn.querySelector('.arrow');
          btn.classList.toggle('active', key === localDiskSortKey);
          if (arrow) {
            arrow.textContent = key === localDiskSortKey ? (localDiskSortOrder === 'desc' ? '↓' : '↑') : '-';
          }
        });
      }

      function handleLocalDiskFileDoubleClickEvent(e) {
        const fileNameClick = e.target.closest('.local-disk-file-name-action[data-local-disk-file-name-click]');
        if (!fileNameClick) {
          return;
        }
        e.preventDefault();
        e.stopPropagation();
        handleLocalDiskFileNameDoubleClick(decodeURIComponent(fileNameClick.getAttribute('data-local-disk-file-name-click') || ''));
      }

      function withFolderPassword(url, path, paramName) {
        const password = getFolderPasswordForPath(path);
        if (!password) {
          return url;
        }
        return url + '&' + encodeURIComponent(paramName || 'folder_password') + '=' + encodeURIComponent(password);
      }

      async function loadAudioFilesForTag(tagId) {
        const data = await fetchJson(appendTagPassword(api.tagFiles + '?tag_id=' + encodeURIComponent(tagId), tagId));
        return (Array.isArray(data.files) ? data.files : []).filter(function (file) {
          return isAudioName(file && file.name);
        });
      }

      function isDocumentName(name) {
        return /\.(pdf|doc|docx|xls|xlsx|ppt|pptx|odt|ods|odp|rtf|xmind|txt|md|markdown|mdown|mkdn|log|csv|tsv|json|xml|yaml|yml|ini|conf|html|htm|css|js|ts|c|h|cc|cpp|cxx|hpp|java|py|go|sh|sql|proto)$/i.test(String(name || ''));
      }

      function canBindFileToTagOnClient(tagId, fileName) {
        const constraint = getTagFileTypeConstraint(tagId);
        if (!constraint) {
          return { ok: true, message: '' };
        }
        if (constraint === 'video' && !isVideoName(fileName)) {
          return { ok: false, message: t('视频标签及其子标签只能引用视频文件（mp4/avi/mkv/rm/rmvb/mov/wmv/mpg/mpeg）') };
        }
        if (constraint === 'audio' && !isAudioName(fileName)) {
          return { ok: false, message: t('音频标签及其子标签只能引用音频文件（mp3/m4a/aac/wav/ogg/flac）') };
        }
        if (constraint === 'image' && !isImageName(fileName)) {
          return { ok: false, message: t('图片标签及其子标签只能引用图片文件（png/jpg/jpeg/gif/heic/heif）') };
        }
        if (constraint === 'document' && !isDocumentName(fileName)) {
          return { ok: false, message: t('文档标签及其子标签只能引用文档文件（pdf/office/xmind/markdown/txt/code）') };
        }
        return { ok: true, message: '' };
      }

      function startFolderRename(path) {
        const target = String(path || '');
        if (!canRenameFolderPath(target)) {
          return;
        }
        activeFolderRenamePath = target;
        ensureFolderPathExpanded(target);
        renderFolderTree();
      }

      function startFileRename(filePath) {
        const record = getFileRecordByPath(filePath);
        if (!record || !canRenameFileRecord(record)) {
          return;
        }
        clearFileRenameClickTimer();
        activeFileRenamePath = filePath;
        fileRenameRequestPath = filePath;
        renderFiles(activeSourceFiles);
        window.setTimeout(function () {
          const input = fileList && fileList.querySelector('.file-rename-input[data-file-rename-path="' + encodeURIComponent(filePath) + '"]');
          if (input) {
            selectRenameInputText(input);
          }
        }, 0);
      }

      function buildLocalDiskFileRowHtml(item) {
        const path = String((item && item.path) || '');
        const name = localDiskDisplayName(path, item && item.name);
        const encodedPath = encodeURIComponent(path);
        const checked = selectedLocalDiskPaths.has(path) ? ' checked' : '';
        const selectedClass = selectedLocalDiskPaths.has(path) ? ' selected-file-row' : '';
        const fileLocked = !!(item && item.locked);
        const lockIcon = fileLocked
          ? '<span class="folder-lock-icon file-lock-inline' + (getFilePassword(path, true) ? ' unlocked' : '') + '" title="' + (getFilePassword(path, true) ? '点击重新加锁' : '点击解锁') + '" aria-label="' + (getFilePassword(path, true) ? '点击重新加锁' : '点击解锁') + '"><span class="folder-lock-shackle"></span><span class="folder-lock-body"></span></span>'
          : '';
        const selectBox = '<span class="file-select-tools"><input class="local-disk-select" type="checkbox" data-local-select="' + encodedPath + '" aria-label="' + escapeHtml(t('选择 ') + name) + '"' + checked + '><button class="file-tag-quick-btn local-file-tag-btn" type="button" data-local-tag-file="' + encodedPath + '" title="' + escapeHtml(t('加入标签')) + '" aria-label="' + escapeHtml(t('加入标签')) + '">🏷</button></span>';
        const isRenamingLocalDiskItem = activeLocalDiskRenamePath === path;
        const renameInput = isRenamingLocalDiskItem
          ? '<input class="file-rename-input local-disk-rename-input" type="text" draggable="false" value="' + escapeHtml(name) + '" data-local-disk-rename-path="' + encodedPath + '" aria-label="' + escapeHtml(t('改名文件')) + '">'
          : '';
        const displayName = selectBox + (renameInput || '<button type="button" class="file-name file-name-action local-disk-file-name-action local-disk-draggable-name" draggable="false" data-local-disk-file-name-click="' + encodedPath + '">' + escapeHtml(name) + '</button>') + lockIcon;
        const previewBtn = isImageName(name)
          ? '<button class="local-preview-btn preview-btn" data-kind="image" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '">预览</button>'
          : '';
        const videoBtn = isVideoName(name)
          ? '<button class="local-preview-btn video-btn" data-kind="video" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '">观影</button>'
          : '';
        const audioBtn = isAudioName(name)
          ? '<button class="local-preview-btn audio-btn" data-kind="audio" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '">听音</button>'
          : '';
        const textBtn = isTextName(name)
          ? '<button class="local-preview-btn text-btn" data-kind="text" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '">查看</button>'
          : '';
        const pdfBtn = isPdfName(name)
          ? '<button class="local-preview-btn preview-btn" data-kind="pdf" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '">预览</button>'
          : '';
        const officeBtn = isOfficeName(name)
          ? '<button class="local-preview-btn preview-btn" data-kind="office" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '">预览</button>'
          : '';
        const xmindBtn = isXMindName(name)
          ? '<button class="local-preview-btn preview-btn" data-kind="mindmap" data-local-file="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '">预览</button>'
          : '';
        const deleteBtn = '<button class="local-delete-btn delete-btn" data-local-delete="' + encodedPath + '" data-local-name="' + escapeHtml(path) + '" title="移至回收站" aria-label="移至回收站">移除</button>';
        return (
          '<tr class="local-disk-draggable' + selectedClass + '" draggable="' + (isRenamingLocalDiskItem ? 'false' : 'true') + '" data-local-drag="' + encodedPath + '" data-local-file-context="' + encodedPath + '" data-file-locked="' + (fileLocked ? '1' : '0') + '" data-file-video="' + (isVideoName(name) ? '1' : '0') + '">' +
            '<td>' + displayName + '</td>' +
            '<td>' + (formatNumber(Number(item.size || 0)) + ' 字节') + '</td>' +
            '<td>' + escapeHtml((item && item.modified_time) || '-') + '</td>' +
            '<td class="actions-cell"><div class="actions">' + previewBtn + videoBtn + audioBtn + textBtn + pdfBtn + officeBtn + xmindBtn + '</div></td>' +
            '<td class="row-danger-action"><div class="danger-actions">' + deleteBtn + '</div></td>' +
          '</tr>'
        );
      }

      function restorePreviewWindowGeometry(win) {
        if (!win) {
          return;
        }
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
        syncPreviewWindowButtons(win);
        window.setTimeout(function () {
          fitPreviewImageToWindow(win);
        }, 0);
      }

      function handleLocalDiskDragOver(e) {
        const target = getLocalDiskDropTarget(e);
        if (!target || !activeLocalDiskDragPaths.length || activeLocalDiskDragPaths.indexOf(target.path) >= 0) {
          return;
        }
        e.preventDefault();
        if (e.dataTransfer) {
          e.dataTransfer.dropEffect = 'move';
        }
        clearLocalDiskDropTarget();
        target.element.classList.add('local-disk-drop-target');
      }

      function openFileContextMenu(path, local, locked, isVideo, clientX, clientY, options) {
        closeFileContextMenu();
        closeFolderContextMenu();
        const filePath = String(path || '');
        if (!filePath) {
          return;
        }
        const opts = options || {};
        const isUnlocked = !!getFilePassword(filePath, local);
        const menu = document.createElement('div');
        menu.className = 'folder-context-menu file-context-menu';
        menu.setAttribute('data-file-path', filePath);
        menu.setAttribute('data-file-local', local ? '1' : '0');
        menu.setAttribute('data-local-disk-list', opts.localDiskList ? '1' : '0');
        const selectedPaths = Array.isArray(opts.selectedPaths) ? opts.selectedPaths.map(function (item) { return String(item || ''); }).filter(Boolean) : [filePath];
        const isMultiLocal = !!(local && opts.localDiskList && selectedPaths.length > 1);
        const isMultiRemote = !!(!local && opts.remoteList && selectedPaths.length > 1);
        const isMultiList = isMultiLocal || isMultiRemote;
        menu.setAttribute('data-file-paths', selectedPaths.join('\n'));
        let html = '';
        if (opts.remoteList || opts.localDiskList) {
          if (!isMultiList) {
            html += '<button type="button" class="folder-context-item" data-file-menu-action="summary">' + t('摘要') + '</button>';
            html += '<button type="button" class="folder-context-item" data-file-menu-action="download">' + t('下载') + '</button>';
          }
          if (local && opts.localDiskList) {
            html += '<button type="button" class="folder-context-item" data-file-menu-action="copy-local">' + t('拷贝') + '</button>';
            html += '<button type="button" class="folder-context-item" data-file-menu-action="upload-local">' + t('上传') + '</button>';
          }
          if (!local && opts.remoteList) {
            html += '<button type="button" class="folder-context-item" data-file-menu-action="copy-remote">' + t('拷贝') + '</button>';
          }
          if (!isMultiList && !opts.recycleMode && (!local || opts.localRename)) {
            html += '<button type="button" class="folder-context-item" data-file-menu-action="rename">' + t('改名') + '</button>';
          }
          html += '<button type="button" class="folder-context-item" data-file-menu-action="delete">' + t(opts.deleteLabel || (opts.tagMode ? '移除' : '删除')) + '</button>';
        }
        if (!isMultiList && locked) {
          html += '<button type="button" class="folder-context-item" data-file-menu-action="' + (isUnlocked ? 'session-lock' : 'session-unlock') + '">' + t(isUnlocked ? '加锁' : '解锁') + '</button>';
          html += '<button type="button" class="folder-context-item" data-file-menu-action="remove-lock">' + t('去锁') + '</button>';
        } else if (!isMultiList) {
          html += '<button type="button" class="folder-context-item" data-file-menu-action="lock">' + t('加锁') + '</button>';
        }
        if (!isMultiList && local && isVideo) {
          html += '<button type="button" class="folder-context-item" data-file-menu-action="open-local-player">' + t('使用本地播放器播放') + '</button>';
          html += '<button type="button" class="folder-context-item" data-file-menu-action="choose-local-player">' + t('选择本地播放器') + '</button>';
        }
        if (!isMultiList && !local && isVideo && !opts.recycleMode) {
          html += '<button type="button" class="folder-context-item" data-file-menu-action="extract-audio">' + t('提取音频') + '</button>';
        }
        menu.innerHTML = html;
        document.body.appendChild(menu);
        menu.style.left = Math.round(clientX) + 'px';
        menu.style.top = Math.round(clientY) + 'px';
        clampFloatingMenuPosition(menu, clientX, clientY);
        activeFileContextMenu = menu;
      }

      function closeTagDialog(value) {
        if (!tagDialog) {
          return;
        }
        tagDialog.hidden = true;
        document.body.style.overflow = '';
        const resolver = activeTagDialogResolver;
        activeTagDialogResolver = null;
        if (resolver) {
          resolver(value);
        }
      }

      async function unbindFileFromTag(tagId, fileName, options) {
        const opts = options || {};
        try {
          await fetchJson(
            api.tagUnbind + '?tag_id=' + encodeURIComponent(tagId) + '&file=' + encodeURIComponent(fileName)
              + (opts.local ? '&local=1' : ''),
            { method: 'POST' }
          );
          return true;
        } catch (_) {
          return false;
        }

      }

      function ensureLocalImportFolderPathExpanded(path) {
        const text = String(path || '');
        localImportExpandedFolderPaths.add('');
        if (!text) {
          return;
        }
        const parts = text.split('/');
        let current = '';
        for (let i = 0; i < parts.length; i += 1) {
          current = current ? (current + '/' + parts[i]) : parts[i];
          localImportExpandedFolderPaths.add(current);
        }
      }

      async function handleFileDeleteOrRemove(filePath, local) {
        const name = String(filePath || '');
        if (!name) {
          return;
        }
        const activeTagId = activeFilterTagId;
        if (activeTagId) {
          const confirmed = await askDeleteConfirmDialog({
            title: t('移除标签引用'),
            description: t('此操作只解除标签引用，不会删除文件。'),
            highlight: name,
            confirmText: t('移除'),
            variant: 'warn',
            icon: '🏷'
          });
          if (!confirmed) {
            return;
          }
          resetStatus();
          const ok = await unbindFileFromTag(activeTagId, name, { local: !!local });
          if (!ok) {
            showStatus(t('移除引用失败：关联不存在'), 'err');
            return;
          }
          await showFilesForTag(activeTagId);
          showStatus(t('已移除标签引用：') + name, 'warn');
          return;
        }

        const isRecycleMode = isRecycleFolderPath(activeFolderPath);
        const itemRecord = getFileRecordByPath(name);
        const itemLabel = itemRecord && itemRecord.directory ? t('文件夹') : t('文件');
        const confirmed = await askDeleteConfirmDialog({
          title: isRecycleMode ? (t('彻底删除') + itemLabel) : (t('删除') + itemLabel),
          description: isRecycleMode
            ? t('此操作不可恢复，请确认是否继续。')
            : t('将先移入回收站，之后仍可在回收站中恢复或彻底删除。'),
          highlight: name,
          note: isRecycleMode ? '' : t('文件将移入回收站，不会立即永久删除。'),
          confirmText: isRecycleMode ? t('彻底删除') : t('移入回收站')
        });
        if (!confirmed) {
          return;
        }

        resetStatus();
        await fetchJson(appendFilePassword(withFolderPassword(api.del + '?file=' + encodeURIComponent(name), parentFolderPathFromFilePath(name)), name, false));
        showStatus(isRecycleMode ? (t('已彻底删除') + itemLabel + t('：') + name) : (t('已移入回收站：') + name), 'warn');
        await loadFiles();
      }

      function cancelLocalDiskFileRename() {
        activeLocalDiskRenamePath = '';
        clearLocalDiskRenameClickTimer();
        renderLocalDiskItems(activeLocalDiskItems);
      }

      function localizedPageUrl(lang) {
        return '/';
      }

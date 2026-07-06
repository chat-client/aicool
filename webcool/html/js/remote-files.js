function folderNameFromPath(path) {
        const text = String(path || '');
        const index = text.lastIndexOf('/');
        return index >= 0 ? text.slice(index + 1) : text;
      }

      function clampFolderBrowserWidth(width) {
        const explorerRect = explorerShell ? explorerShell.getBoundingClientRect() : null;
        const explorerWidth = explorerRect ? explorerRect.width : 900;
        const maxWidth = Math.max(240, Math.min(680, explorerWidth - 260));
        return Math.round(Math.max(220, Math.min(Number(width) || 290, maxWidth)));
      }

      function setFolderBrowserWidth(width, persist) {
        if (!explorerShell) {
          return;
        }
        const nextWidth = clampFolderBrowserWidth(width);
        explorerShell.style.setProperty('--folder-browser-width', nextWidth + 'px');
        if (persist) {
          try {
            window.localStorage.setItem('webcool.folderBrowserWidth', String(nextWidth));
          } catch (_) {}
        }
      }

      function closeFileTagMenu() {
        if (activeFileTagMenu && activeFileTagMenu.parentNode) {
          activeFileTagMenu.parentNode.removeChild(activeFileTagMenu);
        }
        activeFileTagMenu = null;
      }

      function isProtectedRestrictedRootTag(node, level) {
        return !!getRestrictedRootTagType(node, level);
      }

      function syncFolderDropHighlight() {
        if (!folderTree) {
          return;
        }
        const nodes = folderTree.querySelectorAll('.folder-tree-node[data-folder-path]');
        nodes.forEach(function (node) {
          const path = node.getAttribute('data-folder-path');
          node.classList.toggle('drop-target', path === activeDropFolderPath);
        });
      }

      function updateFileSelectAllState() {
        if (!fileSelectAll) {
          return;
        }

        const visibleCount = (Array.isArray(currentFiles) ? currentFiles : []).length;
        const selectedCount = getSelectedVisibleFileNames().length;
        fileSelectAll.checked = visibleCount > 0 && selectedCount === visibleCount;
        fileSelectAll.indeterminate = selectedCount > 0 && selectedCount < visibleCount;
      }

      function applyPreviewImageCrop(win) {
        const img = win ? win.querySelector('.preview-image') : null;
        const rect = win && win.__imageCropRect;
        if (!img || !rect || rect.width < 2 || rect.height < 2) {
          setImageEditHint(win, '请先拖拽选择一个剪切区域。', true);
          return;
        }
        try {
          const source = drawImageElementToCanvas(img);
          const canvas = document.createElement('canvas');
          canvas.width = Math.max(1, Math.round(rect.width));
          canvas.height = Math.max(1, Math.round(rect.height));
          const ctx = canvas.getContext('2d');
          ctx.drawImage(
            source,
            Math.round(rect.x), Math.round(rect.y), canvas.width, canvas.height,
            0, 0, canvas.width, canvas.height
          );
          replacePreviewImageWithCanvas(win, canvas);
          setPreviewCropMode(win, false);
          setImageEditHint(win, '已应用剪切，点击“保存到服务”写入文件。');
        } catch (err) {
          setImageEditHint(win, '剪切失败：' + err.message, true);
        }
      }

      function openPreview(kind, file, name, options) {
        const opts = options || {};
        const previewKey = (opts.previewKey || file);
        const existed = openedPreviewWindows.get(previewKey);
        if (existed && existed.isConnected) {
          bringToFront(existed);
          if (kind === 'image' && Array.isArray(opts.gallery) && opts.gallery.length) {
            updateImagePreviewWindow(existed, opts.gallery, Number(opts.galleryIndex || 0));
          }
          const video = existed.querySelector('video');
          if (video) {
            video.play().catch(function () {});
          }
          return;
        }

        const rawFileForUrl = decodeURIComponent(String(file || ''));
        const previewName = name || rawFileForUrl;
        const isDocxOffice = kind === 'office'
          && window.WebCoolDocxPreview
          && window.WebCoolDocxPreview.isDocxName(previewName);
        const isSpreadsheetOffice = kind === 'office'
          && window.WebCoolSpreadsheetPreview
          && window.WebCoolSpreadsheetPreview.isSpreadsheetName(previewName);
        const isPptxOffice = kind === 'office'
          && window.WebCoolPptxPreview
          && window.WebCoolPptxPreview.isPptxName(previewName);
        const isFrontendOffice = isDocxOffice || isSpreadsheetOffice || isPptxOffice;
        const isXMindPreview = kind === 'mindmap' && isXMindName(previewName);
        const cacheBust = function (value) {
          return value + (value.indexOf('?') >= 0 ? '&' : '?') + 'v=' + Date.now();
        };
        const officePdfBaseUrl = kind === 'office'
          ? (opts.local ? localDiskOfficePreviewUrl(rawFileForUrl) : officePreviewUrlForFile(rawFileForUrl))
          : '';
        const officePdfUrl = kind === 'office' ? cacheBust(officePdfBaseUrl) : '';
        const officeSourceUrl = kind === 'office'
          ? (isFrontendOffice ? (opts.url || (opts.local ? localDiskDownloadUrl(rawFileForUrl) : downloadUrlForFile(rawFileForUrl, false))) : officePdfBaseUrl)
          : '';
        const url = cacheBust(kind === 'office'
          ? officeSourceUrl
          : (opts.url || downloadUrlForFile(rawFileForUrl, true)));
        const win = document.createElement('div');
        win.className = 'floating-preview';
        win.classList.add('preview-kind-' + kind);
        previewZ += 1;
        win.style.zIndex = String(previewZ);

        const isHtmlText = kind === 'text' && window.WebCoolHtml && window.WebCoolHtml.isHtmlName(previewName);
        const isMarkdownText = kind === 'text' && !isHtmlText && window.WebCoolMarkdown
          && window.WebCoolMarkdown.isMarkdownName(previewName);
        const isRichTextPreview = isMarkdownText || isHtmlText;
        const isCodeText = kind === 'text' && !isRichTextPreview && !!detectCodeLang(previewName);
        if (isRichTextPreview) {
          win.classList.add('preview-kind-rich-text');
        } else if (isFrontendOffice) {
          win.classList.add('preview-kind-rich-text', 'preview-kind-office');
        } else if (isXMindPreview) {
          win.classList.add('preview-kind-mindmap');
        } else if (isCodeText) {
          win.classList.add('preview-kind-code');
        }
        const titleText = kind === 'video' ? '视频播放：'
            : (kind === 'audio' ? '音频播放：'
            : (kind === 'office' ? t('Office预览：')
              : (kind === 'mindmap' ? t('思维导图预览：')
              : (kind === 'pdf' ? 'PDF预览：'
              : (kind === 'text'
                ? (isMarkdownText ? t('Markdown预览：') : (isHtmlText ? t('HTML预览：') : '文本查看：'))
                : '图片预览：')))));
        const escapedTitle = escapeHtml(name || '');

        let mediaHtml = '';
        let bodyClass = 'preview-body';
        if (kind === 'video') {
          const rawName = decodeURIComponent(String(file || '')) || String(name || '');
          const subtitleName = toVttSidecarName(rawName);
          const subtitleUrl = (opts.local ? localDiskDownloadUrl(subtitleName) : downloadUrlForFile(subtitleName, true)) + '&v=' + Date.now();
          const sidecarAudioUrl = resolveSplitVideoAudioUrl(rawName, opts.local);
          const sidecarAudioHint = sidecarAudioUrl
            ? '<div class="preview-sidecar-audio-hint">' +
                escapeHtml(t('当前声音由拆分出的独立音频文件同步播放。由于视频文件本身没有音轨，播放器里的音量图标可能显示为禁用，但不影响实际出声。')) +
              '</div>'
            : '';
          bodyClass += ' preview-body-video';
          mediaHtml = '<div class="preview-video-stack">' +
              '<video class="preview-video" controls preload="metadata">' +
                '<track kind="subtitles" srclang="zh-CN" label="中文字幕" default src="' + subtitleUrl + '">' +
              '</video>' +
              (sidecarAudioUrl ? ('<audio class="preview-video-sidecar-audio" preload="metadata" hidden src="' + escapeHtml(sidecarAudioUrl) + '"></audio>') : '') +
              sidecarAudioHint +
              (((opts.local && isLocalDiskConvertibleVideoName(rawName)) || (!opts.local && isRealMediaVideoName(rawName)))
                ? '<div class="preview-video-tools">' +
                    (opts.local ? '<button class="preview-convert-btn" type="button" data-preview-stream-video="' + encodeURIComponent(rawName) + '">' + t('边转边看') + '</button>' : '') +
                    '<button class="preview-convert-btn" type="button" data-preview-convert-video="' + encodeURIComponent(rawName) + '" data-preview-convert-local="' + (opts.local ? '1' : '0') + '">' + t('转换为MP4') + '</button><span>' + (opts.local ? t('本地磁盘RM/RMVB/AVI/MOV/WMV/MPG/MPEG格式可转换为MP4，输出文件会保存在源文件相同目录。') : t('RM/RMVB/MOV/WMV/MPG/MPEG格式浏览器兼容性较差，可转换为MP4后播放。')) + '</span></div>' +
                    (opts.local ? '<div class="preview-stream-progress" data-preview-stream-progress hidden><div class="preview-stream-progress-fill" data-preview-stream-progress-fill></div><span data-preview-stream-progress-text>' + t('等待边转边看') + '</span></div>' : '')
                : '') +
            '</div>';
        } else if (kind === 'audio') {
          mediaHtml = '<audio class="preview-audio" controls preload="metadata" src="' + url + '"></audio>';
        } else if (kind === 'office' && isFrontendOffice) {
          bodyClass += ' preview-body-text';
          const officePreviewKind = isSpreadsheetOffice ? 'spreadsheet' : (isPptxOffice ? 'pptx' : 'docx');
          const officeLoadingText = isSpreadsheetOffice ? t('正在加载表格预览...')
            : (isPptxOffice ? t('正在加载 PPTX 预览...') : t('正在加载 DOCX 预览...'));
          mediaHtml = '<div class="office-preview-shell">' +
              '<div class="office-preview-status" data-office-preview-status>' + escapeHtml(officeLoadingText) + '</div>' +
              '<div class="office-render" data-office-render data-office-preview-kind="' + officePreviewKind + '"></div>' +
              '<iframe class="preview-pdf office-pdf-fallback" data-office-pdf-fallback hidden data-office-pdf-src="' + escapeHtml(officePdfUrl) + '" title="' + escapedTitle + '"></iframe>' +
            '</div>';
        } else if (kind === 'mindmap' && isXMindPreview) {
          bodyClass += ' preview-body-mindmap';
          mediaHtml = '<div class="xmind-preview-shell">' +
              '<div class="xmind-preview-status" data-xmind-preview-status>' + escapeHtml(t('正在加载 XMind 预览...')) + '</div>' +
              '<div class="xmind-preview-host" data-xmind-preview-host></div>' +
            '</div>';
        } else if (kind === 'pdf' || kind === 'office') {
          mediaHtml = '<iframe class="preview-pdf" src="' + escapeHtml(url) + '" title="' + escapedTitle + '"></iframe>';
        } else if (kind === 'text') {
          bodyClass += ' preview-body-text';
          if (isRichTextPreview) {
            const previewNode = isHtmlText
              ? ('<iframe class="html-preview-frame" data-rich-text-render sandbox="" title="' +
                escapeHtml(t('HTML预览')) + '"></iframe>')
              : '<article class="markdown-render" data-rich-text-render>加载中...</article>';
            mediaHtml = '<div class="markdown-preview-shell">' +
                '<div class="markdown-preview-toolbar">' +
                  '<button class="markdown-view-btn active" type="button" data-rich-text-view="preview">' + t('预览') + '</button>' +
                  '<button class="markdown-view-btn" type="button" data-rich-text-view="source">' + t('源码') + '</button>' +
                '</div>' +
                previewNode +
                '<pre class="preview-text markdown-source" data-rich-text-source hidden>加载中...</pre>' +
              '</div>';
          } else {
            mediaHtml = '<pre class="preview-text">加载中...</pre>';
          }
        } else {
          bodyClass += ' preview-body-image';
          const showNav = Array.isArray(opts.gallery) && opts.gallery.length > 1;
          mediaHtml = '<div class="preview-image-toolbar">' +
              '<button class="preview-edit-btn" type="button" data-image-edit="rotate-left" title="向左旋转90度" aria-label="向左旋转90度">' +
                '<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false" fill="none" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">' +
                  '<rect x="8" y="9.5" width="9" height="9" rx="1.8"></rect><path d="M6 4.8v4.4h4.4"></path><path d="M6 9.2a6.5 6.5 0 0 1 10.5-3.8"></path>' +
                '</svg>' +
              '</button>' +
              '<button class="preview-edit-btn" type="button" data-image-edit="rotate-right" title="向右旋转90度" aria-label="向右旋转90度">' +
                '<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false" fill="none" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">' +
                  '<rect x="7" y="9.5" width="9" height="9" rx="1.8"></rect><path d="M18 4.8v4.4h-4.4"></path><path d="M18 9.2A6.5 6.5 0 0 0 7.5 5.4"></path>' +
                '</svg>' +
              '</button>' +
              '<button class="preview-edit-btn" type="button" data-image-edit="crop" title="剪切" aria-label="剪切">✂</button>' +
              '<button class="preview-edit-btn" type="button" data-image-edit="apply-crop" title="应用剪切" aria-label="应用剪切">✓</button>' +
              '<button class="preview-edit-btn" type="button" data-image-edit="cancel-crop" title="取消剪切" aria-label="取消剪切">×</button>' +
              '<button class="preview-edit-btn" type="button" data-image-edit="zoom-in" title="等比例放大" aria-label="等比例放大">＋</button>' +
              '<button class="preview-edit-btn" type="button" data-image-edit="zoom-out" title="等比例缩小" aria-label="等比例缩小">－</button>' +
              '<span class="preview-image-size" title="当前图像尺寸">' +
                '<input class="preview-size-input" data-image-size="width" type="number" min="1" step="1" aria-label="图片宽度">' +
                '<span>x</span>' +
                '<input class="preview-size-input" data-image-size="height" type="number" min="1" step="1" aria-label="图片高度">' +
              '</span>' +
              '<button class="preview-edit-btn" type="button" data-image-edit="download" title="下载到本地" aria-label="下载到本地">' +
                '<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false" fill="none" stroke-width="2.3" stroke-linecap="round" stroke-linejoin="round">' +
                  '<path d="M12 3v12"></path><path d="M6.5 9.5 12 15l5.5-5.5"></path><path d="M5 17v2.2c0 .9.7 1.6 1.6 1.6h10.8c.9 0 1.6-.7 1.6-1.6V17"></path>' +
                '</svg>' +
              '</button>' +
              '<button class="preview-edit-btn primary" type="button" data-image-edit="save" title="保存到服务" aria-label="保存到服务">💾</button>' +
              '<div class="preview-edit-hint" aria-live="polite"></div>' +
            '</div>' +
            '<div class="preview-image-shell">'
            + '<button class="preview-nav-btn preview-nav-prev" type="button" data-preview-nav="prev" aria-label="上一张"' + (showNav ? '' : ' hidden') + '>‹</button>'
            + '<img class="preview-image" alt="图片预览" src="' + url + '">'
            + '<div class="preview-crop-rect" hidden></div>'
            + '<button class="preview-nav-btn preview-nav-next" type="button" data-preview-nav="next" aria-label="下一张"' + (showNav ? '' : ' hidden') + '>›</button>'
            + '</div>';
        }

        win.innerHTML =
          '<div class="preview-head">' +
            '<div class="preview-title">' + titleText + escapedTitle + '</div>' +
            '<div class="preview-head-actions">' +
              '<button class="preview-window-btn" type="button" data-preview-window-action="minimize" title="' + escapeHtml(t('最小化')) + '" aria-label="' + escapeHtml(t('最小化')) + '">−</button>' +
              '<button class="preview-window-btn" type="button" data-preview-window-action="maximize" title="' + escapeHtml(t('最大化')) + '" aria-label="' + escapeHtml(t('最大化')) + '">□</button>' +
              '<button class="preview-close" type="button" title="' + escapeHtml(t('关闭')) + '" aria-label="' + escapeHtml(t('关闭')) + '">×</button>' +
            '</div>' +
          '</div>' +
          '<div class="' + bodyClass + '">' + mediaHtml + '</div>';

        previewLayer.appendChild(win);
        centerPreviewWindow(win);
        openedPreviewWindows.set(previewKey, win);

        const mediaVideo = win.querySelector('video');
        if (mediaVideo) {
          const rawVideoFile = decodeURIComponent(String(file || ''));
          const mediaSidecarAudio = win.querySelector('.preview-video-sidecar-audio');
          bindVideoResume(mediaVideo, opts.local ? ('local:' + rawVideoFile) : rawVideoFile);
          mediaVideo.src = url;
          if (mediaSidecarAudio) {
            bindSplitVideoAudio(mediaVideo, mediaSidecarAudio);
          }
          mediaVideo.play().catch(function () {});
          const streamBtn = win.querySelector('[data-preview-stream-video]');
          if (streamBtn) {
            streamBtn.addEventListener('click', async function () {
              const rawStreamFile = decodeURIComponent(streamBtn.getAttribute('data-preview-stream-video') || '');
              if (!rawStreamFile) {
                return;
              }
              let stateUrl = api.localDiskVideoStreamState + '?path=' + encodeURIComponent(rawStreamFile);
              stateUrl = appendLocalDirPassword(appendFilePassword(stateUrl, rawStreamFile, true), localDiskParentPath(rawStreamFile));
              let savedStreamOffsetMs = 0;
              try {
                const stateData = await fetchJson(stateUrl);
                savedStreamOffsetMs = Math.max(0, Number(stateData.position_ms || 0));
              } catch (_) {
                savedStreamOffsetMs = 0;
              }
              let streamUrl = api.localDiskVideoStream + '?path=' + encodeURIComponent(rawStreamFile);
              streamUrl = appendLocalDirPassword(appendFilePassword(streamUrl, rawStreamFile, true), localDiskParentPath(rawStreamFile));
              const streamTaskId = 'local-stream-' + Date.now() + '-' + Math.floor(Math.random() * 1000000);
              streamUrl += '&stream_task_id=' + encodeURIComponent(streamTaskId);
              streamBtn.disabled = true;
              streamBtn.textContent = t('边转边看中');
              const progressBox = win.querySelector('[data-preview-stream-progress]');
              const progressFill = win.querySelector('[data-preview-stream-progress-fill]');
              const progressText = win.querySelector('[data-preview-stream-progress-text]');
              if (progressBox) {
                progressBox.hidden = false;
              }
              if (progressFill) {
                progressFill.style.width = '0%';
              }
              if (progressText) {
                progressText.textContent = t('边转边看准备中...');
              }
              if (win.__streamProgressTimer) {
                clearInterval(win.__streamProgressTimer);
              }
              if (win.__streamStateTimer) {
                clearInterval(win.__streamStateTimer);
              }
              win.__streamProgressTimer = setInterval(async function () {
                try {
                  const data = await fetchJson(api.convertProgress + '?task_id=' + encodeURIComponent(streamTaskId));
                  const percent = Math.max(0, Math.min(data.done ? 100 : 99, Math.round(Number(data.progress || 0))));
                  if (progressFill) {
                    progressFill.style.width = percent + '%';
                  }
                  if (progressText) {
                    progressText.textContent = data.done
                      ? (data.success ? t('边转边看完成 100%') : t('边转边看失败'))
                      : ((data.message || t('边转边看中...')) + ' ' + percent + '%');
                  }
                  if (data.done) {
                    clearInterval(win.__streamProgressTimer);
                    win.__streamProgressTimer = null;
                  }
                } catch (_) {
                  if (progressText) {
                    progressText.textContent = t('边转边看准备中...');
                  }
                }
              }, 800);
              mediaVideo.pause();
              mediaVideo.src = streamUrl + '&v=' + Date.now();
              mediaVideo.load();
              const saveStreamState = function () {
                const absoluteMs = Math.max(0, Math.round(savedStreamOffsetMs + ((Number(mediaVideo.currentTime) || 0) * 1000)));
                const stateHeaders = authState.token
                  ? { Authorization: 'Bearer ' + authState.token }
                  : undefined;
                fetch(stateUrl + '&position_ms=' + encodeURIComponent(String(absoluteMs)), {
                  method: 'POST',
                  credentials: 'same-origin',
                  headers: stateHeaders
                }).catch(function () {});
              };
              win.__saveStreamState = saveStreamState;
              win.__streamStateTimer = setInterval(saveStreamState, 5000);
              mediaVideo.addEventListener('pause', saveStreamState);
              mediaVideo.addEventListener('ended', function refreshLocalAfterStreamConvert() {
                mediaVideo.removeEventListener('ended', refreshLocalAfterStreamConvert);
                if (win.__streamProgressTimer) {
                  clearInterval(win.__streamProgressTimer);
                  win.__streamProgressTimer = null;
                }
                if (typeof win.__saveStreamState === 'function') {
                  win.__saveStreamState();
                }
                if (win.__streamStateTimer) {
                  clearInterval(win.__streamStateTimer);
                  win.__streamStateTimer = null;
                }
                if (progressFill) {
                  progressFill.style.width = '100%';
                }
                if (progressText) {
                  progressText.textContent = t('边转边看完成 100%');
                }
                loadLocalDisk(activeLocalDiskPath || localDiskParentPath(rawStreamFile) || '');
              });
              mediaVideo.play().catch(function () {});
            });
          }
          const convertBtn = win.querySelector('[data-preview-convert-video]');
          if (convertBtn) {
            convertBtn.addEventListener('click', async function () {
              const rawConvertFile = decodeURIComponent(convertBtn.getAttribute('data-preview-convert-video') || '');
              if (!rawConvertFile) {
                return;
              }
              convertBtn.disabled = true;
              convertBtn.textContent = t('转换任务已启动');
              mediaVideo.pause();
              const encoded = encodeURIComponent(rawConvertFile);
              const isLocalConvert = convertBtn.getAttribute('data-preview-convert-local') === '1';
              showManualTranscodePrompt([{ name: rawConvertFile, reason: isLocalConvert ? t('本地磁盘RM/RMVB/AVI/MOV/WMV/MPG/MPEG格式可转换为MP4，输出文件会保存在源文件相同目录。') : t('RM/RMVB/MOV/WMV/MPG/MPEG格式浏览器兼容性较差，可转换为MP4后播放。') }]);
              if (isLocalConvert) {
                await startLocalVideoTranscode(encoded);
              } else {
                await startManualTranscode(encoded);
              }
            });
          }

          // 仅在真实播放失败或音频不兼容时提示转码（不用额外 probe，避免误判）
          if (!opts.local) {
            bindPreviewVideoCompatHandlers(win, mediaVideo, rawVideoFile);
          }
        }

        const mediaAudio = win.querySelector('audio');
        if (mediaAudio) {
          const rawAudioFile = decodeURIComponent(String(file || ''));
          if (!opts.local) {
            bindVideoResume(mediaAudio, rawAudioFile);
          }
          mediaAudio.play().catch(function () {});
        }

        const mediaText = win.querySelector('.preview-text:not([data-rich-text-source])');
        const richRender = win.querySelector('[data-rich-text-render]');
        const richSource = win.querySelector('[data-rich-text-source]');
        if (mediaText || richRender) {
          const lang = detectCodeLang(name);
          if (lang && mediaText && !richSource) {
            mediaText.classList.add('code');
          }
          const textHeaders = authState.token
            ? { Authorization: 'Bearer ' + authState.token }
            : undefined;
          fetch(url, { credentials: 'same-origin', headers: textHeaders })
            .then(function (res) {
              if (!res.ok) {
                throw new Error('http ' + res.status);
              }
              return res.text();
            })
            .then(function (text) {
              if (richRender && richSource && isMarkdownText && window.WebCoolMarkdown) {
                richRender.innerHTML = window.WebCoolMarkdown.render(text);
                window.WebCoolMarkdown.bindAnchors(richRender);
                richSource.textContent = text;
              } else if (richRender && richSource && isHtmlText && window.WebCoolHtml) {
                window.WebCoolHtml.renderPreview(richRender, text, {
                  title: t('HTML预览')
                });
                richSource.textContent = text;
              } else if (lang && mediaText) {
                mediaText.innerHTML = highlightCodeText(text, lang);
              } else if (mediaText) {
                mediaText.textContent = text;
              }
            })
            .catch(function (err) {
              if (richRender) {
                if (isHtmlText && richRender.tagName === 'IFRAME') {
                  richRender.srcdoc = '<p>文本加载失败: ' + escapeHtml(err.message) + '</p>';
                } else {
                  richRender.textContent = '文本加载失败: ' + err.message;
                }
              }
              if (mediaText) {
                mediaText.textContent = '文本加载失败: ' + err.message;
              }
            });
        }

        if (isRichTextPreview) {
          const richTextButtons = win.querySelectorAll('[data-rich-text-view]');
          richTextButtons.forEach(function (btn) {
            btn.addEventListener('click', function () {
              const view = btn.getAttribute('data-rich-text-view') || 'preview';
              richTextButtons.forEach(function (item) {
                item.classList.toggle('active', item === btn);
              });
              const renderNode = win.querySelector('[data-rich-text-render]');
              const sourceNode = win.querySelector('[data-rich-text-source]');
              if (renderNode) {
                renderNode.hidden = view !== 'preview';
              }
              if (sourceNode) {
                sourceNode.hidden = view !== 'source';
              }
            });
          });
        }

        const officeRender = win.querySelector('[data-office-render]');
        if (officeRender) {
          const officeStatus = win.querySelector('[data-office-preview-status]');
          const officeFallback = win.querySelector('[data-office-pdf-fallback]');
          const officeKind = officeRender.getAttribute('data-office-preview-kind') || 'docx';
          const renderer = officeKind === 'pptx'
            ? (window.WebCoolPptxPreview && window.WebCoolPptxPreview.renderPreview)
            : (officeKind === 'spreadsheet'
            ? (window.WebCoolSpreadsheetPreview && window.WebCoolSpreadsheetPreview.renderPreview)
            : (window.WebCoolDocxPreview && window.WebCoolDocxPreview.renderPreview));
          if (!renderer) {
            if (officeStatus) {
              officeStatus.textContent = t('前端 Office 预览组件未加载，正在尝试转换为 PDF。');
            }
            officeRender.hidden = true;
            if (officeFallback) {
              const fallbackSrc = officeFallback.getAttribute('data-office-pdf-src') || '';
              if (fallbackSrc && !officeFallback.getAttribute('src')) {
                officeFallback.setAttribute('src', fallbackSrc);
              }
              officeFallback.hidden = false;
            }
          } else {
            renderer(officeRender, url, {
            token: authState.token,
            loadingText: officeKind === 'spreadsheet' ? t('正在加载表格预览...')
              : (officeKind === 'pptx' ? t('正在加载 PPTX 预览...') : t('正在加载 DOCX 预览...'))
            }).then(function () {
              if (officeStatus) {
                officeStatus.hidden = true;
              }
            }).catch(function (err) {
              if (officeStatus) {
                const fallbackPrefix = officeKind === 'spreadsheet'
                  ? t('表格前端预览失败，正在尝试转换为 PDF：')
                  : (officeKind === 'pptx'
                    ? t('PPTX 前端预览失败，正在尝试转换为 PDF：')
                    : t('DOCX 前端预览失败，正在尝试转换为 PDF：'));
                officeStatus.hidden = false;
                officeStatus.textContent = fallbackPrefix + (err && err.message ? err.message : err);
              }
              officeRender.hidden = true;
              if (officeFallback) {
                const fallbackSrc = officeFallback.getAttribute('data-office-pdf-src') || '';
                if (fallbackSrc && !officeFallback.getAttribute('src')) {
                  officeFallback.setAttribute('src', fallbackSrc);
                }
                officeFallback.hidden = false;
              }
            });
          }
        }

        const xmindHost = win.querySelector('[data-xmind-preview-host]');
        if (xmindHost) {
          const xmindStatus = win.querySelector('[data-xmind-preview-status]');
          const renderer = window.WebCoolXMindPreview && window.WebCoolXMindPreview.renderPreview;
          if (!renderer) {
            if (xmindStatus) {
              xmindStatus.textContent = t('XMind 预览组件未加载。');
            }
          } else {
            renderer(xmindHost, url, {
              token: authState.token,
              loadingText: t('正在加载 XMind 预览...'),
              region: 'cn'
            }).then(function () {
              if (xmindStatus) {
                xmindStatus.hidden = true;
              }
            }).catch(function (err) {
              if (xmindStatus) {
                xmindStatus.hidden = false;
                xmindStatus.textContent = t('XMind 预览失败：') + (err && err.message ? err.message : err);
              }
            });
          }
        }

        const closeBtn = win.querySelector('.preview-close');
        const head = win.querySelector('.preview-head');
        const minimizeBtn = win.querySelector('[data-preview-window-action="minimize"]');
        const maximizeBtn = win.querySelector('[data-preview-window-action="maximize"]');

        closeBtn.addEventListener('click', function () {
          closePreviewWindow(win, previewKey);
        });

        if (minimizeBtn) {
          minimizeBtn.addEventListener('click', function () {
            minimizePreviewWindow(win);
          });
        }

        if (maximizeBtn) {
          maximizeBtn.addEventListener('click', function () {
            togglePreviewWindowMaximize(win);
          });
        }
        syncPreviewWindowButtons(win);

        const prevNavBtn = win.querySelector('.preview-nav-btn[data-preview-nav="prev"]');
        const nextNavBtn = win.querySelector('.preview-nav-btn[data-preview-nav="next"]');
        if (prevNavBtn) {
          prevNavBtn.addEventListener('click', function () {
            stepImagePreviewWindow(win, -1);
          });
        }
        if (nextNavBtn) {
          nextNavBtn.addEventListener('click', function () {
            stepImagePreviewWindow(win, 1);
          });
        }

        if (kind === 'image' && Array.isArray(opts.gallery) && opts.gallery.length) {
          updateImagePreviewWindow(win, opts.gallery, Number(opts.galleryIndex || 0));
        } else if (kind === 'image') {
          updateImagePreviewWindow(win, [{
            file: rawFileForUrl,
            name: String(name || rawFileForUrl || ''),
            local: !!opts.local
          }], 0);
        }
        if (kind === 'image') {
          initPreviewImageEditing(win);
        }

        win.addEventListener('mousedown', function () {
          bringToFront(win);
        });

        head.addEventListener('mousedown', function (e) {
          if (e.target.closest('.preview-close') || e.target.closest('.preview-window-btn') || win.classList.contains('is-maximized')) {
            return;
          }
          snapshotPreviewWindowRect(win);
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
          togglePreviewWindowMaximize(win);
        });
      }

      function handleLocalDiskClickEvent(e) {
        const renameInput = e.target.closest('.local-disk-rename-input');
        if (renameInput) {
          return;
        }
        const fileNameClick = e.target.closest('.local-disk-file-name-action[data-local-disk-file-name-click]');
        if (fileNameClick) {
          e.preventDefault();
          e.stopPropagation();
          handleLocalDiskFileNameClick(decodeURIComponent(fileNameClick.getAttribute('data-local-disk-file-name-click') || ''));
          return;
        }
        const localDirLockIcon = e.target.closest('.local-dir-lock-inline');
        if (localDirLockIcon) {
          const row = localDirLockIcon.closest('[data-local-dir-context]');
          if (!row) {
            return;
          }
          e.preventDefault();
          e.stopPropagation();
          const path = decodeURIComponent(row.getAttribute('data-local-dir-context') || '');
          const action = getLocalDirPassword(path) ? 'session-lock' : 'session-unlock';
          handleLocalDirContextAction(action, path).catch(function (err) {
            showStatus(t('本地目录锁操作失败：') + err.message, 'err');
          });
          return;
        }
        const localLockIcon = e.target.closest('.file-lock-inline');
        if (localLockIcon) {
          const row = localLockIcon.closest('[data-local-file-context]');
          if (!row) {
            return;
          }
          e.preventDefault();
          e.stopPropagation();
          const path = decodeURIComponent(row.getAttribute('data-local-file-context') || '');
          const action = getFilePassword(path, true) ? 'session-lock' : 'session-unlock';
          handleFileContextAction(action, path, true).catch(function (err) {
            showStatus(t('文件锁操作失败：') + err.message, 'err');
          });
          return;
        }
        const toggleBtn = e.target.closest('.local-disk-tree-caret[data-local-toggle]');
        if (toggleBtn) {
          e.stopPropagation();
          const path = decodeURIComponent(toggleBtn.getAttribute('data-local-toggle') || '');
          const row = toggleBtn.closest('[data-local-dir-context]');
          if (row && row.getAttribute('data-local-dir-locked') === '1' && !getLocalDirPassword(path)) {
            ensureLocalDirUnlocked(path).then(function (ok) {
              if (ok) {
                toggleLocalDiskTreePath(path);
              }
            }).catch(function (err) {
              showStatus(t('解锁本地目录失败：') + err.message, 'err');
            });
            return;
          }
          toggleLocalDiskTreePath(path);
          return;
        }
        const selectInput = e.target.closest('.local-disk-select[data-local-select]');
        if (selectInput) {
          const path = decodeURIComponent(selectInput.getAttribute('data-local-select') || '');
          updateLocalDiskSelection(path, selectInput.checked);
          return;
        }
        const localTagBtn = e.target.closest('.local-file-tag-btn[data-local-tag-file]');
        if (localTagBtn) {
          e.preventDefault();
          e.stopPropagation();
          const path = decodeURIComponent(localTagBtn.getAttribute('data-local-tag-file') || '');
          if (!path) {
            return;
          }
          openFilesTagMenu(localTagBtn, [path], { local: true }).catch(function (err) {
            showStatus(t('打开标签选择失败：') + err.message, 'err');
          });
          return;
        }
        const folderBtn = e.target.closest('[data-local-folder]');
        if (folderBtn && !e.target.closest('.local-delete-btn') && !e.target.closest('.local-disk-select') && !e.target.closest('.local-file-tag-btn')) {
          const path = decodeURIComponent(folderBtn.getAttribute('data-local-folder') || '/');
          const row = folderBtn.closest('[data-local-dir-context], tr[data-local-dir-context]');
          if (row && row.getAttribute('data-local-dir-locked') === '1' && !getLocalDirPassword(path)) {
            ensureLocalDirUnlocked(path).then(function (ok) {
              if (ok) {
                loadLocalDisk(path, { resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, path) });
              }
            }).catch(function (err) {
              showStatus(t('解锁本地目录失败：') + err.message, 'err');
            });
            return;
          }
          loadLocalDisk(path, { resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, path) });
          return;
        }
        const mkdirBtn = e.target.closest('.local-mkdir-btn[data-local-mkdir]');
        if (mkdirBtn) {
          e.stopPropagation();
          const path = decodeURIComponent(mkdirBtn.getAttribute('data-local-mkdir') || '');
          if (!path) { return; }
          const name = window.prompt(t('请输入新建子目录名称'));
          if (name === null) { return; }
          const cleanName = String(name || '').trim();
          if (!cleanName) {
            showStatus(t('子目录名称不能为空'), 'err');
            return;
          }
          fetchJson(appendLocalDirPassword(api.localDiskMkdir + '?path=' + encodeURIComponent(path) + '&name=' + encodeURIComponent(cleanName), path), { method: 'POST' })
            .then(function () {
              showStatus(t('子目录已创建：') + cleanName, 'ok');
              loadLocalDisk(path, { resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, path) });
            })
            .catch(function (err) {
              showStatus(t('创建子目录失败：') + err.message, 'err');
            });
          return;
        }
        const deleteBtn = e.target.closest('.local-delete-btn[data-local-delete]');
        if (deleteBtn) {
          e.stopPropagation();
          const path = decodeURIComponent(deleteBtn.getAttribute('data-local-delete') || '');
          if (!path) { return; }
          const parentRow = deleteBtn.closest('tr');
          const isDir = parentRow
            ? (parentRow.querySelector('td:nth-child(2)') && parentRow.querySelector('td:nth-child(2)').textContent === '文件夹')
            : deleteBtn.closest('.local-disk-dir-item') !== null;
          (async function () {
            const confirmed = await askDeleteConfirmDialog({
              title: isDir ? t('删除目录') : t('删除'),
              description: isDir ? t('目录将移至系统回收站。') : t('文件将移至系统回收站。'),
              highlight: path,
              confirmText: t('移除')
            });
            if (!confirmed) { return; }
            const deleteLockPath = isDir && getLocalDirPassword(path) ? path : localDiskParentPath(path);
            fetchJson(appendLocalDirPassword(appendFilePassword(api.localDiskDelete + '?path=' + encodeURIComponent(path), path, true), deleteLockPath), { method: 'POST' })
              .then(function () {
                showStatus((isDir ? t('本地目录已移至回收站：') : t('本地文件已删除：')) + path, 'warn');
                const nextPath = isDir ? localDiskParentPath(path) : (activeLocalDiskPath || '');
                if (isDir) {
                  localDiskTreeCache.delete(path);
                  expandedLocalDiskTreePaths.delete(path);
                }
                loadLocalDisk(nextPath, { resetTreeRoot: !localDiskPathContains(activeLocalDiskTreeRootPath, nextPath) });
              })
              .catch(function (err) {
                showStatus(t('删除失败：') + err.message, 'err');
              });
          })().catch(function (err) {
            showStatus(t('删除失败：') + err.message, 'err');
          });
          return;
        }
        const previewBtn = e.target.closest('.local-preview-btn[data-local-file][data-kind]');
        if (!previewBtn) { return; }
        const path = decodeURIComponent(previewBtn.getAttribute('data-local-file') || '');
        const kind = previewBtn.getAttribute('data-kind') || 'image';
        if (!path) { return; }
        if (kind === 'image') {
          openLocalDiskImagePreview(path, previewBtn.getAttribute('data-local-name') || path);
          return;
        }
        openPreview(kind, encodeURIComponent(path), path, {
          local: true,
          url: localDiskDownloadUrl(path),
          previewKey: 'local:' + path
        });
      }

      function localDiskDownloadUrl(path) {
        const filePath = String(path || '/');
        return appendLocalDirPassword(
          appendFilePassword(api.localDiskDownload + '?path=' + encodeURIComponent(filePath), filePath, true),
          localDiskParentPath(filePath)
        );
      }

      function localDiskOfficePreviewUrl(path) {
        const filePath = String(path || '/');
        return appendLocalDirPassword(
          appendFilePassword(api.localDiskOfficePreview + '?path=' + encodeURIComponent(filePath), filePath, true),
          localDiskParentPath(filePath)
        );
      }

      function officePreviewUrlForFile(filePath) {
        const target = String(filePath || '');
        return appendFilePassword(
          withFolderPassword(api.officePreview + '?file=' + encodeURIComponent(target), parentFolderPathFromFilePath(target)),
          target,
          false
        );
      }

      function renderAudioPlaylistItems(container, files, activeFileName) {
        if (!container) {
          return;
        }
        container.innerHTML = files.map(function (file, index) {
          const name = String((file && file.name) || getFilePath(file) || '');
          const itemClass = getFilePath(file) === activeFileName ? 'audio-playlist-item active' : 'audio-playlist-item';
          return '<button type="button" class="' + itemClass + '" data-playlist-index="' + index + '">' +
            '<span class="audio-playlist-item-index">' + String(index + 1) + '</span>' +
            '<span class="audio-playlist-item-name">' + escapeHtml(name) + '</span>' +
          '</button>';
        }).join('');
      }

      async function migrateLegacyTagTree(legacyNodes) {
        for (let i = 0; i < legacyNodes.length; i += 1) {
          await migrateLegacyTagNode(legacyNodes[i], '');
        }
        try {
          localStorage.removeItem(TAG_TREE_STORAGE_KEY);
        } catch (_) {
        }
      }

      async function loadFolderTreeState() {
        const data = await fetchFolderList();
        const folders = ensureRootFixedFolderNodes(Array.isArray(data.folders) ? data.folders : []);
        folderTreeData = folders.map(normalizeFolderNode);
        if (activeFolderPath) {
          const parts = String(activeFolderPath || '').split('/').filter(Boolean);
          let current = '';
          for (let i = 0; i < parts.length; i += 1) {
            current = current ? (current + '/' + parts[i]) : parts[i];
            await loadFolderChildren(current);
          }
        }
        ensureFolderPathExpanded(activeFolderPath);
        renderFolderTree();
      }

      function refreshRenderedFileSelection() {
        if (!fileList) {
          return;
        }
        fileList.querySelectorAll('.file-select-input[data-select-file]').forEach(function (checkbox) {
          const path = decodeURIComponent(checkbox.getAttribute('data-select-file') || '');
          checkbox.checked = selectedFileNames.has(path);
          const row = checkbox.closest('tr');
          if (row) {
            row.classList.toggle('selected-file-row', selectedFileNames.has(path));
          }
        });
        updateFileSelectAllState();
        updateFileBulkActionButton();
      }

      function isLocalDiskDirectoryPath(path) {
        const item = getLocalDiskItemByPath(path) || getLocalDiskTreeItemByPath(path);
        return !!(item && item.directory);
      }

      function centerPreviewWindow(win) {
        if (!win) {
          return;
        }

        function placeWindow() {
          win.style.transform = 'none';
          const count = previewLayer.querySelectorAll('.floating-preview').length;
          const rect = win.getBoundingClientRect();
          const w = rect.width || win.offsetWidth || 760;
          const h = rect.height || win.offsetHeight || 520;
          const offset = (count - 1) * 24;
          const left = Math.max(8, Math.round((window.innerWidth - w) / 2));
          const top = Math.max(8, Math.round((window.innerHeight - h) / 2));
          win.style.left = (left + offset) + 'px';
          win.style.top = (top + offset) + 'px';
          clampWindowPosition(win);
        }

        requestAnimationFrame(placeWindow);
      }

      function handleLocalDiskDragEnd() {
        activeLocalDiskDragPaths = [];
        clearLocalDiskDropTarget();
        if (localDiskExplorer) {
          localDiskExplorer.querySelectorAll('.local-disk-dragging').forEach(function (node) {
            node.classList.remove('local-disk-dragging');
          });
        }
        if (localDiskList) {
          localDiskList.querySelectorAll('.local-disk-dragging').forEach(function (node) {
            node.classList.remove('local-disk-dragging');
          });
        }
      }

      async function ensureFolderUnlocked(path) {
        const lockedPath = getFolderLockAncestorPath(path);
        if (!lockedPath) {
          return true;
        }
        if (unlockedFolderPasswords.has(lockedPath)) {
          return true;
        }
        const password = await askLockPassword({
          title: t('解锁目录'),
          description: t('请输入目录「') + lockedPath + t('」的锁密码。'),
          onSubmit: async function (passwordText) {
            await fetchJson(
              api.folderLockVerify + '?path=' + encodeURIComponent(path) + '&password=' + encodeURIComponent(passwordText),
              { method: 'POST' }
            );
          }
        });
        if (password === null) {
          return false;
        }
        setUnlockedFolderPassword(lockedPath, password);
        return true;
      }

      function renderAudioPlayModeButtons(activeMode) {
        return ['random', 'sequential', 'loop'].map(function (modeKey) {
          const activeClass = modeKey === activeMode ? 'audio-mode-btn active' : 'audio-mode-btn';
          const label = AUDIO_PLAY_MODE_LABELS[modeKey] || t('播放方式');
          const icon = AUDIO_PLAY_MODE_ICONS[modeKey] || '?';
          return '<button type="button" class="' + activeClass + '" data-audio-mode="' + modeKey + '" title="' + escapeHtml(label) + '" aria-label="' + escapeHtml(label) + '">' +
            '<span class="audio-mode-btn-icon">' + icon + '</span>' +
          '</button>';
        }).join('');
      }

      async function removeTagNode(tagId) {
        try {
          return await fetchJson(api.tagDelete + '?id=' + encodeURIComponent(tagId), {
            method: 'POST'
          });
        } catch (_) {
          return null;
        }
      }

      async function createFolderAtPath(parentPath) {
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
        ensureFolderPathExpanded(targetPath);
        await loadFolderTreeState();
        showStatus(t('已创建文件夹：') + cleanName, 'ok');
      }

      async function createFolderAtCurrentPath() {
        await createFolderAtPath(activeFolderPath);
      }

      function downloadRemoteListFile(filePath, local) {
        const file = String(filePath || '');
        if (!file) {
          return;
        }
        const link = document.createElement('a');
        link.href = local ? localDiskDownloadUrl(file) : downloadUrlForFile(file, false);
        link.style.display = 'none';
        document.body.appendChild(link);
        link.click();
        document.body.removeChild(link);
      }

      function localBaseName(path) {
        const text = String(path || '').replace(/\/+$/, '');
        const index = text.lastIndexOf('/');
        return index >= 0 ? text.slice(index + 1) : text;
      }

      function clampWindowPosition(win) {
        if (!win) {
          return;
        }

        const rect = win.getBoundingClientRect();
        const maxLeft = Math.max(8, window.innerWidth - rect.width - 8);
        const maxTop = Math.max(8, window.innerHeight - 42);
        let left = rect.left;
        let top = rect.top;

        if (left < 8) left = 8;
        if (left > maxLeft) left = maxLeft;

        if (top < 8) top = 8;
        if (top > maxTop) top = maxTop;

        win.style.left = Math.round(left) + 'px';
        win.style.top = Math.round(top) + 'px';
      }

function loadUnlockedFolderPasswords() {
        try {
          const raw = sessionStorage.getItem(FOLDER_UNLOCK_SESSION_STORAGE_KEY);
          const entries = raw ? JSON.parse(raw) : [];
          if (!Array.isArray(entries)) {
            return;
          }
          entries.forEach(function (entry) {
            if (Array.isArray(entry) && entry[0] && entry[1]) {
              unlockedFolderPasswords.set(String(entry[0]), String(entry[1]));
            }
          });
        } catch (_) {}
      }

      async function relockFolderInSession(path) {
        const target = String(path || '');
        if (!target || !unlockedFolderPasswords.has(target)) {
          return;
        }
        deleteUnlockedFolderPassword(target);
        await loadFolderTreeState();
        if (isSameOrChildFolderPath(target, activeFolderPath)) {
          renderFiles([]);
        } else {
          renderFiles(activeSourceFiles);
        }
        showStatus(t('目录已重新加锁：') + target, 'ok');
      }

      function askLockPassword(options) {
        if (!lockDialog || !lockDialogTitle || !lockDialogDesc || !lockDialogInput) {
          return Promise.resolve(null);
        }
        if (activeLockDialogState) {
          closeLockDialog(null);
        }

        const opts = options || {};
        lockDialogTitle.textContent = String(opts.title || t('解锁目录'));
        lockDialogDesc.textContent = String(opts.description || t('请输入目录锁密码。'));
        lockDialogInput.value = '';
        lockDialogInput.placeholder = String(opts.placeholder || t('请输入锁密码'));
        setLockDialogError('');
        lockDialog.hidden = false;
        document.body.style.overflow = 'hidden';

        requestAnimationFrame(function () {
          lockDialogInput.focus();
          lockDialogInput.select();
        });

        return new Promise(function (resolve) {
          activeLockDialogState = {
            resolve: resolve,
            onSubmit: typeof opts.onSubmit === 'function' ? opts.onSubmit : null,
            errorMessage: String(opts.errorMessage || t('密码错误或验证失败，请重新输入。')),
            statusErrorMessage: String(opts.statusErrorMessage || t('解锁失败：密码错误或验证失败'))
          };
        });
      }

      function buildTranscodeActionButtons(encoded, item) {
        if (isAudioSplitChoiceCandidate(item)) {
          return '' +
            '<button type="button" class="transcode-btn" data-transcode-file="' + encoded + '" data-transcode-mode="audio_split">' + t('拆分视频并转音频') + '</button>' +
            '<button type="button" class="transcode-btn" data-transcode-file="' + encoded + '" data-transcode-mode="full_mp4">' + t('音视频都转') + '</button>';
        }
        return '<button type="button" class="transcode-btn" data-transcode-file="' + encoded + '" data-transcode-mode="auto">' + t('确认转码') + '</button>';
      }

      function buildPreviewCompatButtons(encoded, item) {
        const modeAttr = function (mode) {
          return ' data-transcode-mode="' + mode + '"';
        };
        if (isAudioSplitChoiceCandidate(item)) {
          return '' +
            '<button type="button" class="preview-convert-btn" data-preview-transcode-file="' + encoded + '"' + modeAttr('audio_split') + '>' + t('拆分并转音频') + '</button>' +
            '<button type="button" class="preview-convert-btn" data-preview-transcode-file="' + encoded + '"' + modeAttr('full_mp4') + '>' + t('音视频都转') + '</button>';
        }
        return '<button type="button" class="preview-convert-btn" data-preview-transcode-file="' + encoded + '"' + modeAttr('auto') + '>' + t('转换为MP4') + '</button>';
      }

      function wirePreviewCompatButtons(tools, rawVideoFile, item) {
        if (!tools) {
          return;
        }
        tools.querySelectorAll('[data-preview-transcode-file]').forEach(function (btn) {
          btn.addEventListener('click', async function () {
            const encoded = btn.getAttribute('data-preview-transcode-file');
            const mode = btn.getAttribute('data-transcode-mode') || 'auto';
            if (!encoded) {
              return;
            }
            tools.querySelectorAll('[data-preview-transcode-file]').forEach(function (itemBtn) {
              itemBtn.disabled = true;
            });
            const hint = tools.querySelector('[data-preview-compat-hint]');
            if (hint) {
              hint.textContent = t('正在启动转码任务...');
            }
            showManualTranscodePrompt([{
              name: rawVideoFile,
              reason: (item && item.reason) || t('浏览器无法直接播放此视频'),
              allowAudioSplitChoice: !!(item && item.allowAudioSplitChoice)
            }]);
            await startManualTranscode(encoded, mode);
          });
        });
      }

      function showPreviewVideoCompatTools(win, rawVideoFile, item) {
        const stack = win ? win.querySelector('.preview-video-stack') : null;
        if (!stack || !rawVideoFile) {
          return;
        }
        if (stack.querySelector('[data-preview-convert-video]')) {
          return;
        }
        let tools = stack.querySelector('[data-preview-compat-tools]');
        if (!tools) {
          tools = document.createElement('div');
          tools.className = 'preview-video-compat-tools';
          tools.setAttribute('data-preview-compat-tools', '1');
          stack.appendChild(tools);
        }
        const encoded = encodeURIComponent(rawVideoFile);
        const reason = String((item && item.reason) || t('浏览器无法直接播放此视频'));
        const audioSplitTip = isAudioSplitChoiceCandidate(item)
          ? ('<div class="preview-video-compat-tip">' +
            escapeHtml(t('提示：仅转音轨（拆分并转音频）速度更快，画面无需重新编码。')) +
            '</div>')
          : '';
        tools.hidden = false;
        tools.innerHTML =
          '<div class="preview-video-compat-reason">' + escapeHtml(reason) + '</div>' +
          audioSplitTip +
          '<div class="preview-video-compat-actions">' +
            buildPreviewCompatButtons(encoded, item) +
          '</div>' +
          '<div class="preview-video-compat-hint" data-preview-compat-hint>' +
            escapeHtml(t('转码完成后请关闭并重新打开播放窗口。')) +
          '</div>';
        wirePreviewCompatButtons(tools, rawVideoFile, item);
      }

      function offerPreviewVideoTranscode(win, rawVideoFile, candidate) {
        if (!win || !rawVideoFile || !candidate) {
          return;
        }
        if (win.__compatTranscodeOfferedFor === rawVideoFile) {
          return;
        }
        if (win.querySelector('[data-preview-convert-video]')) {
          return;
        }
        win.__compatTranscodeOfferedFor = rawVideoFile;
        showPreviewVideoCompatTools(win, rawVideoFile, {
          name: rawVideoFile,
          reason: candidate.reason || t('浏览器无法直接播放此视频'),
          allowAudioSplitChoice: !!candidate.allowAudioSplitChoice
        });
      }

      function bindPreviewVideoCompatHandlers(win, mediaVideo, rawVideoFile) {
        if (!win || !mediaVideo || !rawVideoFile) {
          return;
        }
        mediaVideo.addEventListener('loadedmetadata', function () {
          checkVideoAudio(rawVideoFile).then(function (probeResult) {
            if (!probeResult || !probeResult.ok || !probeResult.has_audio) {
              return;
            }
            if (probeResult.browser_audio_supported !== false) {
              return;
            }
            offerPreviewVideoTranscode(win, rawVideoFile, {
              reason: t('音频编码 ') + (probeResult.audio_codec || 'unknown')
                + t(' 浏览器不支持，可转码后播放'),
              allowAudioSplitChoice: true
            });
          });
        }, { once: true });
        mediaVideo.addEventListener('error', function () {
          offerPreviewVideoTranscode(win, rawVideoFile, {
            reason: t('浏览器无法播放该视频，可转码为 MP4 后再试'),
            allowAudioSplitChoice: false
          });
        }, { once: true });
      }

      function closeLocalImportDialog() {
        if (localImportDialog) {
          localImportDialog.hidden = true;
        }
        localImportOverridePaths = null;
      }

      function renderTagImagePreview(files) {
        if (!tagImagePreviewWrap) {
          return;
        }
        const imageFiles = (Array.isArray(files) ? files : []).filter(function (f) {
          return !f.directory && isImageName(f.name);
        });
        activeTagPreviewImages = imageFiles.map(function (f) {
          const rawName = getFilePath(f);
          return {
            file: rawName,
            name: String(f.name || rawName || ''),
            local: !!f.local
          };
        });
        if (!imageFiles.length) {
          tagImagePreviewWrap.innerHTML = '<p class="tag-preview-empty">\u5f53\u524d\u6807\u7b7e\u4e0b\u6ca1\u6709\u56fe\u7247\u3002</p>';
          tagImagePreviewWrap.hidden = false;
          return;
        }
        const items = activeTagPreviewImages.map(function (item, index) {
          const rawName = item.file;
          const url = item.local ? localDiskDownloadUrl(rawName) : downloadUrlForFile(rawName, true);
          const encodedPath = encodeURIComponent(rawName);
          return '<div class="tag-img-thumb" data-thumb-file="' + encodedPath + '" data-thumb-index="' + index + '" data-thumb-name="' + escapeHtml(item.name) + '" role="button" tabindex="0" title="' + escapeHtml(item.name) + '">' +
            '<img src="' + escapeHtml(url) + '" alt="' + escapeHtml(item.name) + '" loading="lazy">' +
            '<div class="tag-img-thumb-name">' + escapeHtml(item.name) + '</div>' +
            '</div>';
        });
        tagImagePreviewWrap.innerHTML = '<div class="tag-img-thumb-grid">' + items.join('') + '</div>';
        tagImagePreviewWrap.hidden = false;
      }

      function showLocalDiskFileSummaryDialog(path) {
        const filePath = String(path || '');
        const item = getLocalDiskItemByPath(filePath) || { path: filePath, name: localBaseName(filePath), size: 0 };
        const oldDialog = document.getElementById('file-summary-dialog');
        if (oldDialog && oldDialog.parentNode) {
          oldDialog.parentNode.removeChild(oldDialog);
        }
        const rows = [
          [t('文件名'), String(item.name || localBaseName(filePath) || '')],
          [t('文件大小'), formatNumber(Number(item.size || 0)) + t(' 字节')],
          [t('文件类型'), inferFileTypeLabel({ name: item.name || filePath, directory: false })],
          [t('创建时间'), String(item.created_time || '-')],
          [t('修改时间'), String(item.modified_time || '-')]
        ];
        const dialog = document.createElement('div');
        dialog.className = 'tag-dialog file-summary-dialog';
        dialog.id = 'file-summary-dialog';
        dialog.innerHTML =
          '<div class="tag-dialog-backdrop" data-file-summary-close="1"></div>' +
          '<div class="tag-dialog-card file-summary-card" role="dialog" aria-modal="true" aria-labelledby="file-summary-title">' +
            '<div class="tag-dialog-head">' +
              '<h2 id="file-summary-title">' + escapeHtml(t('文件摘要')) + '</h2>' +
              '<p>' + escapeHtml(filePath) + '</p>' +
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

      function setAdminStorageProgressControls(state) {
        const running = state === 'running' || state === 'conflict' || state === 'queued';
        const paused = state === 'paused';
        const active = running || paused;
        if (adminStorageProgressPauseBtn) {
          adminStorageProgressPauseBtn.hidden = !running;
          adminStorageProgressPauseBtn.disabled = !running;
        }
        if (adminStorageProgressResumeBtn) {
          adminStorageProgressResumeBtn.hidden = !paused;
          adminStorageProgressResumeBtn.disabled = !paused;
        }
        if (adminStorageProgressCancelBtn) {
          adminStorageProgressCancelBtn.hidden = !active;
          adminStorageProgressCancelBtn.disabled = !active;
        }
      }

      function getFilePassword(path, local) {
        return unlockedFilePasswords.get(fileLockKey(path, local)) || '';
      }

      function selectFolderPath(path, shiftKey) {
        const target = String(path || '');
        if (!target || isRecycleRootFolderPath(target)) {
          clearSelectedFolders();
          return;
        }
        const targetInRecycle = isRecycleFolderPath(target);
        if (shiftKey && lastSelectedFolderPath) {
          const visiblePaths = getVisibleFolderPathsInOrder().filter(function (item) {
            if (!item || isRecycleRootFolderPath(item)) {
              return false;
            }
            return targetInRecycle ? isRecycleFolderPath(item) : !isRecycleFolderPath(item);
          });
          const start = visiblePaths.indexOf(lastSelectedFolderPath);
          const end = visiblePaths.indexOf(target);
          selectedFolderPaths.clear();
          if (start >= 0 && end >= 0) {
            const min = Math.min(start, end);
            const max = Math.max(start, end);
            for (let i = min; i <= max; i += 1) {
              selectedFolderPaths.add(visiblePaths[i]);
            }
          } else {
            selectedFolderPaths.add(target);
          }
        } else {
          selectedFolderPaths.clear();
          selectedFolderPaths.add(target);
          lastSelectedFolderPath = target;
        }
      }

      function hasTagChildren(node) {
        return !!(node && Array.isArray(node.children) && node.children.length > 0);
      }

      function dismissTranscodeItem(encodedName) {
        stopTranscodePolling(encodedName);
        const item = statusBox.querySelector('[data-transcode-item="' + encodedName + '"]');
        if (item && item.parentNode) {
          item.parentNode.removeChild(item);
        }
        if (!statusBox.querySelector('[data-transcode-item]')) {
          statusBox.className = 'status';
          statusBox.textContent = '';
        }
      }

      function setLocalImportProgress(percent, text) {
        const p = Math.max(0, Math.min(100, Number(percent) || 0));
        if (localImportProgressDialog) {
          localImportProgressDialog.hidden = false;
        }
        if (localImportProgressFill) {
          localImportProgressFill.style.width = p + '%';
        }
        if (localImportProgressText) {
          localImportProgressText.textContent = text || (t('上传中 ') + Math.round(p) + '%');
        }
        updateLocalImportProgressSummary(localImportProgressText ? localImportProgressText.textContent : text);
        if (localImportProgressClose) {
          localImportProgressClose.hidden = true;
        }
      }

      function setLocalImportProgressControls(state) {
        const mode = String(localImportProgressWindowMode || '');
        const isImport = mode === 'local-import';
        const running = isImport && (state === 'running' || state === 'queued');
        const paused = isImport && state === 'paused';
        const active = running || paused;
        if (localImportProgressPause) {
          localImportProgressPause.hidden = !running;
          localImportProgressPause.disabled = !running;
        }
        if (localImportProgressResume) {
          localImportProgressResume.hidden = !paused;
          localImportProgressResume.disabled = !paused;
        }
        if (localImportProgressCancel) {
          localImportProgressCancel.hidden = !active && !isCopyTaskWindowMode(mode);
          if (isImport) {
            localImportProgressCancel.disabled = !active || activeLocalImportCancelRequested;
            localImportProgressCancel.textContent = activeLocalImportCancelRequested ? t('取消中...') : t('取消');
          }
        }
      }

      function splitAudioSidecarCandidates(filePath) {
        const text = String(filePath || '');
        const match = text.match(/^(.*)_web_video(_\d+)?\.mp4$/i);
        if (!match) {
          return [];
        }
        const base = match[1];
        const suffix = match[2] || '';
        return [base + '_web_audio' + suffix + '.m4a', base + '_web_audio' + suffix + '.aac'];
      }

      function updateLocalDiskSelectAllState() {
        const filePaths = getVisibleLocalDiskFilePaths();
        const selectedCount = filePaths.filter(function (path) {
          return selectedLocalDiskPaths.has(path);
        }).length;
        [localDiskSelectAll, localDiskTableSelectAll].forEach(function (selectAll) {
          if (!selectAll) {
            return;
          }
          selectAll.checked = filePaths.length > 0 && selectedCount === filePaths.length;
          selectAll.indeterminate = selectedCount > 0 && selectedCount < filePaths.length;
          selectAll.disabled = filePaths.length === 0;
        });
      }

      function adminStoragePickerUrl(path) {
        const target = String(path || '');
        return target
          ? (api.localDiskList + '?path=' + encodeURIComponent(target))
          : api.localDiskList;
      }

      function setUnlockedTagPassword(tagId, password) {
        const id = String(tagId || '');
        if (!id) {
          return;
        }
        unlockedFilePasswords.set(tagLockKey(id), String(password || ''));
        saveUnlockedFilePasswords();
      }

      function isImageName(name) {
        return /\.(png|jpg|jpeg|gif|heic|heif)$/i.test(String(name || ''));
      }

      async function openFilesTagMenu(button, fileNames, options) {
        closeFileTagMenu();
        const opts = options || {};
        const targetFiles = (Array.isArray(fileNames) ? fileNames : [])
          .map(function (name) { return String(name || ''); })
          .filter(Boolean);
        if (!button || !targetFiles.length) {
          return;
        }
        if (!tagTree.length) {
          await loadTagTreeState();
        }
        const menu = document.createElement('div');
        menu.className = 'quick-tag-menu';
        menu.setAttribute('data-quick-tag-files', targetFiles.join('\n'));
        menu.setAttribute('data-quick-tag-local', opts.local ? '1' : '0');
        menu.innerHTML = tagTree.length
          ? '<div class="quick-tag-title">' + (targetFiles.length > 1 ? (t('给 ') + targetFiles.length + t(' 个文件加入标签')) : t('加入标签')) + '</div>' + buildQuickTagTreeHtml(tagTree, targetFiles, 1)
          : '<div class="quick-tag-empty">' + t('当前没有标签') + '</div>';
        document.body.appendChild(menu);
        const rect = button.getBoundingClientRect();
        menu.style.left = Math.round(rect.left) + 'px';
        menu.style.top = Math.round(rect.bottom + 6) + 'px';
        clampFloatingMenuPosition(menu, rect.left, rect.bottom + 6);
        activeFileTagMenu = menu;
      }

      async function startLocalVideoTranscode(encodedName) {
        setTranscodeButtons(encodedName, { startDisabled: true, cancelDisabled: true });
        const fileName = decodeURIComponent(encodedName);
        try {
          setTranscodeVisualState(encodedName, 'running');
          updateTranscodeProgress(encodedName, 0, t('任务创建中...'));
          setTranscodeReason(encodedName, t('状态：正在请求后台启动转码任务'));
          let url = api.localDiskVideoConvert + '?path=' + encodeURIComponent(fileName);
          url = appendLocalDirPassword(appendFilePassword(url, fileName, true), localDiskParentPath(fileName));
          const data = await fetchJson(url, { method: 'POST' });
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

      async function cancelRemoteCopyTask() {
        const taskId = String(activeRemoteCopyTaskId || '');
        if (!taskId) {
          return;
        }
        activeRemoteCopyCancelRequested = true;
        if (localImportProgressCancel) {
          localImportProgressCancel.disabled = true;
          localImportProgressCancel.textContent = t('取消中...');
        }
        await fetchJson(api.remoteCopyCancel + '?task_id=' + encodeURIComponent(taskId), { method: 'POST' });
        showStatus(t('正在取消粘贴...'), 'warn');
      }

      async function controlLocalImportTask(action) {
        const taskId = String(activeLocalImportTaskId || '');
        if (!taskId) {
          return;
        }
        if (action === 'cancel') {
          activeLocalImportCancelRequested = true;
          if (localImportProgressCancel) {
            localImportProgressCancel.disabled = true;
            localImportProgressCancel.textContent = t('取消中...');
          }
        }
        await fetchJson(api.localDiskImportControl + '?task_id=' + encodeURIComponent(taskId) + '&action=' + encodeURIComponent(action), { method: 'POST' });
      }

      function imageEditMimeForFile(filePath) {
        const text = String(filePath || '').toLowerCase();
        if (/\.(jpg|jpeg)$/.test(text)) {
          return 'image/jpeg';
        }
        if (/\.png$/.test(text)) {
          return 'image/png';
        }
        return '';
      }

      function setFileLockedState(path, local, locked) {
        const target = String(path || '');
        const isLocal = !!local;
        if (!target) {
          return;
        }

        function updateList(list) {
          (Array.isArray(list) ? list : []).forEach(function (item) {
            if (!item) {
              return;
            }
            if (!!item.local === isLocal && getFilePath(item) === target) {
              item.locked = !!locked;
            }
          });
        }

        updateList(allFiles);
        updateList(activeSourceFiles);
        activeLocalDiskItems.forEach(function (item) {
          if (!item || item.directory) {
            return;
          }
          if (String(item.path || '') === target) {
            item.locked = !!locked;
          }
        });
      }

      function closeAdminStoragePickerDialog() {
        if (adminStoragePickerDialog) {
          adminStoragePickerDialog.hidden = true;
          document.body.style.overflow = '';
        }
      }

      function getFolderLabel(path) {
        return path ? path : t('根目录');
      }

      function isTextName(name) {
        const text = String(name || '');
        if (window.WebCoolHtml && window.WebCoolHtml.isHtmlName(text)) {
          return true;
        }
        return /\.(txt|md|markdown|mdown|mkdn|log|csv|json|xml|yaml|yml|ini|conf|c|h|cpp|hpp|cc|java|py|js|ts|sh|go|sql|proto)$/i.test(text);
      }

      function isOfficeName(name) {
        return /\.(doc|docx|xls|xlsx|csv|ppt|pptx|odt|ods|odp)$/i.test(String(name || ''));
      }

      function isXMindName(name) {
        return window.WebCoolXMindPreview
          ? window.WebCoolXMindPreview.isXMindName(name)
          : /\.xmind$/i.test(String(name || ''));
      }

      function findTagMetaById(tagId) {
        let found = null;
        walkTagNodes(tagTree, function (node, level, parent, parentList, index) {
          if (node.id === tagId) {
            found = {
              node: node,
              level: level,
              parent: parent,
              parentList: parentList,
              index: index
            };
            return true;
          }
          return false;
        }, 1, null, tagTree);
        return found;
      }

      function ensureFolderPathExpanded(path) {
        const text = String(path || '');
        expandedFolderPaths.add('');
        if (!text) {
          return;
        }
        const parts = text.split('/');
        let current = '';
        parts.forEach(function (part) {
          current = current ? (current + '/' + part) : part;
          expandedFolderPaths.add(current);
        });
      }

      function ensureFolderParentPathExpanded(path) {
        const text = String(path || '');
        expandedFolderPaths.add('');
        const index = text.lastIndexOf('/');
        if (index <= 0) {
          return;
        }
        ensureFolderPathExpanded(text.slice(0, index));
      }

      function updateFileViewContext() {
        if (!fileViewContext) {
          return;
        }

        if (activeFilterTagId) {
          const meta = findTagMetaById(activeFilterTagId);
          const tagName = meta && meta.node && meta.node.name
            ? (getRestrictedRootTagType(meta.node, meta.level) ? t(String(meta.node.name)) : String(meta.node.name))
            : t('当前标签');
          fileViewContext.textContent = t('当前视图：标签：') + tagName + t(' / 范围：标签内全部文件');
        } else {
          fileViewContext.textContent = t('当前视图：目录：') + getFolderLabel(activeFolderPath) + t(' / 范围：全部文件');
        }
      }

      function capturePreviewBaseCanvas(win) {
        const img = win ? win.querySelector('.preview-image') : null;
        if (!win || !img || !img.complete || !img.naturalWidth || !img.naturalHeight) {
          return null;
        }
        win.__imageBaseCanvas = drawImageElementToCanvas(img);
        win.__imageScale = 1;
        win.__imageCurrentWidth = win.__imageBaseCanvas.width;
        win.__imageCurrentHeight = win.__imageBaseCanvas.height;
        fitPreviewImageToWindow(win);
        return win.__imageBaseCanvas;
      }

      function renderLocalDiskTreeNode(path, level, itemMeta) {
        const textPath = String(path || '/');
        const encodedPath = encodeURIComponent(textPath);
        const name = localDiskDisplayName(textPath, itemMeta && itemMeta.name);
        const checked = selectedLocalDiskPaths.has(textPath) ? ' checked' : '';
        const isActive = textPath === activeLocalDiskPath;
        const isExpanded = expandedLocalDiskTreePaths.has(textPath);
        const hasCache = localDiskTreeCache.has(textPath);
        const dirs = localDiskTreeCache.get(textPath) || [];
        const isVirtualRoot = textPath === '/';
        const isDriveRoot = /^[A-Za-z]:[\/\\]?$/.test(textPath);
        const canMove = level > 0 && !isVirtualRoot && !isDriveRoot;
        const dirLocked = !!(itemMeta && itemMeta.locked);
        const canLockDir = !isVirtualRoot && !isDriveRoot;
        const dirLockIcon = canLockDir ? localDirLockIconHtml(textPath, dirLocked) : '';
        const createBtn = isVirtualRoot ? '' : '<button type="button" class="local-mkdir-btn local-disk-dir-create-inline" data-local-mkdir="' + encodedPath + '" data-local-name="' + escapeHtml(textPath) + '" title="新建子目录" aria-label="新建子目录">+</button>';
        const deleteBtn = level > 0 && !isDriveRoot && itemMeta && itemMeta.empty_directory
          ? '<button type="button" class="local-delete-btn local-disk-dir-delete-inline" data-local-delete="' + encodedPath + '" data-local-name="' + escapeHtml(textPath) + '" title="删除空目录" aria-label="删除空目录">-</button>'
          : '';
        const selectBox = canMove
          ? '<input class="local-disk-select" type="checkbox" data-local-select="' + encodedPath + '" aria-label="' + escapeHtml(t('选择 ') + name) + '"' + checked + '>'
          : '<span class="local-disk-select-placeholder"></span>';
        const dirContextAttrs = canLockDir
          ? ' data-local-dir-context="' + encodedPath + '" data-local-dir-locked="' + (dirLocked ? '1' : '0') + '"'
          : '';
        let html = '<div class="local-disk-tree-row local-disk-dir-item' + (canMove ? ' local-disk-draggable' : '') + (isActive ? ' active' : '') + '" ' + (canMove ? 'draggable="true" data-local-drag="' + encodedPath + '" ' : '') + 'data-local-drop-target="' + encodedPath + '"' + dirContextAttrs + ' style="--tree-level:' + level + '">' +
          '<button type="button" class="local-disk-tree-caret" data-local-toggle="' + encodedPath + '" title="展开或收起目录" aria-label="展开或收起目录">' + (isExpanded && dirs.length ? '▾' : (hasCache && !dirs.length ? '' : '▸')) + '</button>' +
          selectBox +
          '<button type="button" class="local-disk-dir-link" data-local-folder="' + encodedPath + '" title="' + escapeHtml(textPath) + '">' +
            '<span class="local-folder-icon">📁</span>' +
            '<span class="local-disk-dir-item-name">' + escapeHtml(name) + '</span>' +
          '</button>' +
          dirLockIcon +
          createBtn +
          deleteBtn +
          '</div>';
        if (isExpanded && dirs.length) {
          html += '<div class="local-disk-tree-children">';
          html += dirs.map(function (item) {
            return renderLocalDiskTreeNode(String(item.path || ''), level + 1, item);
          }).join('');
          html += '</div>';
        }
        return html;
      }

      function uploadWithProgress(formData) {
        return new Promise(function (resolve, reject) {
          const xhr = new XMLHttpRequest();
          let lastProgressTime = Date.now();
          let lastProgressLoaded = 0;
          let currentSpeed = 0;
          xhr.open('POST', api.upload, true);
          xhr.withCredentials = true;
          if (authState.token) {
            xhr.setRequestHeader('Authorization', 'Bearer ' + authState.token);
          }
          xhr.responseType = 'json';

          xhr.upload.onprogress = function (e) {
            const now = Date.now();
            const elapsed = Math.max(1, now - lastProgressTime);
            if (e.loaded >= lastProgressLoaded && elapsed >= 250) {
              currentSpeed = ((e.loaded - lastProgressLoaded) * 1000) / elapsed;
              lastProgressLoaded = e.loaded;
              lastProgressTime = now;
            } else if (currentSpeed <= 0 && e.loaded > 0) {
              currentSpeed = (e.loaded * 1000) / Math.max(1, now - lastProgressTime);
            }
            if (e.lengthComputable && e.total > 0) {
              const percent = Math.round((e.loaded * 100) / e.total);
              setUploadProgress(percent, appendSpeedText(t('上传中 ') + percent + '%', currentSpeed));
            } else {
              setUploadProgress(0, appendSpeedText(t('上传中...'), currentSpeed));
            }
          };

          xhr.onerror = function () {
            reject(new Error('network error'));
          };

          xhr.onabort = function () {
            activeUploadRunning = false;
            reject(new Error(activeUploadPaused ? 'upload paused' : 'upload aborted'));
          };

          xhr.onload = function () {
            let data = xhr.response;
            if (!data || typeof data !== 'object') {
              try {
                data = JSON.parse(xhr.responseText || '{}');
              } catch (_) {
                data = { ok: false, error: 'invalid json response' };
              }
            }

            if (xhr.status < 200 || xhr.status >= 300 || !data.ok) {
              reject(new Error(data.error || ('http ' + xhr.status)));
              return;
            }

            resolve(data);
          };

          activeUploadXhr = xhr;
          activeUploadRunning = true;
          xhr.send(formData);
        });
      }

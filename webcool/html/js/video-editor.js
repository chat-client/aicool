// Video editor dialog and FFmpeg export task client.

function videoEditorEscape(value) {
  return String(value == null ? '' : value)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

function formatVideoEditorTime(seconds) {
  const safe = Math.max(0, Number(seconds) || 0);
  const whole = Math.floor(safe);
  const hours = Math.floor(whole / 3600);
  const minutes = Math.floor((whole % 3600) / 60);
  const secs = whole % 60;
  const fraction = Math.floor((safe - whole) * 10);
  return (hours ? String(hours).padStart(2, '0') + ':' : '')
    + String(minutes).padStart(2, '0') + ':' + String(secs).padStart(2, '0')
    + '.' + fraction;
}

function videoEditorMediaUrl(path, local) {
  if (local) {
    return appendLocalDirPassword(
      appendFilePassword(api.localDiskDownload + '?path=' + encodeURIComponent(path), path, true),
      localDiskParentPath(path)
    );
  }
  return appendFilePassword(
    withFolderPassword(api.download + '?preview=1&file=' + encodeURIComponent(path), parentFolderPathFromFilePath(path)),
    path,
    false
  );
}

function videoEditorTaskUrl(base, path, local) {
  let url = base + '?' + (local ? 'path=' : 'file=') + encodeURIComponent(path);
  if (local) {
    url = appendLocalDirPassword(appendFilePassword(url, path, true), localDiskParentPath(path));
  } else {
    url = appendFilePassword(withFolderPassword(url, parentFolderPathFromFilePath(path)), path, false);
  }
  return url;
}

function openVideoEditor(path, local) {
  const old = document.getElementById('video-editor-dialog');
  if (old && old.parentNode) old.parentNode.removeChild(old);

  const dialog = document.createElement('div');
  dialog.id = 'video-editor-dialog';
  dialog.className = 'video-editor-dialog';
  dialog.innerHTML =
    '<div class="video-editor-backdrop" data-video-editor-close></div>' +
    '<section class="video-editor-window" role="dialog" aria-modal="true" aria-labelledby="video-editor-title">' +
      '<header class="video-editor-header"><div><h2 id="video-editor-title">' + videoEditorEscape(t('视频剪辑')) + '</h2>' +
        '<p title="' + videoEditorEscape(path) + '">' + videoEditorEscape(path) + '</p></div>' +
        '<button type="button" class="video-editor-close" data-video-editor-close title="' + videoEditorEscape(t('关闭')) + '" aria-label="' + videoEditorEscape(t('关闭')) + '">×</button>' +
      '</header>' +
      '<div class="video-editor-main">' +
        '<div class="video-editor-preview-column">' +
          '<div class="video-editor-stage"><video controls playsinline preload="metadata" src="' + videoEditorEscape(videoEditorMediaUrl(path, local)) + '"></video></div>' +
          '<div class="video-editor-transport">' +
            '<button type="button" data-editor-play-selection>▶ ' + videoEditorEscape(t('播放选区')) + '</button>' +
            '<button type="button" data-editor-set-start>' + videoEditorEscape(t('设为起点')) + '</button>' +
            '<button type="button" data-editor-set-end>' + videoEditorEscape(t('设为终点')) + '</button>' +
            '<span data-editor-current>00:00.0</span>' +
          '</div>' +
          '<div class="video-editor-timeline">' +
            '<div class="video-editor-timeline-head"><strong>' + videoEditorEscape(t('裁剪时段')) + '</strong><span data-editor-selection-label>--</span></div>' +
            '<label><span>' + videoEditorEscape(t('开始')) + '</span><input type="range" min="0" max="1" step="0.1" value="0" data-editor-start-range><input type="number" min="0" step="0.1" value="0" data-editor-start-number><em>s</em></label>' +
            '<label><span>' + videoEditorEscape(t('结束')) + '</span><input type="range" min="0" max="1" step="0.1" value="1" data-editor-end-range><input type="number" min="0" step="0.1" value="0" data-editor-end-number><em>s</em></label>' +
          '</div>' +
        '</div>' +
        '<aside class="video-editor-controls">' +
          '<div class="video-editor-control-group"><h3>' + videoEditorEscape(t('速度与声音')) + '</h3>' +
            '<label><span>' + videoEditorEscape(t('播放速度')) + '</span><select data-editor-speed><option value="0.5">0.5×</option><option value="0.75">0.75×</option><option value="1" selected>1×</option><option value="1.25">1.25×</option><option value="1.5">1.5×</option><option value="2">2×</option><option value="4">4×</option></select></label>' +
            '<label><span>' + videoEditorEscape(t('音量')) + '</span><input type="range" min="0" max="200" value="100" data-editor-volume><output data-editor-volume-label>100%</output></label>' +
            '<label class="video-editor-check"><input type="checkbox" data-editor-muted><span>' + videoEditorEscape(t('静音')) + '</span></label>' +
          '</div>' +
          '<div class="video-editor-control-group"><h3>' + videoEditorEscape(t('画面')) + '</h3>' +
            '<label><span>' + videoEditorEscape(t('旋转')) + '</span><select data-editor-rotate><option value="0">0°</option><option value="90">90°</option><option value="180">180°</option><option value="270">270°</option></select></label>' +
            '<div class="video-editor-toggle-row"><label class="video-editor-check"><input type="checkbox" data-editor-flip-h><span>' + videoEditorEscape(t('水平翻转')) + '</span></label><label class="video-editor-check"><input type="checkbox" data-editor-flip-v><span>' + videoEditorEscape(t('垂直翻转')) + '</span></label></div>' +
            '<label><span>' + videoEditorEscape(t('画面比例')) + '</span><select data-editor-crop><option value="original">' + videoEditorEscape(t('原始比例')) + '</option><option value="16:9">16:9</option><option value="9:16">9:16</option><option value="1:1">1:1</option></select></label>' +
            '<label><span>' + videoEditorEscape(t('导出分辨率')) + '</span><select data-editor-height><option value="0">' + videoEditorEscape(t('保持原始')) + '</option><option value="1080">1080p</option><option value="720">720p</option><option value="480">480p</option></select></label>' +
          '</div>' +
          '<p class="video-editor-output-hint">' + videoEditorEscape(t('将导出为新的 MP4 文件，原视频不会被修改。')) + '</p>' +
        '</aside>' +
      '</div>' +
      '<footer class="video-editor-footer"><div class="video-editor-progress" hidden><div><i></i></div><span>' + videoEditorEscape(t('准备导出')) + '</span></div>' +
        '<div class="video-editor-actions"><button type="button" data-editor-cancel-export hidden>' + videoEditorEscape(t('取消导出')) + '</button><button type="button" data-video-editor-close>' + videoEditorEscape(t('取消')) + '</button><button type="button" class="primary" data-editor-export>' + videoEditorEscape(t('导出视频')) + '</button></div></footer>' +
    '</section>';
  document.body.appendChild(dialog);

  const video = dialog.querySelector('video');
  const stage = dialog.querySelector('.video-editor-stage');
  const startRange = dialog.querySelector('[data-editor-start-range]');
  const endRange = dialog.querySelector('[data-editor-end-range]');
  const startNumber = dialog.querySelector('[data-editor-start-number]');
  const endNumber = dialog.querySelector('[data-editor-end-number]');
  const selectionLabel = dialog.querySelector('[data-editor-selection-label]');
  const currentLabel = dialog.querySelector('[data-editor-current]');
  const speed = dialog.querySelector('[data-editor-speed]');
  const volume = dialog.querySelector('[data-editor-volume]');
  const muted = dialog.querySelector('[data-editor-muted]');
  const rotate = dialog.querySelector('[data-editor-rotate]');
  const flipH = dialog.querySelector('[data-editor-flip-h]');
  const flipV = dialog.querySelector('[data-editor-flip-v]');
  const crop = dialog.querySelector('[data-editor-crop]');
  const outputHeight = dialog.querySelector('[data-editor-height]');
  const exportBtn = dialog.querySelector('[data-editor-export]');
  const cancelExportBtn = dialog.querySelector('[data-editor-cancel-export]');
  const progress = dialog.querySelector('.video-editor-progress');
  const progressBar = progress.querySelector('i');
  const progressText = progress.querySelector('span');
  let duration = 0;
  let exporting = false;
  let completed = false;
  let taskId = '';
  let stopAtEnd = false;

  function selection() {
    let start = Math.max(0, Math.min(Math.max(0, duration - 0.1), Number(startNumber.value) || 0));
    let end = Math.max(0, Math.min(duration, Number(endNumber.value) || duration));
    if (end < start + 0.1) end = Math.min(duration, start + 0.1);
    return { start: start, end: end };
  }

  function syncSelection(source) {
    if (!duration) return;
    if (source === startRange) startNumber.value = Number(startRange.value).toFixed(1);
    if (source === endRange) endNumber.value = Number(endRange.value).toFixed(1);
    const value = selection();
    startNumber.value = value.start.toFixed(1);
    endNumber.value = value.end.toFixed(1);
    startRange.value = String(value.start);
    endRange.value = String(value.end);
    selectionLabel.textContent = formatVideoEditorTime(value.start) + ' — ' + formatVideoEditorTime(value.end)
      + '  (' + formatVideoEditorTime(value.end - value.start) + ')';
  }

  function applyPreview() {
    const x = flipH.checked ? -1 : 1;
    const y = flipV.checked ? -1 : 1;
    if (rotate.value === '0' && x === 1 && y === 1) {
      video.style.removeProperty('transform');
    } else {
      video.style.transform = 'rotate(' + rotate.value + 'deg) scale(' + x + ',' + y + ')';
    }
    video.playbackRate = Number(speed.value) || 1;
    video.muted = muted.checked;
    video.volume = Math.max(0, Math.min(1, (Number(volume.value) || 0) / 100));
    stage.setAttribute('data-crop', crop.value);
    dialog.querySelector('[data-editor-volume-label]').textContent = volume.value + '%';
  }

  function closeEditor() {
    if (exporting) return;
    video.pause();
    dialog.remove();
  }

  video.addEventListener('loadedmetadata', function () {
    duration = Number(video.duration) || 0;
    [startRange, endRange, startNumber, endNumber].forEach(function (input) { input.max = String(duration); });
    endRange.value = String(duration);
    endNumber.value = duration.toFixed(1);
    syncSelection();
  });
  video.addEventListener('timeupdate', function () {
    currentLabel.textContent = formatVideoEditorTime(video.currentTime);
    if (stopAtEnd && video.currentTime >= selection().end) {
      stopAtEnd = false;
      video.pause();
    }
  });
  [startRange, endRange].forEach(function (input) { input.addEventListener('input', function () { syncSelection(input); }); });
  [startNumber, endNumber].forEach(function (input) { input.addEventListener('change', function () { syncSelection(input); }); });
  [speed, volume, muted, rotate, flipH, flipV, crop].forEach(function (input) { input.addEventListener('input', applyPreview); input.addEventListener('change', applyPreview); });
  dialog.querySelector('[data-editor-set-start]').addEventListener('click', function () { startNumber.value = video.currentTime.toFixed(1); syncSelection(startNumber); });
  dialog.querySelector('[data-editor-set-end]').addEventListener('click', function () { endNumber.value = video.currentTime.toFixed(1); syncSelection(endNumber); });
  dialog.querySelector('[data-editor-play-selection]').addEventListener('click', function () {
    video.currentTime = selection().start; stopAtEnd = true; video.play().catch(function () {});
  });
  dialog.querySelectorAll('[data-video-editor-close]').forEach(function (button) { button.addEventListener('click', closeEditor); });

  function updateProgress(value, message, state) {
    progress.hidden = false;
    progressBar.style.width = Math.max(0, Math.min(100, Number(value) || 0)) + '%';
    progressText.textContent = message || '';
    progress.classList.toggle('failed', state === 'failed');
    progress.classList.toggle('done', state === 'done');
  }

  async function pollTask() {
    const base = local ? api.localDiskVideoEditProgress : api.videoEditProgress;
    const data = await fetchJson(base + '?task_id=' + encodeURIComponent(taskId));
    const value = Math.round(Number(data.progress) || 0);
    updateProgress(value, data.message || t('正在导出视频'));
    if (!data.done) {
      await new Promise(function (resolve) { window.setTimeout(resolve, 800); });
      return pollTask();
    }
    exporting = false;
    cancelExportBtn.hidden = true;
    exportBtn.disabled = false;
    if (!data.success) {
      updateProgress(value, data.cancel_requested ? t('已取消') : (data.error || data.message || t('导出失败')), 'failed');
      return;
    }
    updateProgress(100, t('导出完成：') + String(data.name || ''), 'done');
    completed = true;
    exportBtn.textContent = t('完成');
    if (local) await loadLocalDisk(activeLocalDiskPath || localDiskParentPath(path) || '');
    else await loadFiles();
    showStatus(t('视频剪辑完成：') + String(data.name || ''), 'ok');
  }

  exportBtn.addEventListener('click', async function () {
    if (completed) {
      closeEditor();
      return;
    }
    if (exporting || !duration) return;
    const selected = selection();
    exporting = true;
    exportBtn.disabled = true;
    cancelExportBtn.hidden = false;
    updateProgress(0, t('正在启动导出'));
    try {
      let url = videoEditorTaskUrl(local ? api.localDiskVideoEdit : api.videoEdit, path, local);
      const params = {
        start_ms: Math.round(selected.start * 1000), end_ms: Math.round(selected.end * 1000),
        speed: speed.value, volume: volume.value, muted: muted.checked ? 1 : 0,
        rotate: rotate.value, flip_h: flipH.checked ? 1 : 0, flip_v: flipV.checked ? 1 : 0,
        crop: crop.value, output_height: outputHeight.value
      };
      Object.keys(params).forEach(function (key) { url += '&' + key + '=' + encodeURIComponent(params[key]); });
      const data = await fetchJson(url, { method: 'POST' });
      taskId = String(data.task_id || '');
      if (!taskId) throw new Error(data.message || t('无法启动视频剪辑'));
      await pollTask();
    } catch (err) {
      exporting = false;
      exportBtn.disabled = false;
      cancelExportBtn.hidden = true;
      updateProgress(0, t('导出失败：') + err.message, 'failed');
    }
  });

  cancelExportBtn.addEventListener('click', function () {
    if (!taskId) return;
    cancelExportBtn.disabled = true;
    const base = local ? api.localDiskVideoEditCancel : api.videoEditCancel;
    fetchJson(base + '?task_id=' + encodeURIComponent(taskId), { method: 'POST' })
      .then(function () { updateProgress(null, t('取消中')); })
      .catch(function (err) { updateProgress(null, t('取消失败：') + err.message, 'failed'); cancelExportBtn.disabled = false; });
  });

  applyPreview();
}

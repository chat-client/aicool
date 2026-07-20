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

function formatVideoEditorElapsed(milliseconds) {
  const totalSeconds = Math.max(0, Math.floor((Number(milliseconds) || 0) / 1000));
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  return String(hours).padStart(2, '0') + ':'
    + String(minutes).padStart(2, '0') + ':'
    + String(seconds).padStart(2, '0');
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

function videoEditorTrackCandidates(local, pattern) {
  const list = local ? activeLocalDiskItems : allFiles;
  const values = [];
  (Array.isArray(list) ? list : []).forEach(function (item) {
    if (!item || item.directory) return;
    const value = String(item.path || item.name || '');
    if (value && pattern.test(value) && values.indexOf(value) < 0) values.push(value);
  });
  return values.sort().map(function (value) {
    return '<option value="' + videoEditorEscape(value) + '"></option>';
  }).join('');
}

function openVideoEditor(path, local) {
  const old = document.getElementById('video-editor-dialog');
  if (old && old.parentNode) old.parentNode.removeChild(old);

  const dialog = document.createElement('div');
  const audioCandidates = videoEditorTrackCandidates(local, /\.(mp3|m4a|aac|wav|ogg|flac)$/i);
  const subtitleCandidates = videoEditorTrackCandidates(local, /\.(srt|vtt|ass|ssa)$/i);
  dialog.id = 'video-editor-dialog';
  dialog.className = 'video-editor-dialog';
  dialog.innerHTML =
    '<div class="video-editor-backdrop" data-video-editor-close></div>' +
    '<section class="video-editor-window" role="dialog" aria-modal="true" aria-labelledby="video-editor-title">' +
      '<header class="video-editor-header"><div><h2 id="video-editor-title">' + videoEditorEscape(t('视频剪辑')) + '</h2>' +
        '<p title="' + videoEditorEscape(path) + '">' + videoEditorEscape(path) + '</p></div>' +
        '<div class="video-editor-window-actions"><button type="button" data-editor-window-minimize title="' + videoEditorEscape(t('最小化')) + '" aria-label="' + videoEditorEscape(t('最小化')) + '">—</button><button type="button" data-editor-window-maximize title="' + videoEditorEscape(t('最大化')) + '" aria-label="' + videoEditorEscape(t('最大化')) + '">□</button><button type="button" class="video-editor-close" data-video-editor-close title="' + videoEditorEscape(t('关闭')) + '" aria-label="' + videoEditorEscape(t('关闭')) + '">×</button></div>' +
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
          '<div class="video-editor-control-group"><h3>' + videoEditorEscape(t('音轨')) + '</h3>' +
            '<label><span>' + videoEditorEscape(t('音频处理')) + '</span><select data-editor-audio-mode><option value="keep">' + videoEditorEscape(t('保留原音频')) + '</option><option value="remove">' + videoEditorEscape(t('去除音频')) + '</option><option value="replace">' + videoEditorEscape(t('添加或替换音频')) + '</option></select></label>' +
            '<div class="video-editor-track-source" data-editor-audio-source hidden><label><span>' + videoEditorEscape(t('音频文件')) + '</span><input type="text" list="video-editor-audio-files" data-editor-audio-file placeholder="' + videoEditorEscape(t('选择当前磁盘中的音频文件')) + '"></label><label><span>' + videoEditorEscape(t('起始位置')) + '</span><input type="number" min="0" step="0.1" value="0" data-editor-audio-start><em>s</em></label></div>' +
            '<button type="button" class="video-editor-track-export" data-editor-export-audio>' + videoEditorEscape(t('导出原音频')) + '</button>' +
          '</div>' +
          '<div class="video-editor-control-group"><h3>' + videoEditorEscape(t('字幕轨')) + '</h3>' +
            '<label><span>' + videoEditorEscape(t('字幕处理')) + '</span><select data-editor-subtitle-mode><option value="keep">' + videoEditorEscape(t('保留原字幕')) + '</option><option value="remove">' + videoEditorEscape(t('去除字幕')) + '</option><option value="replace">' + videoEditorEscape(t('添加或替换字幕')) + '</option></select></label>' +
            '<div class="video-editor-track-source" data-editor-subtitle-source hidden><label><span>' + videoEditorEscape(t('字幕文件')) + '</span><span class="video-editor-file-picker"><input type="text" list="video-editor-subtitle-files" data-editor-subtitle-file placeholder="' + videoEditorEscape(t('选择字幕文件位置')) + '"><button type="button" data-editor-subtitle-browse>' + videoEditorEscape(t('选择文件')) + '</button><input type="file" accept=".srt,.vtt,.ass,.ssa,text/vtt,application/x-subrip" data-editor-subtitle-upload hidden></span></label><label><span>' + videoEditorEscape(t('添加方式')) + '</span><select data-editor-subtitle-import-mode><option value="fast">' + videoEditorEscape(t('快速封装（不重新编码）')) + '</option><option value="reencode">' + videoEditorEscape(t('兼容转码（应用全部编辑）')) + '</option></select></label><p class="video-editor-track-hint" data-editor-subtitle-import-hint>' + videoEditorEscape(t('快速封装速度快，但不能同时应用画面、变速或音频编辑；裁剪点可能对齐到关键帧。')) + '</p><label><span>' + videoEditorEscape(t('起始位置')) + '</span><input type="number" min="0" step="0.1" value="0" data-editor-subtitle-start><em>s</em></label></div>' +
            '<button type="button" class="video-editor-track-export" data-editor-export-subtitle>' + videoEditorEscape(t('导出原字幕')) + '</button>' +
          '</div>' +
          '<div class="video-editor-control-group"><h3>' + videoEditorEscape(t('画面')) + '</h3>' +
            '<label><span>' + videoEditorEscape(t('旋转')) + '</span><select data-editor-rotate><option value="0">0°</option><option value="90">90°</option><option value="180">180°</option><option value="270">270°</option></select></label>' +
            '<div class="video-editor-toggle-row"><label class="video-editor-check"><input type="checkbox" data-editor-flip-h><span>' + videoEditorEscape(t('水平翻转')) + '</span></label><label class="video-editor-check"><input type="checkbox" data-editor-flip-v><span>' + videoEditorEscape(t('垂直翻转')) + '</span></label></div>' +
            '<label><span>' + videoEditorEscape(t('画面比例')) + '</span><select data-editor-crop><option value="original">' + videoEditorEscape(t('原始比例')) + '</option><option value="16:9">16:9</option><option value="9:16">9:16</option><option value="1:1">1:1</option></select></label>' +
            '<label><span>' + videoEditorEscape(t('导出分辨率')) + '</span><select data-editor-height><option value="0">' + videoEditorEscape(t('保持原始')) + '</option><option value="1080">1080p</option><option value="720">720p</option><option value="480">480p</option></select></label>' +
            '<label><span>' + videoEditorEscape(t('增强方式')) + '</span><select data-editor-screenshot-mode><option value="original">' + videoEditorEscape(t('原始画面')) + '</option><option value="sharpen">' + videoEditorEscape(t('锐化增强')) + '</option><option value="ai">' + videoEditorEscape(t('AI超分辨率')) + '</option></select></label>' +
            '<div class="video-editor-capture-actions"><button type="button" class="video-editor-track-export" data-editor-export-keyframes>' + videoEditorEscape(t('连续截取关键帧')) + '</button><button type="button" class="video-editor-track-export" data-editor-screenshot>' + videoEditorEscape(t('截屏')) + '</button><button type="button" class="video-editor-track-export video-editor-ai-settings" data-editor-screenshot-ai-settings hidden>' + videoEditorEscape(t('详细设置')) + '</button></div>' +
            '<button type="button" class="video-editor-main-export" data-editor-export>' + videoEditorEscape(t('导出视频')) + '</button>' +
          '</div>' +
          '<p class="video-editor-output-hint">' + videoEditorEscape(t('将导出为新的 MP4 文件，原视频不会被修改。')) + '</p>' +
        '</aside>' +
      '</div>' +
      '<footer class="video-editor-footer" hidden><div class="video-editor-progress" hidden><div><i></i></div><span>' + videoEditorEscape(t('准备导出')) + '</span></div>' +
        '<div class="video-editor-actions"><button type="button" data-editor-cancel-export hidden>' + videoEditorEscape(t('取消导出')) + '</button></div></footer>' +
      '<datalist id="video-editor-audio-files">' + audioCandidates + '</datalist><datalist id="video-editor-subtitle-files">' + subtitleCandidates + '</datalist>' +
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
  const audioMode = dialog.querySelector('[data-editor-audio-mode]');
  const audioSource = dialog.querySelector('[data-editor-audio-source]');
  const audioFile = dialog.querySelector('[data-editor-audio-file]');
  const audioStart = dialog.querySelector('[data-editor-audio-start]');
  const subtitleMode = dialog.querySelector('[data-editor-subtitle-mode]');
  const subtitleSource = dialog.querySelector('[data-editor-subtitle-source]');
  const subtitleFile = dialog.querySelector('[data-editor-subtitle-file]');
  const subtitleBrowseBtn = dialog.querySelector('[data-editor-subtitle-browse]');
  const subtitleUploadInput = dialog.querySelector('[data-editor-subtitle-upload]');
  const subtitleImportMode = dialog.querySelector('[data-editor-subtitle-import-mode]');
  const subtitleImportHint = dialog.querySelector('[data-editor-subtitle-import-hint]');
  const subtitleStart = dialog.querySelector('[data-editor-subtitle-start]');
  const exportBtn = dialog.querySelector('[data-editor-export]');
  const exportAudioBtn = dialog.querySelector('[data-editor-export-audio]');
  const exportSubtitleBtn = dialog.querySelector('[data-editor-export-subtitle]');
  const exportKeyframesBtn = dialog.querySelector('[data-editor-export-keyframes]');
  const screenshotBtn = dialog.querySelector('[data-editor-screenshot]');
  const screenshotMode = dialog.querySelector('[data-editor-screenshot-mode]');
  const screenshotAiSettingsBtn = dialog.querySelector('[data-editor-screenshot-ai-settings]');
  const cancelExportBtn = dialog.querySelector('[data-editor-cancel-export]');
  const footer = dialog.querySelector('.video-editor-footer');
  const progress = dialog.querySelector('.video-editor-progress');
  const progressBar = progress.querySelector('i');
  const progressText = progress.querySelector('span');
  let duration = 0;
  let exporting = false;
  let taskId = '';
  let activeCancelApi = '';
  let exportStartedAt = 0;
  let stopAtEnd = false;
  let selectedSubtitleUpload = null;
  let selectedSubtitleUploadPath = '';
  const screenshotSettings = {
    model: 'coreml-x2plus', scale: 2, denoise: 1, sharpen: 20,
    quality: 95, computeUnits: 'auto', tile: 0, overlap: 'balanced'
  };

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
  audioMode.addEventListener('change', function () { audioSource.hidden = audioMode.value !== 'replace'; });
  function syncSubtitleMode() {
    const adding = subtitleMode.value === 'replace';
    subtitleSource.hidden = !adding;
    exportSubtitleBtn.textContent = t(adding ? '开始添加字幕' : '导出原字幕');
  }
  subtitleMode.addEventListener('change', syncSubtitleMode);
  subtitleImportMode.addEventListener('change', function () {
    subtitleImportHint.textContent = t(subtitleImportMode.value === 'fast'
      ? '快速封装速度快，但不能同时应用画面、变速或音频编辑；裁剪点可能对齐到关键帧。'
      : '兼容转码会重新编码视频，可应用剪辑窗口中的全部设置。');
  });

  function openScreenshotAiSettings() {
    if (dialog.querySelector('.video-screenshot-ai-dialog')) return;
    const settingsDialog = document.createElement('div');
    settingsDialog.className = 'video-screenshot-ai-dialog';
    settingsDialog.innerHTML = '<section class="video-screenshot-ai-card" role="dialog" aria-modal="true" aria-label="' + videoEditorEscape(t('AI截图详细设置')) + '">' +
      '<header><h3>' + videoEditorEscape(t('AI截图详细设置')) + '</h3><button type="button" data-ai-screenshot-close aria-label="' + videoEditorEscape(t('关闭')) + '">×</button></header>' +
      '<div class="video-screenshot-ai-body">' +
        '<label><span>' + videoEditorEscape(t('AI模型')) + '</span><select data-ai-screenshot-model>' +
          '<option value="coreml-x2plus">' + videoEditorEscape(t('真实2×（高质量/较快）')) + '</option>' +
          '<option value="coreml-general-x4v3">' + videoEditorEscape(t('轻量x4（速度优先）')) + '</option>' +
          '<option value="coreml-general-x4v3-w8a8">' + videoEditorEscape(t('轻量W8A8 x4（M4实验）')) + '</option>' +
          '<option value="coreml-x4plus-int8">' + videoEditorEscape(t('M4量化x4（质量优先）')) + '</option>' +
          '<option value="realesrgan-x4plus">RealESRGAN x4plus</option>' +
          '<option value="realesr-animevideov3">RealESRGAN AnimeVideo v3</option>' +
        '</select></label>' +
        '<label><span>' + videoEditorEscape(t('AI放大倍数')) + '</span><select data-ai-screenshot-scale><option value="2">2×</option><option value="4">4×</option></select></label>' +
        '<label><span>' + videoEditorEscape(t('AI前降噪')) + '</span><select data-ai-screenshot-denoise><option value="0">' + videoEditorEscape(t('关闭')) + '</option><option value="1">' + videoEditorEscape(t('轻度')) + '</option><option value="2">' + videoEditorEscape(t('中度')) + '</option></select></label>' +
        '<label><span>' + videoEditorEscape(t('轻微去模糊/预锐化')) + '</span><span class="video-screenshot-ai-range"><input type="range" min="0" max="50" step="5" data-ai-screenshot-sharpen><output data-ai-screenshot-sharpen-value></output></span></label>' +
        '<label><span>' + videoEditorEscape(t('图片质量')) + '</span><span class="video-screenshot-ai-range"><input type="range" min="70" max="100" step="1" data-ai-screenshot-quality><output data-ai-screenshot-quality-value></output></span></label>' +
        '<label><span>' + videoEditorEscape(t('计算单元')) + '</span><select data-ai-screenshot-compute><option value="auto">' + videoEditorEscape(t('自动（CPU/GPU/ANE）')) + '</option><option value="ane">' + videoEditorEscape(t('Neural Engine优先')) + '</option><option value="gpu">' + videoEditorEscape(t('GPU优先')) + '</option><option value="cpu">' + videoEditorEscape(t('仅CPU（对照）')) + '</option></select></label>' +
        '<label><span>Tile</span><select data-ai-screenshot-tile><option value="0">' + videoEditorEscape(t('自动')) + '</option><option value="128">128</option><option value="256">256</option><option value="512">512</option></select></label>' +
        '<label><span>' + videoEditorEscape(t('Tile重叠')) + '</span><select data-ai-screenshot-overlap><option value="low">' + videoEditorEscape(t('较少（更快）')) + '</option><option value="balanced">' + videoEditorEscape(t('均衡')) + '</option><option value="quality">' + videoEditorEscape(t('较多（减少接缝）')) + '</option></select></label>' +
        '<p>' + videoEditorEscape(t('AI会推测并生成纹理细节，结果不一定与原始真实内容完全一致。')) + '</p>' +
      '</div><footer><button type="button" data-ai-screenshot-cancel>' + videoEditorEscape(t('取消')) + '</button><button type="button" class="primary" data-ai-screenshot-save>' + videoEditorEscape(t('保存设置')) + '</button></footer>' +
    '</section>';
    dialog.appendChild(settingsDialog);
    const model = settingsDialog.querySelector('[data-ai-screenshot-model]');
    const scale = settingsDialog.querySelector('[data-ai-screenshot-scale]');
    const denoise = settingsDialog.querySelector('[data-ai-screenshot-denoise]');
    const sharpen = settingsDialog.querySelector('[data-ai-screenshot-sharpen]');
    const quality = settingsDialog.querySelector('[data-ai-screenshot-quality]');
    const compute = settingsDialog.querySelector('[data-ai-screenshot-compute]');
    const tile = settingsDialog.querySelector('[data-ai-screenshot-tile]');
    const overlap = settingsDialog.querySelector('[data-ai-screenshot-overlap]');
    model.value = screenshotSettings.model;
    scale.value = String(screenshotSettings.scale);
    denoise.value = String(screenshotSettings.denoise);
    sharpen.value = String(screenshotSettings.sharpen);
    quality.value = String(screenshotSettings.quality);
    compute.value = screenshotSettings.computeUnits;
    tile.value = String(screenshotSettings.tile);
    overlap.value = screenshotSettings.overlap;
    const syncRangeLabels = function () {
      settingsDialog.querySelector('[data-ai-screenshot-sharpen-value]').textContent = sharpen.value + '%';
      settingsDialog.querySelector('[data-ai-screenshot-quality-value]').textContent = quality.value + '%';
    };
    const syncModelScale = function () {
      const coreml = model.value.indexOf('coreml-') === 0;
      if (model.value === 'coreml-x2plus') scale.value = '2';
      else if (coreml || model.value === 'realesrgan-x4plus') scale.value = '4';
      scale.disabled = coreml || model.value === 'realesrgan-x4plus';
      compute.disabled = !coreml;
    };
    sharpen.addEventListener('input', syncRangeLabels);
    quality.addEventListener('input', syncRangeLabels);
    model.addEventListener('change', syncModelScale);
    syncRangeLabels();
    syncModelScale();
    const close = function () { settingsDialog.remove(); };
    settingsDialog.querySelector('[data-ai-screenshot-close]').addEventListener('click', close);
    settingsDialog.querySelector('[data-ai-screenshot-cancel]').addEventListener('click', close);
    settingsDialog.addEventListener('click', function (event) { if (event.target === settingsDialog) close(); });
    settingsDialog.querySelector('[data-ai-screenshot-save]').addEventListener('click', function () {
      screenshotSettings.model = model.value;
      screenshotSettings.scale = Number(scale.value) || 2;
      screenshotSettings.denoise = Number(denoise.value) || 0;
      screenshotSettings.sharpen = Number(sharpen.value) || 0;
      screenshotSettings.quality = Number(quality.value) || 95;
      screenshotSettings.computeUnits = compute.value;
      screenshotSettings.tile = Number(tile.value) || 0;
      screenshotSettings.overlap = overlap.value;
      close();
    });
  }

  screenshotMode.addEventListener('change', function () {
    const ai = screenshotMode.value === 'ai';
    screenshotAiSettingsBtn.hidden = !ai;
    if (ai) openScreenshotAiSettings();
  });
  screenshotAiSettingsBtn.addEventListener('click', openScreenshotAiSettings);
  subtitleBrowseBtn.addEventListener('click', function () { subtitleUploadInput.click(); });
  subtitleUploadInput.addEventListener('change', function () {
    const file = subtitleUploadInput.files && subtitleUploadInput.files[0];
    if (!file) return;
    if (!/\.(srt|vtt|ass|ssa)$/i.test(file.name)) {
      selectedSubtitleUpload = null;
      selectedSubtitleUploadPath = '';
      subtitleUploadInput.value = '';
      updateProgress(0, t('请选择 SRT、VTT、ASS 或 SSA 字幕文件'), 'failed');
      return;
    }
    selectedSubtitleUpload = file;
    selectedSubtitleUploadPath = '';
    subtitleFile.value = file.name;
  });
  subtitleFile.addEventListener('input', function () {
    if (selectedSubtitleUpload && subtitleFile.value !== selectedSubtitleUpload.name) {
      selectedSubtitleUpload = null;
      selectedSubtitleUploadPath = '';
      subtitleUploadInput.value = '';
    }
  });
  dialog.querySelector('[data-editor-set-start]').addEventListener('click', function () { startNumber.value = video.currentTime.toFixed(1); syncSelection(startNumber); });
  dialog.querySelector('[data-editor-set-end]').addEventListener('click', function () { endNumber.value = video.currentTime.toFixed(1); syncSelection(endNumber); });
  dialog.querySelector('[data-editor-play-selection]').addEventListener('click', function () {
    video.currentTime = selection().start; stopAtEnd = true; video.play().catch(function () {});
  });
  dialog.querySelectorAll('[data-video-editor-close]').forEach(function (button) { button.addEventListener('click', closeEditor); });
  exportAudioBtn.addEventListener('click', function () { startTrackExport('audio'); });
  exportSubtitleBtn.addEventListener('click', function () {
    if (subtitleMode.value === 'replace') startVideoExport();
    else startTrackExport('subtitle');
  });
  exportKeyframesBtn.addEventListener('click', function () { startTrackExport('keyframes'); });
  screenshotBtn.addEventListener('click', function () { startTrackExport('screenshot'); });

  function updateProgress(value, message, state) {
    footer.hidden = false;
    progress.hidden = false;
    progressBar.style.width = Math.max(0, Math.min(100, Number(value) || 0)) + '%';
    progressText.textContent = message || '';
    progressText.title = message || '';
    progress.classList.toggle('failed', state === 'failed');
    progress.classList.toggle('done', state === 'done');
  }

  function exportProgressMessage(message, value) {
    const percent = Math.max(0, Math.min(100, Math.round(Number(value) || 0)));
    const elapsed = exportStartedAt ? formatVideoEditorElapsed(Date.now() - exportStartedAt) : '00:00:00';
    return String(message || '') + ' ' + percent + '% · ' + t('已耗时') + elapsed;
  }

  function setExportBusy(busy) {
    exporting = busy;
    exportBtn.disabled = busy;
    exportAudioBtn.disabled = busy;
    exportSubtitleBtn.disabled = busy;
    exportKeyframesBtn.disabled = busy;
    screenshotBtn.disabled = busy;
    screenshotMode.disabled = busy;
    screenshotAiSettingsBtn.disabled = busy;
    subtitleBrowseBtn.disabled = busy;
    subtitleUploadInput.disabled = busy;
    cancelExportBtn.hidden = !busy;
    if (!busy) cancelExportBtn.disabled = false;
  }

  async function startTrackExport(kind) {
    if (exporting || !duration) return;
    const selected = selection();
    const isAudio = kind === 'audio';
    const isKeyframes = kind === 'keyframes';
    const isScreenshot = kind === 'screenshot';
    const isFrameExport = isKeyframes || isScreenshot;
    const isAiScreenshot = isScreenshot && screenshotMode.value === 'ai';
    const startedAt = Date.now();
    let lastProgress = 0;
    const progressMessage = function (message, value) {
      return String(message || '') + ' ' + value + '% · ' + t('已用时') + ' '
        + formatVideoEditorElapsed(Date.now() - startedAt);
    };
    const startApi = isAudio
      ? (local ? api.localDiskAudioExtract : api.extractAudio)
      : (isFrameExport
        ? (local ? api.localDiskVideoKeyframeExport : api.videoKeyframeExport)
        : (local ? api.localDiskVideoSubtitleExport : api.videoSubtitleExport));
    const progressApi = isAudio
      ? (local ? api.localDiskAudioExtractProgress : api.extractAudioProgress)
      : (isFrameExport
        ? (local ? api.localDiskVideoKeyframeExportProgress : api.videoKeyframeExportProgress)
        : (local ? api.localDiskVideoSubtitleExportProgress : api.videoSubtitleExportProgress));
    activeCancelApi = isAudio
      ? (local ? api.localDiskAudioExtractCancel : api.extractAudioCancel)
      : (isFrameExport
        ? (local ? api.localDiskVideoKeyframeExportCancel : api.videoKeyframeExportCancel)
        : (local ? api.localDiskVideoSubtitleExportCancel : api.videoSubtitleExportCancel));
    const startingMessage = isAudio ? t('正在启动音频导出')
      : (isScreenshot ? t(isAiScreenshot ? '正在启动AI增强截屏' : '正在启动截屏')
        : (isKeyframes ? t('正在启动关键帧截屏') : t('正在启动字幕导出')));
    const runningMessage = isAudio ? t('正在导出音频')
      : (isScreenshot ? t(isAiScreenshot ? 'AI正在增强当前画面' : '正在截取当前画面')
        : (isKeyframes ? t('正在导出关键帧') : t('正在导出字幕')));
    const completionMessage = isAudio ? t('音频导出完成：')
      : (isScreenshot ? t(isAiScreenshot ? 'AI截屏完成：' : '截屏完成：')
        : (isKeyframes ? t('关键帧截屏完成：') : t('字幕导出完成：')));
    const failureMessage = isAudio ? t('导出音频失败：')
      : (isScreenshot ? t(isAiScreenshot ? 'AI截屏失败：' : '截屏失败：')
        : (isKeyframes ? t('导出关键帧失败：') : t('导出字幕失败：')));
    setExportBusy(true);
    updateProgress(0, progressMessage(startingMessage, 0));
    try {
      let url = videoEditorTaskUrl(startApi, path, local);
      const currentTime = Number(video.currentTime);
      const capturePosition = Math.max(0, Math.min(Math.max(0, duration - 0.001),
        Number.isFinite(currentTime) ? currentTime : selected.start));
      url += '&start_ms=' + encodeURIComponent(String(Math.round(
        (isScreenshot ? capturePosition : selected.start) * 1000
      )));
      url += '&end_ms=' + encodeURIComponent(String(Math.round(selected.end * 1000)));
      if (isScreenshot) {
        url += '&single=1&enhance_mode=' + encodeURIComponent(screenshotMode.value);
        url += '&ai_model=' + encodeURIComponent(screenshotSettings.model);
        url += '&ai_scale=' + encodeURIComponent(String(screenshotSettings.scale));
        url += '&ai_denoise=' + encodeURIComponent(String(screenshotSettings.denoise));
        url += '&sharpen=' + encodeURIComponent(String(
          screenshotMode.value === 'sharpen' ? 35 : screenshotSettings.sharpen
        ));
        url += '&image_quality=' + encodeURIComponent(String(screenshotSettings.quality));
        url += '&ai_compute_units=' + encodeURIComponent(screenshotSettings.computeUnits);
        url += '&ai_tile=' + encodeURIComponent(String(screenshotSettings.tile));
        url += '&ai_overlap=' + encodeURIComponent(screenshotSettings.overlap);
      }
      const started = await fetchJson(url, { method: 'POST' });
      taskId = String(started.task_id || '');
      if (!taskId) throw new Error(started.message || t('无法启动导出'));
      while (true) {
        const data = await fetchJson(progressApi + '?task_id=' + encodeURIComponent(taskId));
        const value = Math.max(0, Math.min(100, Math.round(Number(data.progress) || 0)));
        lastProgress = value;
        updateProgress(value, progressMessage(
          data.message || runningMessage,
          value
        ));
        if (data.done) {
          if (!data.success) throw new Error(data.cancel_requested ? t('已取消') : (data.error || data.message || t('导出失败')));
          lastProgress = 100;
          updateProgress(100, progressMessage(
            completionMessage + String(data.name || ''),
            100
          ), 'done');
          if (local) await loadLocalDisk(activeLocalDiskPath || localDiskParentPath(path) || '');
          else await loadFiles();
          showStatus(completionMessage + String(data.name || ''), 'ok');
          break;
        }
        await new Promise(function (resolve) { window.setTimeout(resolve, 800); });
      }
    } catch (err) {
      updateProgress(lastProgress, progressMessage(
        failureMessage + err.message,
        lastProgress
      ), 'failed');
    } finally {
      taskId = '';
      activeCancelApi = '';
      setExportBusy(false);
    }
  }

  async function pollTask() {
    const base = local ? api.localDiskVideoEditProgress : api.videoEditProgress;
    const data = await fetchJson(base + '?task_id=' + encodeURIComponent(taskId));
    const value = Math.round(Number(data.progress) || 0);
    // The server task message is shared by all clients and is not localized.
    updateProgress(value, exportProgressMessage(t('正在导出'), value));
    if (!data.done) {
      await new Promise(function (resolve) { window.setTimeout(resolve, 800); });
      return pollTask();
    }
    setExportBusy(false);
    taskId = '';
    activeCancelApi = '';
    if (!data.success) {
      updateProgress(value, exportProgressMessage(
        data.cancel_requested ? t('已取消') : (data.error || data.message || t('导出失败')),
        value
      ), 'failed');
      return;
    }
    updateProgress(100, exportProgressMessage(t('导出完成：') + String(data.name || ''), 100), 'done');
    exportBtn.textContent = t('导出视频');
    if (local) await loadLocalDisk(activeLocalDiskPath || localDiskParentPath(path) || '');
    else await loadFiles();
    showStatus(t('视频剪辑完成：') + String(data.name || ''), 'ok');
  }

  async function uploadSelectedSubtitle() {
    if (!selectedSubtitleUpload) return '';
    if (selectedSubtitleUploadPath) return selectedSubtitleUploadPath;
    updateProgress(0, t('正在上传字幕文件'));
    const form = new FormData();
    form.set('folder', '');
    const dot = selectedSubtitleUpload.name.lastIndexOf('.');
    const stem = (dot > 0 ? selectedSubtitleUpload.name.slice(0, dot) : selectedSubtitleUpload.name)
      .replace(/[^\w\u4e00-\u9fff.-]+/g, '_').slice(0, 120) || 'subtitle';
    const extension = dot >= 0 ? selectedSubtitleUpload.name.slice(dot).toLowerCase() : '.vtt';
    const stagedName = stem + '_video_editor_' + Date.now() + extension;
    form.append('file', selectedSubtitleUpload, stagedName);
    const uploaded = await fetchJson(api.upload, { method: 'POST', body: form });
    const item = (Array.isArray(uploaded.files) ? uploaded.files : []).find(function (entry) {
      return entry && entry.saved && entry.path;
    });
    if (!item) throw new Error(t('字幕文件上传失败'));
    selectedSubtitleUploadPath = String(item.path);
    return selectedSubtitleUploadPath;
  }

  async function startVideoExport() {
    if (exporting || !duration) return;
    const selected = selection();
    exportStartedAt = Date.now();
    setExportBusy(true);
    updateProgress(0, exportProgressMessage(
      subtitleMode.value === 'replace' ? t('正在开始添加字幕') : t('正在启动导出'), 0
    ));
    try {
      let subtitlePath = subtitleFile.value.trim();
      let subtitleFileSource = '';
      if (subtitleMode.value === 'replace' && selectedSubtitleUpload) {
        subtitlePath = await uploadSelectedSubtitle();
        subtitleFileSource = 'upload';
      }
      let url = videoEditorTaskUrl(local ? api.localDiskVideoEdit : api.videoEdit, path, local);
      const params = {
        start_ms: Math.round(selected.start * 1000), end_ms: Math.round(selected.end * 1000),
        speed: speed.value, volume: volume.value, muted: muted.checked ? 1 : 0,
        rotate: rotate.value, flip_h: flipH.checked ? 1 : 0, flip_v: flipV.checked ? 1 : 0,
        crop: crop.value, output_height: outputHeight.value,
        audio_mode: audioMode.value, audio_file: audioFile.value.trim(),
        audio_start_ms: Math.round((Number(audioStart.value) || 0) * 1000),
        subtitle_mode: subtitleMode.value, subtitle_file: subtitlePath,
        subtitle_file_source: subtitleFileSource,
        subtitle_import_mode: subtitleMode.value === 'replace' ? subtitleImportMode.value : 'reencode',
        subtitle_start_ms: Math.round((Number(subtitleStart.value) || 0) * 1000),
        enhance_mode: screenshotMode.value,
        ai_model: screenshotSettings.model, ai_scale: screenshotSettings.scale,
        ai_denoise: screenshotSettings.denoise,
        sharpen: screenshotMode.value === 'sharpen' ? 35 : screenshotSettings.sharpen,
        ai_compute_units: screenshotSettings.computeUnits,
        ai_tile: screenshotSettings.tile, ai_overlap: screenshotSettings.overlap
      };
      if (audioMode.value === 'replace' && !params.audio_file) throw new Error(t('请选择音频文件'));
      if (subtitleMode.value === 'replace' && !params.subtitle_file) throw new Error(t('请选择字幕文件'));
      if (subtitleMode.value === 'replace' && subtitleImportMode.value === 'fast'
        && (speed.value !== '1' || volume.value !== '100' || muted.checked
          || rotate.value !== '0' || flipH.checked || flipV.checked
          || crop.value !== 'original' || outputHeight.value !== '0'
          || audioMode.value !== 'keep' || screenshotMode.value !== 'original')) {
        throw new Error(t('快速封装不能同时应用画面、变速或音频编辑，请选择兼容转码'));
      }
      Object.keys(params).forEach(function (key) { url += '&' + key + '=' + encodeURIComponent(params[key]); });
      const data = await fetchJson(url, { method: 'POST' });
      taskId = String(data.task_id || '');
      activeCancelApi = local ? api.localDiskVideoEditCancel : api.videoEditCancel;
      if (!taskId) throw new Error(data.message || t('无法启动视频剪辑'));
      await pollTask();
    } catch (err) {
      setExportBusy(false);
      updateProgress(0, exportProgressMessage(t('导出失败：') + err.message, 0), 'failed');
    }
  }

  exportBtn.addEventListener('click', startVideoExport);

  cancelExportBtn.addEventListener('click', function () {
    if (!taskId || !activeCancelApi) return;
    cancelExportBtn.disabled = true;
    fetchJson(activeCancelApi + '?task_id=' + encodeURIComponent(taskId), { method: 'POST' })
      .then(function () { updateProgress(null, t('取消中')); })
      .catch(function (err) { updateProgress(null, t('取消失败：') + err.message, 'failed'); cancelExportBtn.disabled = false; });
  });

  applyPreview();
  syncSubtitleMode();

  const editorWindow = dialog.querySelector('.video-editor-window');
  const editorHeader = dialog.querySelector('.video-editor-header');
  const minimizeBtn = dialog.querySelector('[data-editor-window-minimize]');
  const maximizeBtn = dialog.querySelector('[data-editor-window-maximize]');
  let minimizedFromMaximized = false;
  const initialRect = editorWindow.getBoundingClientRect();
  editorWindow.style.left = Math.round(initialRect.left) + 'px';
  editorWindow.style.top = Math.round(initialRect.top) + 'px';
  editorWindow.style.position = 'fixed';
  function setMinimized(minimizing) {
    if (minimizing) {
      minimizedFromMaximized = editorWindow.classList.contains('is-maximized');
      editorWindow.classList.remove('is-maximized');
      maximizeBtn.textContent = '□';
      maximizeBtn.title = t('最大化');
    }
    editorWindow.classList.toggle('is-minimized', minimizing);
    dialog.classList.toggle('is-minimized', minimizing);
    editorWindow.setAttribute('aria-modal', minimizing ? 'false' : 'true');
    minimizeBtn.title = t(minimizing ? '复原' : '最小化');
    minimizeBtn.setAttribute('aria-label', minimizeBtn.title);
    if (!minimizing && minimizedFromMaximized) {
      editorWindow.classList.add('is-maximized');
      maximizeBtn.textContent = '❐';
      maximizeBtn.title = t('复原');
    }
  }
  minimizeBtn.addEventListener('click', function () {
    setMinimized(!editorWindow.classList.contains('is-minimized'));
  });
  maximizeBtn.addEventListener('click', function () {
    if (editorWindow.classList.contains('is-minimized')) {
      const restoreMaximized = minimizedFromMaximized;
      setMinimized(false);
      if (restoreMaximized) return;
    }
    const maximizing = !editorWindow.classList.contains('is-maximized');
    editorWindow.classList.toggle('is-maximized', maximizing);
    maximizeBtn.textContent = maximizing ? '❐' : '□';
    maximizeBtn.title = maximizing ? t('复原') : t('最大化');
  });
  editorHeader.addEventListener('dblclick', function (event) {
    if (event.target.closest('button')) return;
    if (editorWindow.classList.contains('is-minimized')) setMinimized(false);
    else maximizeBtn.click();
  });
  editorHeader.addEventListener('pointerdown', function (event) {
    if (event.button !== 0 || event.target.closest('button') || editorWindow.classList.contains('is-maximized')) return;
    const rect = editorWindow.getBoundingClientRect();
    const startX = event.clientX;
    const startY = event.clientY;
    const originLeft = rect.left;
    const originTop = rect.top;
    editorHeader.setPointerCapture(event.pointerId);
    const move = function (moveEvent) {
      const maxLeft = Math.max(0, window.innerWidth - 180);
      const maxTop = Math.max(0, window.innerHeight - 60);
      editorWindow.style.left = Math.round(Math.max(0, Math.min(maxLeft, originLeft + moveEvent.clientX - startX))) + 'px';
      editorWindow.style.top = Math.round(Math.max(0, Math.min(maxTop, originTop + moveEvent.clientY - startY))) + 'px';
    };
    const end = function () {
      editorHeader.removeEventListener('pointermove', move);
      editorHeader.removeEventListener('pointerup', end);
      editorHeader.removeEventListener('pointercancel', end);
    };
    editorHeader.addEventListener('pointermove', move);
    editorHeader.addEventListener('pointerup', end);
    editorHeader.addEventListener('pointercancel', end);
  });
}

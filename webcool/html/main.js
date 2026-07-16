(function () {
  function clamp(value, min, max) {
    return Math.max(min, Math.min(max, value));
  }

  function getAdaptiveUiScale() {
    const viewportWidth = window.innerWidth || document.documentElement.clientWidth || 0;
    const viewportHeight = window.innerHeight || document.documentElement.clientHeight || 0;
    const screenWidth = window.screen && window.screen.width ? window.screen.width : viewportWidth;
    const screenHeight = window.screen && window.screen.height ? window.screen.height : viewportHeight;
    const isWindows = /Win/i.test(navigator.platform || navigator.userAgent || '');

    if (viewportWidth <= 900) {
      return 1;
    }

    const isFullHdLaptop = screenWidth >= 1800 && screenHeight <= 1120;
    const isShortWideViewport = viewportWidth >= 1280 && viewportHeight <= 920;
    if (!isFullHdLaptop && !isShortWideViewport) {
      return 1;
    }

    const heightScale = viewportHeight > 0
      ? 0.82 + clamp((viewportHeight - 720) / 320, 0, 1) * 0.1
      : 0.86;
    const screenScale = isWindows ? 0.84 : (isFullHdLaptop ? 0.88 : 0.92);
    return Math.min(0.94, Math.max(0.82, Math.min(heightScale, screenScale)));
  }

  function applyAdaptiveUiScale() {
    const root = document.documentElement;
    const bodyStyle = window.getComputedStyle(document.body);
    const padX = parseFloat(bodyStyle.paddingLeft || '0') + parseFloat(bodyStyle.paddingRight || '0');
    const padY = parseFloat(bodyStyle.paddingTop || '0') + parseFloat(bodyStyle.paddingBottom || '0');
    const scale = getAdaptiveUiScale();
    const availableWidth = Math.max(0, (window.innerWidth || 0) - padX);
    const availableHeight = Math.max(0, (window.innerHeight || 0) - padY);

    root.style.setProperty('--ui-scale', scale.toFixed(3));
    root.style.setProperty('--scaled-shell-width', (availableWidth / scale).toFixed(2) + 'px');
    root.style.setProperty('--scaled-shell-height', (availableHeight / scale).toFixed(2) + 'px');
  }

  let adaptiveScaleFrame = 0;
  function scheduleAdaptiveUiScale() {
    if (adaptiveScaleFrame) {
      window.cancelAnimationFrame(adaptiveScaleFrame);
    }
    adaptiveScaleFrame = window.requestAnimationFrame(function () {
      adaptiveScaleFrame = 0;
      applyAdaptiveUiScale();
    });
  }

  applyAdaptiveUiScale();
  window.addEventListener('resize', scheduleAdaptiveUiScale);
  window.addEventListener('orientationchange', scheduleAdaptiveUiScale);

  const assetVersion = 'image-preview-flex-height-20260716-41';
  const modules = [
    '/webcool/html/js/core-state.js',
    '/webcool/html/js/locks-context.js',
    '/webcool/html/js/tags-audio-dialogs.js',
    '/webcool/html/js/transcode-upload.js',
    '/webcool/html/js/remote-folders-import.js',
    '/webcool/html/js/markdown-preview.js',
    '/webcool/html/js/html-preview.js',
    '/webcool/html/js/docx-preview-support.js',
    '/webcool/html/js/spreadsheet-preview-support.js',
    '/webcool/html/js/pptx-preview-support.js',
    '/webcool/html/js/xmind-preview-support.js',
    '/webcool/html/js/remote-files.js',
    '/webcool/html/js/preview-local-disk.js',
    '/webcool/html/js/admin-storage.js',
    '/webcool/html/js/video-editor.js',
    '/webcool/html/js/events-bootstrap.js'
  ];

  let heicRuntimePromise = null;
  window.loadWebCoolHeicPreviewRuntime = function () {
    if (window.ViewHEIC) {
      return Promise.resolve(window.ViewHEIC);
    }
    if (heicRuntimePromise) {
      return heicRuntimePromise;
    }
    heicRuntimePromise = new Promise(function (resolve, reject) {
      const script = document.createElement('script');
      script.src = '/webcool/html/js/vendor/view-heic.js?v=' + encodeURIComponent(assetVersion);
      script.async = true;
      script.onload = function () {
        resolve(window.ViewHEIC || null);
      };
      script.onerror = function () {
        heicRuntimePromise = null;
        reject(new Error('view-heic.js load failed'));
      };
      document.head.appendChild(script);
    });
    return heicRuntimePromise;
  };

  function showLoadError(err) {
    console.error('Failed to load WebCool modules:', err);
    const status = document.getElementById('status');
    if (status) {
      status.className = 'status show err';
      status.textContent = '前端模块加载失败：' + (err && err.message ? err.message : err);
    }
  }

  function loadModuleScript(url) {
    return new Promise(function (resolve, reject) {
      const script = document.createElement('script');
      script.src = url + '?v=' + encodeURIComponent(assetVersion);
      script.async = false;
      script.onload = resolve;
      script.onerror = function () {
        reject(new Error(url + ' load failed'));
      };
      document.head.appendChild(script);
    });
  }

  modules.reduce(function (chain, url) {
    return chain.then(function () {
      return loadModuleScript(url);
    });
  }, Promise.resolve())
    .catch(showLoadError);
})();

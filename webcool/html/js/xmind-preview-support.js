(function () {
  const XMIND_VIEWER_SOURCES = [
    '/webcool/html/js/vendor/xmind-local-embed-viewer.js'
  ];

  let viewerLoadPromise = null;

  function isXMindName(name) {
    return /\.xmind$/i.test(String(name || ''));
  }

  function xmindViewerApi() {
    if (typeof window.XMindEmbedViewer === 'function') {
      return window.XMindEmbedViewer;
    }
    if (window.XMindEmbedViewer && typeof window.XMindEmbedViewer.XMindEmbedViewer === 'function') {
      return window.XMindEmbedViewer.XMindEmbedViewer;
    }
    return null;
  }

  function loadScript(src, key) {
    return new Promise(function (resolve, reject) {
      const attr = 'data-' + key + '-src';
      const existing = document.querySelector('script[' + attr + '="' + src + '"]');
      if (existing) {
        if (existing.getAttribute('data-loaded') === '1') {
          resolve();
          return;
        }
        if (existing.getAttribute('data-failed') === '1') {
          reject(new Error(key + ' load failed: ' + src));
          return;
        }
        existing.addEventListener('load', resolve, { once: true });
        existing.addEventListener('error', reject, { once: true });
        return;
      }
      const script = document.createElement('script');
      script.src = src;
      script.async = true;
      script.setAttribute(attr, src);
      script.onload = function () {
        script.setAttribute('data-loaded', '1');
        resolve();
      };
      script.onerror = function () {
        script.setAttribute('data-failed', '1');
        reject(new Error(key + ' load failed: ' + src));
      };
      document.head.appendChild(script);
    });
  }

  function loadViewer() {
    const loaded = xmindViewerApi();
    if (loaded) {
      return Promise.resolve(loaded);
    }
    if (viewerLoadPromise) {
      return viewerLoadPromise;
    }
    viewerLoadPromise = XMIND_VIEWER_SOURCES.reduce(function (chain, src) {
      return chain.catch(function () {
        const current = xmindViewerApi();
        if (current) {
          return current;
        }
        return loadScript(src, 'xmind-viewer').then(function () {
          const api = xmindViewerApi();
          if (!api) {
            throw new Error('XMind viewer API not found: ' + src);
          }
          return api;
        });
      });
    }, Promise.reject(new Error('start XMind viewer loading'))).catch(function (err) {
      viewerLoadPromise = null;
      throw new Error('XMind viewer local library load failed, please check /webcool/html/js/vendor/xmind-local-embed-viewer.js: ' + err.message);
    });
    return viewerLoadPromise;
  }

  async function renderPreview(container, url, options) {
    if (!container) {
      throw new Error('xmind preview container missing');
    }
    const opts = options || {};
    container.innerHTML = '<div class="xmind-preview-loading">' + (opts.loadingText || '正在加载 XMind 预览...') + '</div>';

    const Viewer = await loadViewer();
    const headers = opts.token ? { Authorization: 'Bearer ' + opts.token } : undefined;
    const res = await fetch(url, {
      credentials: 'same-origin',
      headers: headers
    });
    if (!res.ok) {
      throw new Error('xmind download failed: HTTP ' + res.status);
    }

    const buffer = await res.arrayBuffer();
    container.innerHTML = '<div class="xmind-preview-render" data-xmind-preview-render></div>';
    const renderTarget = container.querySelector('[data-xmind-preview-render]');
    const viewer = new Viewer({
      el: renderTarget,
      region: opts.region || 'cn',
      isPitchModeDisabled: true,
      styles: {
        width: '100%',
        height: '100%'
      }
    });
    container.__webcoolXMindViewer = viewer;
    await new Promise(function (resolve) {
      let done = false;
      const finish = function () {
        if (done) {
          return;
        }
        done = true;
        resolve();
      };
      if (typeof viewer.addEventListener === 'function') {
        viewer.addEventListener('map-ready', finish);
      }
      viewer.load(buffer);
      setTimeout(finish, 8000);
    });
    if (typeof viewer.setFitMap === 'function') {
      viewer.setFitMap();
    }
  }

  window.WebCoolXMindPreview = {
    isXMindName: isXMindName,
    renderPreview: renderPreview
  };
}());

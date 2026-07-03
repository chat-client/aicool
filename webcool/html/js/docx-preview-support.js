(function () {
  const JSZIP_SOURCES = [
    '/webcool/html/js/vendor/jszip.min.js',
    'https://cdn.jsdelivr.net/npm/jszip@3.10.1/dist/jszip.min.js'
  ];

  const DOCX_PREVIEW_SOURCES = [
    '/webcool/html/js/vendor/docx-preview.min.js',
    'https://cdn.jsdelivr.net/npm/docx-preview@0.3.6/dist/docx-preview.min.js'
  ];

  let jsZipLoadPromise = null;
  let docxPreviewLoadPromise = null;

  function isDocxName(name) {
    return /\.docx$/i.test(String(name || ''));
  }

  function docxPreviewApi() {
    if (window.docx && typeof window.docx.renderAsync === 'function') {
      return window.docx;
    }
    if (window.docxPreview && typeof window.docxPreview.renderAsync === 'function') {
      return window.docxPreview;
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

  function loadFromSources(sources, key, isLoaded) {
    return sources.reduce(function (chain, src) {
      return chain.catch(function () {
        if (isLoaded()) {
          return Promise.resolve();
        }
        return loadScript(src, key).then(function () {
          if (!isLoaded()) {
            throw new Error(key + ' API not found: ' + src);
          }
        });
      });
    }, Promise.reject(new Error('start ' + key + ' loading')));
  }

  function loadJSZip() {
    if (window.JSZip) {
      return Promise.resolve();
    }
    if (jsZipLoadPromise) {
      return jsZipLoadPromise;
    }
    jsZipLoadPromise = loadFromSources(JSZIP_SOURCES, 'jszip', function () {
      return !!window.JSZip;
    });
    return jsZipLoadPromise;
  }

  function loadDocxPreview() {
    const loaded = docxPreviewApi();
    if (loaded && window.JSZip) {
      return Promise.resolve(loaded);
    }
    if (docxPreviewLoadPromise) {
      return docxPreviewLoadPromise;
    }
    docxPreviewLoadPromise = loadJSZip().then(function () {
      if (docxPreviewApi() && window.JSZip) {
        return docxPreviewApi();
      }
      return DOCX_PREVIEW_SOURCES.reduce(function (chain, src) {
        return chain.catch(function () {
          return loadScript(src, 'docx-preview').then(function () {
            const api = docxPreviewApi();
            if (!api) {
              throw new Error('docx-preview API not found: ' + src);
            }
            return api;
          });
        });
      }, Promise.reject(new Error('start docx-preview loading')));
    }).then(function (api) {
      if (!window.JSZip) {
        docxPreviewLoadPromise = null;
        throw new Error('JSZip API not found');
      }
      return api;
    }).catch(function (err) {
      docxPreviewLoadPromise = null;
      const api = docxPreviewApi();
      if (api && window.JSZip) {
        return api;
      }
      throw err;
    });
    return docxPreviewLoadPromise;
  }

  function reloadDocxPreviewAfterJSZip() {
    docxPreviewLoadPromise = DOCX_PREVIEW_SOURCES.reduce(function (chain, src) {
      return chain.catch(function () {
        return loadScript(src + '?reload=' + Date.now(), 'docx-preview-reload').then(function () {
          const api = docxPreviewApi();
          if (!api) {
            throw new Error('docx-preview API not found: ' + src);
          }
          return api;
        });
      });
    }, Promise.reject(new Error('reload docx-preview')));
    return docxPreviewLoadPromise;
  }

  async function renderPreview(container, url, options) {
    if (!container) {
      throw new Error('docx preview container missing');
    }
    const opts = options || {};
    container.innerHTML = '<div class="office-preview-loading">' + (opts.loadingText || '正在加载 DOCX 预览...') + '</div>';
    let api = await loadDocxPreview();
    const headers = opts.token ? { Authorization: 'Bearer ' + opts.token } : undefined;
    const res = await fetch(url, {
      credentials: 'same-origin',
      headers: headers
    });
    if (!res.ok) {
      throw new Error('docx download failed: HTTP ' + res.status);
    }
    const blob = await res.blob();
    container.innerHTML = '';
    try {
      await api.renderAsync(blob, container, null, {
        className: 'docx-preview-document',
        inWrapper: true,
        ignoreWidth: false,
        ignoreHeight: false,
        breakPages: true,
        renderHeaders: true,
        renderFooters: true,
        renderFootnotes: true,
        useBase64URL: true
      });
    } catch (err) {
      const message = err && err.message ? err.message : String(err || '');
      if (window.JSZip && message.indexOf('loadAsync') >= 0) {
        api = await reloadDocxPreviewAfterJSZip();
        container.innerHTML = '';
        await api.renderAsync(blob, container, null, {
          className: 'docx-preview-document',
          inWrapper: true,
          ignoreWidth: false,
          ignoreHeight: false,
          breakPages: true,
          renderHeaders: true,
          renderFooters: true,
          renderFootnotes: true,
          useBase64URL: true
        });
        return;
      }
      throw err;
    }
  }

  window.WebCoolDocxPreview = {
    isDocxName: isDocxName,
    renderPreview: renderPreview
  };
}());

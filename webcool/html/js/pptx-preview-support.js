(function () {
  const PPTX_RENDERER_URL = '/webcool/html/js/vendor/pptx-renderer.bundle.js';

  let modulePromise = null;

  function isPptxName(name) {
    return /\.pptx$/i.test(String(name || ''));
  }

  function loadRenderer() {
    if (!modulePromise) {
      modulePromise = import(PPTX_RENDERER_URL).catch(function (err) {
        modulePromise = null;
        throw err;
      });
    }
    return modulePromise;
  }

  async function renderPreview(container, url, options) {
    if (!container) {
      throw new Error('pptx preview container missing');
    }
    const opts = options || {};
    container.innerHTML = '<div class="office-preview-loading">' + (opts.loadingText || '正在加载 PPTX 预览...') + '</div>';

    const api = await loadRenderer();
    if (!api || !api.PptxViewer) {
      throw new Error('PPTX renderer API not found');
    }

    const headers = opts.token ? { Authorization: 'Bearer ' + opts.token } : undefined;
    const res = await fetch(url, {
      credentials: 'same-origin',
      headers: headers
    });
    if (!res.ok) {
      throw new Error('pptx download failed: HTTP ' + res.status);
    }

    const buffer = await res.arrayBuffer();
    container.innerHTML = '<div class="pptx-preview-render" data-pptx-preview-render></div>';
    const renderTarget = container.querySelector('[data-pptx-preview-render]');
    const viewer = await api.PptxViewer.open(buffer, renderTarget, {
      pdfjs: false,
      lazySlides: true,
      lazyMedia: true,
      zipLimits: api.RECOMMENDED_ZIP_LIMITS,
      listOptions: {
        windowed: true,
        initialSlides: 4,
        batchSize: 4
      }
    });
    container.__webcoolPptxViewer = viewer;
  }

  window.WebCoolPptxPreview = {
    isPptxName: isPptxName,
    renderPreview: renderPreview
  };
}());

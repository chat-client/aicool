(function () {
  function isHtmlName(name) {
    return /\.(html?|xhtml|htm)$/i.test(String(name || ''));
  }

  function escapeHtmlAttr(value) {
    return String(value == null ? '' : value)
      .replace(/&/g, '&amp;')
      .replace(/"/g, '&quot;')
      .replace(/</g, '&lt;');
  }

  function injectBaseHref(html, baseHref) {
    const text = String(html == null ? '' : html);
    if (!baseHref) {
      return text;
    }
    const baseTag = '<base href="' + escapeHtmlAttr(baseHref) + '">';
    if (/<head[\s>]/i.test(text)) {
      return text.replace(/<head(\s[^>]*)?>/i, function (match) {
        return match + baseTag;
      });
    }
    if (/<html[\s>]/i.test(text)) {
      return text.replace(/<html(\s[^>]*)?>/i, function (match) {
        return match + '<head>' + baseTag + '</head>';
      });
    }
    return '<!DOCTYPE html><html><head><meta charset="utf-8">' + baseTag +
      '</head><body>' + text + '</body></html>';
  }

  function renderPreview(iframe, html, options) {
    if (!iframe) {
      return;
    }
    const opts = options || {};
    const wrapped = injectBaseHref(html, opts.baseHref);
    iframe.setAttribute('sandbox', '');
    iframe.setAttribute('title', opts.title || 'HTML preview');
    iframe.src = 'about:blank';
    iframe.srcdoc = wrapped;
  }

  window.WebCoolHtml = {
    isHtmlName: isHtmlName,
    renderPreview: renderPreview
  };
}());

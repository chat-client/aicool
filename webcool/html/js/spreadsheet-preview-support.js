(function () {
  const SHEETJS_SOURCES = [
    '/webcool/html/js/vendor/xlsx.full.min.js'
  ];

  let sheetJSLoadPromise = null;

  function isSpreadsheetName(name) {
    return /\.(xls|xlsx|csv)$/i.test(String(name || ''));
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

  function loadSheetJS() {
    if (window.XLSX && typeof window.XLSX.read === 'function') {
      return Promise.resolve(window.XLSX);
    }
    if (sheetJSLoadPromise) {
      return sheetJSLoadPromise;
    }
    sheetJSLoadPromise = SHEETJS_SOURCES.reduce(function (chain, src) {
      return chain.catch(function () {
        if (window.XLSX && typeof window.XLSX.read === 'function') {
          return window.XLSX;
        }
        return loadScript(src, 'sheetjs').then(function () {
          if (!window.XLSX || typeof window.XLSX.read !== 'function') {
            throw new Error('SheetJS API not found: ' + src);
          }
          return window.XLSX;
        });
      });
    }, Promise.reject(new Error('start SheetJS loading'))).catch(function (err) {
      sheetJSLoadPromise = null;
      throw new Error('SheetJS local library load failed, please check /webcool/html/js/vendor/xlsx.full.min.js: ' + err.message);
    });
    return sheetJSLoadPromise;
  }

  function escapeHTML(value) {
    return String(value == null ? '' : value)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function sheetToRows(sheet) {
    const rows = window.XLSX.utils.sheet_to_json(sheet, {
      header: 1,
      raw: false,
      defval: ''
    });
    return rows.slice(0, 2000).map(function (row) {
      return Array.isArray(row) ? row.slice(0, 200) : [];
    });
  }

  function renderSheetTable(rows) {
    if (!rows.length) {
      return '<div class="spreadsheet-empty">空工作表</div>';
    }
    let html = '<table class="spreadsheet-table"><tbody>';
    rows.forEach(function (row, rowIndex) {
      html += '<tr><th class="spreadsheet-row-head">' + (rowIndex + 1) + '</th>';
      const width = Math.max(row.length, 1);
      for (let i = 0; i < width; i += 1) {
        html += '<td>' + escapeHTML(row[i] || '') + '</td>';
      }
      html += '</tr>';
    });
    html += '</tbody></table>';
    return html;
  }

  function renderWorkbook(container, workbook) {
    const names = workbook.SheetNames || [];
    if (!names.length) {
      container.innerHTML = '<div class="spreadsheet-empty">没有可显示的工作表</div>';
      return;
    }
    const tabs = names.map(function (name, index) {
      return '<button type="button" class="spreadsheet-tab' + (index === 0 ? ' active' : '') +
        '" data-spreadsheet-tab="' + index + '">' + escapeHTML(name) + '</button>';
    }).join('');
    const sheets = names.map(function (name, index) {
      const rows = sheetToRows(workbook.Sheets[name]);
      return '<div class="spreadsheet-sheet" data-spreadsheet-sheet="' + index + '"' +
        (index === 0 ? '' : ' hidden') + '>' + renderSheetTable(rows) + '</div>';
    }).join('');
    container.innerHTML = '<div class="spreadsheet-tabs">' + tabs + '</div>' +
      '<div class="spreadsheet-sheets">' + sheets + '</div>';
    container.querySelectorAll('[data-spreadsheet-tab]').forEach(function (btn) {
      btn.addEventListener('click', function () {
        const index = btn.getAttribute('data-spreadsheet-tab') || '0';
        container.querySelectorAll('[data-spreadsheet-tab]').forEach(function (item) {
          item.classList.toggle('active', item === btn);
        });
        container.querySelectorAll('[data-spreadsheet-sheet]').forEach(function (sheet) {
          sheet.hidden = sheet.getAttribute('data-spreadsheet-sheet') !== index;
        });
      });
    });
  }

  async function renderPreview(container, url, options) {
    if (!container) {
      throw new Error('spreadsheet preview container missing');
    }
    const opts = options || {};
    container.innerHTML = '<div class="office-preview-loading">' + (opts.loadingText || '正在加载表格预览...') + '</div>';
    const api = await loadSheetJS();
    const headers = opts.token ? { Authorization: 'Bearer ' + opts.token } : undefined;
    const res = await fetch(url, {
      credentials: 'same-origin',
      headers: headers
    });
    if (!res.ok) {
      throw new Error('spreadsheet download failed: HTTP ' + res.status);
    }
    const data = await res.arrayBuffer();
    const workbook = api.read(data, {
      type: 'array',
      cellDates: true
    });
    renderWorkbook(container, workbook);
  }

  window.WebCoolSpreadsheetPreview = {
    isSpreadsheetName: isSpreadsheetName,
    renderPreview: renderPreview
  };
}());

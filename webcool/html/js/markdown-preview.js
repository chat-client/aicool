(function () {
  function mdEscapeHtml(value) {
    return String(value == null ? '' : value)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function isMarkdownName(name) {
    return /\.(md|markdown|mdown|mkdn)$/i.test(String(name || ''));
  }

  function isSafeUrl(url) {
    const text = String(url || '').trim();
    return !text || /^(https?:|mailto:|tel:|#|\/|\.\.?\/)/i.test(text);
  }

  function escapeUrl(url) {
    const text = String(url || '').trim();
    return isSafeUrl(text) ? mdEscapeHtml(text) : '#';
  }

  function slugify(text, used) {
    let slug = String(text || '')
      .toLowerCase()
      .replace(/<[^>]*>/g, '')
      .replace(/&[a-z0-9#]+;/gi, '')
      .replace(/[^\w\u4e00-\u9fa5\s-]/g, '')
      .replace(/\s+/g, '-')
      .replace(/-+/g, '-')
      .replace(/^-+|-+$/g, '');
    if (!slug) {
      slug = 'section';
    }
    const count = used[slug] || 0;
    used[slug] = count + 1;
    return count ? slug + '-' + count : slug;
  }

  function findElementById(root, id) {
    if (!root || !id) {
      return null;
    }
    const safeId = String(id).replace(/\\/g, '\\\\').replace(/"/g, '\\"');
    try {
      return root.querySelector('[id="' + safeId + '"]');
    } catch (e) {
      return null;
    }
  }

  function findScrollableParent(node, boundary) {
    let current = node;
    while (current && current !== boundary && current !== document.body) {
      const style = window.getComputedStyle(current);
      const overflowY = style.overflowY;
      if ((overflowY === 'auto' || overflowY === 'scroll' || overflowY === 'overlay') &&
          current.scrollHeight > current.clientHeight + 1) {
        return current;
      }
      current = current.parentElement;
    }
    return boundary;
  }

  function scrollElementIntoView(target, boundary) {
    if (!target) {
      return;
    }
    const scroller = findScrollableParent(target, boundary) || boundary;
    if (!scroller) {
      target.scrollIntoView({ block: 'start', behavior: 'smooth' });
      return;
    }
    const top = target.getBoundingClientRect().top -
      scroller.getBoundingClientRect().top +
      scroller.scrollTop - 12;
    if (typeof scroller.scrollTo === 'function') {
      scroller.scrollTo({ top: Math.max(0, top), behavior: 'smooth' });
    } else {
      scroller.scrollTop = Math.max(0, top);
    }
  }

  function bindMarkdownAnchors(container) {
    if (!container || container.__webcoolMarkdownAnchorsBound) {
      return;
    }
    container.__webcoolMarkdownAnchorsBound = true;
    container.addEventListener('click', function (event) {
      const link = event.target && event.target.closest
        ? event.target.closest('a[href^="#"]')
        : null;
      if (!link || !container.contains(link)) {
        return;
      }
      const href = String(link.getAttribute('href') || '').trim();
      const id = decodeURIComponent(href.slice(1));
      if (!id) {
        return;
      }
      const target = findElementById(container, id);
      if (!target) {
        return;
      }
      event.preventDefault();
      scrollElementIntoView(target, container);
    });
  }

  function inlineMarkdown(text) {
    let value = mdEscapeHtml(text || '');
    const code = [];
    value = value.replace(/`([^`]+)`/g, function (_, body) {
      const token = '\u0000CODE' + code.length + '\u0000';
      code.push('<code>' + body + '</code>');
      return token;
    });
    value = value.replace(/!\[([^\]]*)\]\(([^)\s]+)(?:\s+"([^"]*)")?\)/g, function (_, alt, url, title) {
      return '<img src="' + escapeUrl(url) + '" alt="' + mdEscapeHtml(alt) + '"' + (title ? ' title="' + mdEscapeHtml(title) + '"' : '') + '>';
    });
    value = value.replace(/\[([^\]]+)\]\(([^)\s]+)(?:\s+"([^"]*)")?\)/g, function (_, label, url, title) {
      const href = escapeUrl(url);
      const isHashLink = String(url || '').trim().charAt(0) === '#';
      const externalAttrs = isHashLink ? '' : ' target="_blank" rel="noopener noreferrer"';
      return '<a href="' + href + '"' + externalAttrs +
        (title ? ' title="' + mdEscapeHtml(title) + '"' : '') + '>' + label + '</a>';
    });
    value = value.replace(/(\*\*|__)(.+?)\1/g, '<strong>$2</strong>');
    value = value.replace(/(\*|_)([^*_]+?)\1/g, '<em>$2</em>');
    value = value.replace(/~~(.+?)~~/g, '<del>$1</del>');
    value = value.replace(/\u0000CODE(\d+)\u0000/g, function (_, index) {
      return code[Number(index)] || '';
    });
    return value;
  }

  function parseTable(lines, index) {
    if (index + 1 >= lines.length) {
      return null;
    }
    const head = lines[index];
    const divider = lines[index + 1];
    if (head.indexOf('|') < 0 || !/^\s*\|?\s*:?-{3,}:?\s*(\|\s*:?-{3,}:?\s*)+\|?\s*$/.test(divider)) {
      return null;
    }
    function cells(line) {
      return String(line || '').trim().replace(/^\|/, '').replace(/\|$/, '').split('|').map(function (cell) {
        return cell.trim();
      });
    }
    const headers = cells(head);
    const aligns = cells(divider).map(function (cell) {
      const left = /^:/.test(cell);
      const right = /:$/.test(cell);
      return left && right ? 'center' : (right ? 'right' : (left ? 'left' : ''));
    });
    let rowIndex = index + 2;
    const rows = [];
    while (rowIndex < lines.length && lines[rowIndex].indexOf('|') >= 0 && String(lines[rowIndex]).trim()) {
      rows.push(cells(lines[rowIndex]));
      rowIndex += 1;
    }
    let html = '<table><thead><tr>';
    headers.forEach(function (cell, cellIndex) {
      html += '<th' + (aligns[cellIndex] ? ' style="text-align:' + aligns[cellIndex] + '"' : '') + '>' + inlineMarkdown(cell) + '</th>';
    });
    html += '</tr></thead>';
    if (rows.length) {
      html += '<tbody>';
      rows.forEach(function (row) {
        html += '<tr>';
        headers.forEach(function (_, cellIndex) {
          html += '<td' + (aligns[cellIndex] ? ' style="text-align:' + aligns[cellIndex] + '"' : '') + '>' + inlineMarkdown(row[cellIndex] || '') + '</td>';
        });
        html += '</tr>';
      });
      html += '</tbody>';
    }
    html += '</table>';
    return { html: html, nextIndex: rowIndex };
  }

  const allowedHTMLTags = new Set([
    'a', 'br', 'caption', 'code', 'del', 'details', 'div', 'em', 'img', 'kbd',
    'li', 'ol', 'p', 'pre', 'span', 'strong', 'sub', 'summary', 'sup', 'table',
    'tbody', 'td', 'tfoot', 'th', 'thead', 'tr', 'ul'
  ]);

  const htmlBlockTags = new Set([
    'div', 'table', 'thead', 'tbody', 'tfoot', 'tr', 'td', 'th', 'details', 'summary'
  ]);

  function htmlTagName(line) {
    const match = String(line || '').trim().match(/^<\/?\s*([A-Za-z][A-Za-z0-9-]*)\b/);
    return match ? match[1].toLowerCase() : '';
  }

  function isHTMLBlockStart(line) {
    const tag = htmlTagName(line);
    return tag && allowedHTMLTags.has(tag);
  }

  function matchesClosingTag(line, tag) {
    return new RegExp('<\\/\\s*' + tag.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '\\s*>', 'i')
      .test(String(line || ''));
  }

  function sanitizeRawHTML(html) {
    return String(html || '')
      .replace(/<\s*(script|style|iframe|object|embed)\b[^>]*>[\s\S]*?<\s*\/\s*\1\s*>/gi, '')
      .replace(/<\s*\/?\s*(script|style|iframe|object|embed)\b[^>]*>/gi, '')
      .replace(/\s+on[A-Za-z]+\s*=\s*("[^"]*"|'[^']*'|[^\s>]+)/gi, '')
      .replace(/\s+(href|src)\s*=\s*(["'])\s*javascript:[^"']*\2/gi, ' $1="#"')
      .replace(/\s+(href|src)\s*=\s*javascript:[^\s>]+/gi, ' $1="#"');
  }

  function parseHTMLBlock(lines, index) {
    if (index >= lines.length || !isHTMLBlockStart(lines[index])) {
      return null;
    }
    const rootTag = htmlTagName(lines[index]);
    const body = [];
    let rowIndex = index;

    if (rootTag && htmlBlockTags.has(rootTag)) {
      while (rowIndex < lines.length) {
        const line = lines[rowIndex];
        body.push(line);
        rowIndex += 1;
        if (matchesClosingTag(line, rootTag)) {
          break;
        }
      }
    } else {
      while (rowIndex < lines.length) {
        const line = lines[rowIndex];
        if (!isHTMLBlockStart(line) && String(line || '').trim()) {
          break;
        }
        body.push(line);
        rowIndex += 1;
        if (!String(line || '').trim()) {
          break;
        }
      }
    }

    return { html: sanitizeRawHTML(body.join('\n')), nextIndex: rowIndex };
  }

  function renderMarkdown(markdown) {
    const lines = String(markdown == null ? '' : markdown).replace(/\r\n?/g, '\n').split('\n');
    const usedSlugs = {};
    const html = [];
    let i = 0;

    function collectParagraph() {
      const parts = [];
      const start = i;
      while (i < lines.length && String(lines[i]).trim()) {
        if (/^\s*(```|~~~)/.test(lines[i]) || /^\s{0,3}(#{1,6})\s+/.test(lines[i]) || /^\s*[-*_]{3,}\s*$/.test(lines[i]) || /^\s*>/.test(lines[i]) || isHTMLBlockStart(lines[i]) || /^\s*([-+*]|\d+\.)\s+/.test(lines[i])) {
          break;
        }
        parts.push(lines[i]);
        i += 1;
      }
      if (parts.length) {
        html.push('<p>' + inlineMarkdown(parts.join(' ')) + '</p>');
      } else if (i === start && i < lines.length) {
        i += 1;
      }
    }

    while (i < lines.length) {
      const line = lines[i];
      const trimmed = String(line || '').trim();
      if (!trimmed) {
        i += 1;
        continue;
      }

      const fence = line.match(/^\s*(```|~~~)\s*(.*?)\s*$/);
      if (fence) {
        const marker = fence[1];
        const lang = fence[2] || '';
        i += 1;
        const body = [];
        while (i < lines.length && !new RegExp('^\\s*' + marker + '\\s*$').test(lines[i])) {
          body.push(lines[i]);
          i += 1;
        }
        if (i < lines.length) {
          i += 1;
        }
        html.push('<pre><code' + (lang ? ' class="language-' + mdEscapeHtml(lang) + '"' : '') + '>' + mdEscapeHtml(body.join('\n')) + '</code></pre>');
        continue;
      }

      const heading = line.match(/^\s{0,3}(#{1,6})\s+(.+?)\s*#*\s*$/);
      if (heading) {
        const level = heading[1].length;
        const body = inlineMarkdown(heading[2]);
        html.push('<h' + level + ' id="' + mdEscapeHtml(slugify(heading[2], usedSlugs)) + '">' + body + '</h' + level + '>');
        i += 1;
        continue;
      }

      if (/^\s*[-*_]{3,}\s*$/.test(line)) {
        html.push('<hr>');
        i += 1;
        continue;
      }

      if (/^\s*>/.test(line)) {
        const parts = [];
        while (i < lines.length && /^\s*>/.test(lines[i])) {
          parts.push(lines[i].replace(/^\s*>\s?/, ''));
          i += 1;
        }
        html.push('<blockquote>' + renderMarkdown(parts.join('\n')) + '</blockquote>');
        continue;
      }

      const htmlBlock = parseHTMLBlock(lines, i);
      if (htmlBlock) {
        html.push(htmlBlock.html);
        i = htmlBlock.nextIndex;
        continue;
      }

      const table = parseTable(lines, i);
      if (table) {
        html.push(table.html);
        i = table.nextIndex;
        continue;
      }

      const list = line.match(/^(\s*)([-+*]|\d+\.)\s+(.*)$/);
      if (list) {
        const ordered = /\d+\./.test(list[2]);
        const tag = ordered ? 'ol' : 'ul';
        html.push('<' + tag + '>');
        while (i < lines.length) {
          const item = lines[i].match(/^(\s*)([-+*]|\d+\.)\s+(.*)$/);
          if (!item || (/\d+\./.test(item[2]) !== ordered)) {
            break;
          }
          const task = item[3].match(/^\[([ xX])\]\s+(.*)$/);
          if (task) {
            html.push('<li class="task-list-item"><input type="checkbox" disabled' + (task[1].toLowerCase() === 'x' ? ' checked' : '') + '> ' + inlineMarkdown(task[2]) + '</li>');
          } else {
            html.push('<li>' + inlineMarkdown(item[3]) + '</li>');
          }
          i += 1;
        }
        html.push('</' + tag + '>');
        continue;
      }

      collectParagraph();
    }

    return html.join('\n') || '<p class="markdown-empty">空文档</p>';
  }

  window.WebCoolMarkdown = {
    isMarkdownName: isMarkdownName,
    render: renderMarkdown,
    bindAnchors: bindMarkdownAnchors
  };
}());

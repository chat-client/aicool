(function (global) {
  'use strict';

  const LOCAL_VIEWER_URL = '/webcool/html/js/vendor/xmind-official/embed-viewer.html?pitch-mode=disabled';

  function getElement(el) {
    if (typeof el === 'string') {
      return document.querySelector(el);
    }
    return el;
  }

  function XMindEmbedViewer(options) {
    const opts = options || {};
    const mount = getElement(opts.el);
    if (!mount) {
      throw new Error('IFrame or mount element not found');
    }

    this.internalState = {
      sheets: [],
      zoomScale: 100,
      currentSheetId: ''
    };
    this.handlers = {};
    this.eventIndex = 0;
    this.channel = new MessageChannel();

    const iframe = document.createElement('iframe');
    iframe.setAttribute('frameborder', '0');
    iframe.setAttribute('scrolling', 'no');
    iframe.setAttribute('allowfullscreen', 'true');
    iframe.setAttribute('allow', 'allowfullscreen');
    iframe.src = LOCAL_VIEWER_URL;
    mount.appendChild(iframe);
    this.iframe = iframe;

    this.setStyles(opts.styles || { height: '350px', width: '750px' });
    this.channelSetupPromise = this.setupChannel();
    this.addEventListener('sheet-switch', (sheetId) => {
      this.internalState.currentSheetId = sheetId || '';
    });
    this.addEventListener('zoom-change', (scale) => {
      this.internalState.zoomScale = scale || 100;
    });
    this.addEventListener('sheets-load', (sheets) => {
      this.internalState.sheets = sheets || [];
    });

    if (opts.file) {
      this.load(opts.file);
    }
  }

  XMindEmbedViewer.prototype.setupChannel = function () {
    return new Promise((resolve) => {
      this.iframe.addEventListener('load', () => {
        this.channel.port1.start();
        const readyHandler = (event) => {
          const data = event.data || [];
          if (data[0] !== 'channel-ready') {
            return;
          }
          event.preventDefault();
          this.channel.port1.removeEventListener('message', readyHandler);
          this.channel.port1.addEventListener('message', this.dispatchEvent.bind(this));
          resolve();
        };
        this.channel.port1.addEventListener('message', readyHandler);
        this.iframe.contentWindow.postMessage(
          ['setup-channel', { port: this.channel.port2 }],
          window.location.origin,
          [this.channel.port2]
        );
      }, { once: true });
    });
  };

  XMindEmbedViewer.prototype.dispatchEvent = function (event) {
    const data = event.data || [];
    const type = data[0];
    const name = data[1];
    const payload = data[2];
    if (type === 'event' && this.handlers[name]) {
      this.handlers[name].forEach((handler) => handler(payload));
    }
  };

  XMindEmbedViewer.prototype.addEventListener = function (name, handler) {
    this.handlers[name] = this.handlers[name] || [];
    if (!this.handlers[name].includes(handler)) {
      this.handlers[name].push(handler);
    }
  };

  XMindEmbedViewer.prototype.removeEventListener = function (name, handler) {
    const list = this.handlers[name];
    if (!list) {
      return;
    }
    const index = list.indexOf(handler);
    if (index >= 0) {
      list.splice(index, 1);
    }
  };

  XMindEmbedViewer.prototype.emit = function (name, payload) {
    return this.channelSetupPromise.then(() => new Promise((resolve) => {
      const eventId = 'xmind-embed-viewer#' + this.eventIndex++;
      const replyHandler = (event) => {
        const data = event.data || [];
        if (data[0] !== eventId) {
          return;
        }
        this.channel.port1.removeEventListener('message', replyHandler);
        resolve(data[1]);
      };
      this.channel.port1.addEventListener('message', replyHandler);
      this.channel.port1.postMessage([name, payload, eventId]);
    }));
  };

  XMindEmbedViewer.prototype.setStyles = function (styles) {
    Object.keys(styles || {}).forEach((key) => {
      this.iframe.style[key] = styles[key];
    });
  };

  XMindEmbedViewer.prototype.load = function (file) {
    return this.emit('open-file', file);
  };

  XMindEmbedViewer.prototype.setZoomScale = function (scale) {
    return this.emit('zoom', scale);
  };

  XMindEmbedViewer.prototype.setFitMap = function () {
    return this.emit('fit-map');
  };

  XMindEmbedViewer.prototype.switchSheet = function (sheetId) {
    return this.emit('switch-sheet', sheetId);
  };

  Object.defineProperty(XMindEmbedViewer.prototype, 'zoom', {
    get: function () {
      return this.internalState.zoomScale;
    }
  });

  Object.defineProperty(XMindEmbedViewer.prototype, 'sheets', {
    get: function () {
      return JSON.parse(JSON.stringify(this.internalState.sheets));
    }
  });

  Object.defineProperty(XMindEmbedViewer.prototype, 'currentSheetId', {
    get: function () {
      return this.internalState.currentSheetId;
    }
  });

  global.XMindEmbedViewer = XMindEmbedViewer;
})(window);

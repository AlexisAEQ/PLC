// PLC Studio — faux automate pour l'aperçu de l'interface web réelle (data/index.js).
//
// Chargé AVANT index.js dans preview.html. Il remplace :
//  - fetch() de config.json / interface.json par les fichiers générés par l'éditeur ;
//  - WebSocket par un faux serveur qui répond comme BorneUniverselle (états, heartbeat,
//    activation de l'interface).
// Les données viennent de la page parente (même origine) : window.parent.plcPreview.
(function () {
  'use strict';
  const nativeJSON = window.JSON;
  const nativeFetch = window.fetch.bind(window);
  const host = () => (window.parent && window.parent !== window ? window.parent.plcPreview : null) || {};

  window.fetch = function (input, init) {
    const url = typeof input === 'string' ? input : input && input.url ? input.url : String(input);
    const path = new URL(url, location.href).pathname;
    const data = host();
    const json = (obj) => Promise.resolve(new Response(nativeJSON.stringify(obj), { status: 200, headers: { 'Content-Type': 'application/json' } }));
    if (/\/config\.json$/.test(path) && data.config) return json(data.config);
    if (/\/interface\.json$/.test(path) && data.interface) return json(data.interface);
    // Les autres fichiers absolus (/component-manifest.json…) sont servis depuis /hmi/.
    if (path.startsWith('/') && !path.startsWith('/hmi/') && !path.startsWith('/api/')) return nativeFetch('/hmi' + path, init);
    return nativeFetch(input, init);
  };

  class FakeSocket {
    constructor() {
      this.readyState = 0;
      this.listeners = {};
      FakeSocket.instance = this;
      setTimeout(() => {
        this.readyState = 1;
        this.onopen && this.onopen({});
        this.emit('open', {});
        this.heartbeat = setInterval(() => this.push({ heartbeat: 'true' }), 400);
      }, 30);
    }
    addEventListener(type, fn) {
      (this.listeners[type] = this.listeners[type] || []).push(fn);
    }
    removeEventListener(type, fn) {
      this.listeners[type] = (this.listeners[type] || []).filter((f) => f !== fn);
    }
    emit(type, ev) {
      (this.listeners[type] || []).forEach((fn) => fn(ev));
    }
    push(obj) {
      if (this.readyState !== 1) return;
      const ev = { data: nativeJSON.stringify(obj) };
      this.onmessage && this.onmessage(ev);
      this.emit('message', ev);
    }
    close(code, reason) {
      this.readyState = 3;
      clearInterval(this.heartbeat);
      const ev = { code: code || 1000, reason: reason || '' };
      this.onclose && this.onclose(ev);
      this.emit('close', ev);
    }
    send(text) {
      let msg;
      try {
        msg = window.JSON.parse(text); // JSON5 (remplacé par index.js)
      } catch (e) {
        return;
      }
      const key = Object.keys(msg)[0];
      const data = host();
      const states = () => (data.states ? data.states() : []);
      setTimeout(() => {
        switch (key) {
          case 'suspend_plc':
            this.push({ plc_suspended: !!msg.suspend_plc });
            break;
          case 'allStatesRequest':
            this.push({ interfaceSuspend: true });
            this.push({ states: states() });
            this.push({ interfaceSuspend: false });
            break;
          case 'initialStatesLoaded':
            this.push({ enableInterface: true });
            break;
          case 'get': {
            const s = states().find((x) => x.hash === msg.get);
            if (s) this.push({ states: [s] });
            break;
          }
          case 'states':
            if (data.onStates) data.onStates(msg.states);
            break;
          case 'is_plc_idle':
            this.push({ plc_is_idle: true });
            break;
          case 'directory':
            this.push({ Directory: ['config.json', 'interface.json'] });
            break;
          default:
            break;
        }
      }, 5);
    }
  }
  FakeSocket.OPEN = 1;
  FakeSocket.CLOSED = 3;
  FakeSocket.CONNECTING = 0;
  window.WebSocket = FakeSocket;

  // Accès pour la page parente : pousser des états, savoir quand l'interface est prête.
  window.plcPreviewPush = (obj) => FakeSocket.instance && FakeSocket.instance.push(obj);
})();

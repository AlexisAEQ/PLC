// Aperçu de l'interface web RÉELLE du firmware (data/index.js) dans un iframe,
// branché sur un faux automate (public/hmi/plc-shim.js), avec les outils d'édition :
// survol, sélection au clic, glisser-déposer des widgets.
//
// Correspondance DOM -> modèle : hw-dynamic (shadow) > .page-content > hw-section[s]
//   > .horizontal-wrap|… > hw-dock[i]. s = index de la section dans la page affichée,
//   i = index du widget dans la section.

const BLOCKED = ['click', 'dblclick', 'mousedown', 'mouseup', 'pointerdown', 'pointerup', 'touchstart', 'touchend', 'keydown', 'change', 'input', 'sl-change', 'sl-input'];

export class HmiPreview {
  // callbacks : { onSelect(sel), onDrop(source, target), onPage(index), getDrag() }
  constructor(callbacks) {
    this.cb = callbacks;
    this.mode = 'edit';
    this.page = 0;
    this.selection = null;
    this.ready = false;
    this.values = new Map();
    this.iframe = document.createElement('iframe');
    this.iframe.className = 'hmi-frame';
    this.iframe.title = 'Aperçu de l’interface opérateur';
    this.iframe.addEventListener('load', () => this.onLoad());
  }

  get win() {
    return this.iframe.contentWindow;
  }
  get doc() {
    return this.iframe.contentDocument;
  }
  get dynamic() {
    return this.doc?.querySelector('hw-dynamic');
  }
  get content() {
    return this.dynamic?.shadowRoot?.querySelector('.page-content') || this.dynamic?.querySelector('.page-content');
  }

  // data : { config, interface, states: [{hash, value}] }
  setData(data) {
    this.data = data;
    for (const s of data.states) if (!this.values.has(s.hash)) this.values.set(s.hash, s.value);
    window.plcPreview = {
      config: data.config,
      interface: data.interface,
      states: () => data.states.map((s) => ({ hash: s.hash, value: this.values.has(s.hash) ? this.values.get(s.hash) : s.value })),
      onStates: (list) => {
        // Mode essai : le faux automate renvoie la valeur écrite par le widget.
        for (const s of list || []) this.values.set(s.hash, s.value);
        if (this.win?.plcPreviewPush) this.win.plcPreviewPush({ states: list });
      },
    };
  }

  mount(container) {
    container.append(this.iframe);
    if (!this.iframe.src) this.iframe.src = '/hmi/preview.html';
  }

  async onLoad() {
    this.ready = false;
    const ok = await this.waitFor(() => this.content && this.content.querySelector('hw-section'), 8000);
    if (!ok) return;
    // Lever la protection de démarrage (5 s) qui bloque les rechargements de l'interface.
    this.dynamic._canReload = true;
    this.installListeners();
    this.ready = true;
    this.cb.onReady?.();
    await this.showPage(this.page);
  }

  waitFor(test, timeout = 3000) {
    return new Promise((resolve) => {
      const t0 = performance.now();
      const tick = () => {
        let ok = false;
        try {
          ok = !!test();
        } catch {
          ok = false;
        }
        if (ok) return resolve(true);
        if (performance.now() - t0 > timeout) return resolve(false);
        setTimeout(tick, 40);
      };
      tick();
    });
  }

  // Recharge l'interface (après une modification) sans recharger la page.
  async refresh() {
    if (!this.ready) return;
    // hw-dynamic vide la page puis relit config.json et interface.json (via plc-shim.js).
    this.doc.dispatchEvent(new CustomEvent('interface-reload'));
    await new Promise((r) => setTimeout(r, 60));
    await this.waitFor(() => this.content?.querySelector('hw-section') || !this.data?.interface?.pages?.[0]?.sections?.length, 2000);
    await this.showPage(this.page);
  }

  async showPage(index) {
    this.page = index;
    if (!this.ready) return;
    const dyn = this.dynamic;
    try {
      if (dyn._currentPage !== index) dyn.handlePageChange({ detail: { pageIndex: index } });
      const nav = this.doc.querySelector('hw-navbar');
      nav?._tabGroup?.show?.(`panel-${index}`);
    } catch {
      /* page absente */
    }
    await this.waitFor(() => this.content?.querySelector('hw-section') || !this.data?.interface?.pages?.[index]?.sections?.length, 1500);
    this.decorate();
  }

  setMode(mode) {
    this.mode = mode;
    this.decorate();
  }

  setSelection(sel) {
    this.selection = sel;
    this.decorate();
  }

  sections() {
    return [...(this.content?.querySelectorAll(':scope > hw-section') || [])];
  }
  docks(section) {
    return [...section.querySelectorAll('hw-dock')];
  }

  // Repères visuels (sélection, éléments déplaçables).
  decorate() {
    if (!this.content) return;
    const sel = this.selection;
    this.sections().forEach((sec, s) => {
      const box = sec.querySelector('.section') || sec;
      const secSelected = sel && sel.kind === 'section' && sel.page === this.page && sel.s === s;
      box.style.outline = secSelected ? '3px solid #3b82f6' : '';
      box.style.outlineOffset = '3px';
      this.docks(sec).forEach((dock, i) => {
        const selected = sel && sel.kind === 'item' && sel.page === this.page && sel.s === s && sel.i === i;
        dock.style.outline = selected ? '3px solid #3b82f6' : '';
        dock.style.outlineOffset = '3px';
        dock.style.borderRadius = '8px';
        dock.setAttribute('draggable', this.mode === 'edit' ? 'true' : 'false');
        dock.style.cursor = this.mode === 'edit' ? 'grab' : '';
      });
    });
  }

  // Position d'un événement dans la structure : { s, i?, dock?, section }.
  locate(e) {
    const path = e.composedPath ? e.composedPath() : [];
    const dock = path.find((n) => n.tagName === 'HW-DOCK');
    const section = path.find((n) => n.tagName === 'HW-SECTION');
    if (!section) return null;
    const s = this.sections().indexOf(section);
    if (s < 0) return null;
    const i = dock ? this.docks(section).indexOf(dock) : -1;
    return { s, i, dock, section, inContent: path.includes(this.dynamic) };
  }

  clearIndicators() {
    if (!this.content) return;
    for (const d of this.content.querySelectorAll('hw-dock')) d.style.boxShadow = '';
    for (const s of this.sections()) (s.querySelector('.section') || s).style.boxShadow = '';
  }

  dropTarget(e) {
    const loc = this.locate(e);
    if (!loc) return null;
    if (loc.dock && loc.i >= 0) {
      const r = loc.dock.getBoundingClientRect();
      const wrap = loc.dock.parentElement;
      const horizontal = wrap && getComputedStyle(wrap).flexDirection !== 'column' && /horizontal|wrap/.test(wrap.className || '');
      const after = horizontal ? e.clientX > r.left + r.width / 2 : e.clientY > r.top + r.height / 2;
      return { s: loc.s, i: after ? loc.i + 1 : loc.i, dock: loc.dock, after, horizontal };
    }
    return { s: loc.s, i: this.docks(loc.section).length, section: loc.section };
  }

  installListeners() {
    const w = this.win;
    if (!w || w.__plcStudio) return;
    w.__plcStudio = true;
    const inContent = (e) => (e.composedPath ? e.composedPath() : []).includes(this.dynamic);

    // Mode édition : les widgets ne réagissent pas, le clic sélectionne.
    for (const type of BLOCKED) {
      w.addEventListener(
        type,
        (e) => {
          if (this.mode !== 'edit' || !inContent(e)) return;
          e.stopPropagation();
          if (type === 'click') {
            e.preventDefault();
            const loc = this.locate(e);
            if (!loc) return;
            if (loc.dock && loc.i >= 0) this.cb.onSelect({ kind: 'item', page: this.page, s: loc.s, i: loc.i });
            else this.cb.onSelect({ kind: 'section', page: this.page, s: loc.s });
          } else if (type !== 'mousedown' && type !== 'pointerdown') {
            // mousedown/pointerdown gardent leur action par défaut pour permettre le glisser.
            e.preventDefault();
          }
        },
        true
      );
    }

    let hovered = null;
    w.addEventListener(
      'mouseover',
      (e) => {
        if (this.mode !== 'edit') return;
        const loc = this.locate(e);
        const target = loc?.dock || (loc ? loc.section.querySelector('.section') : null);
        if (hovered && hovered !== target && !hovered.style.outline.includes('solid')) hovered.style.outline = '';
        if (target && !target.style.outline.includes('solid')) target.style.outline = '2px dashed #60a5fa';
        hovered = target;
      },
      true
    );

    // Glisser-déposer
    let source = null;
    w.addEventListener(
      'dragstart',
      (e) => {
        if (this.mode !== 'edit') return;
        const loc = this.locate(e);
        if (!loc || !loc.dock) return;
        source = { type: 'item', page: this.page, s: loc.s, i: loc.i };
        e.dataTransfer.effectAllowed = 'move';
        try {
          e.dataTransfer.setData('text/plain', 'plc-item');
        } catch {
          /* ignoré */
        }
      },
      true
    );
    w.addEventListener(
      'dragover',
      (e) => {
        const src = source || this.cb.getDrag?.();
        if (this.mode !== 'edit' || !src) return;
        const t = this.dropTarget(e);
        this.clearIndicators();
        if (!t) return;
        e.preventDefault();
        if (t.dock) t.dock.style.boxShadow = t.horizontal ? (t.after ? '6px 0 0 0 #f59e0b' : '-6px 0 0 0 #f59e0b') : t.after ? '0 6px 0 0 #f59e0b' : '0 -6px 0 0 #f59e0b';
        else (t.section.querySelector('.section') || t.section).style.boxShadow = 'inset 0 0 0 3px #f59e0b';
      },
      true
    );
    w.addEventListener(
      'drop',
      (e) => {
        const src = source || this.cb.getDrag?.();
        this.clearIndicators();
        if (this.mode !== 'edit' || !src) return;
        const t = this.dropTarget(e);
        source = null;
        if (!t) return;
        e.preventDefault();
        this.cb.onDrop(src, { page: this.page, s: t.s, i: t.i });
      },
      true
    );
    w.addEventListener(
      'dragend',
      () => {
        source = null;
        this.clearIndicators();
      },
      true
    );

    // Changement d'onglet dans la barre de navigation de l'aperçu.
    const nav = this.doc.querySelector('hw-navbar');
    nav?.addEventListener('page-change', (e) => {
      const idx = e.detail?.pageIndex;
      if (typeof idx === 'number' && idx !== this.page) {
        this.page = idx;
        this.cb.onPage?.(idx);
        setTimeout(() => this.decorate(), 50);
      }
    });
  }
}

// Petits utilitaires DOM sans framework.

export function h(tag, attrs = {}, ...children) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
    else if (k === 'on') for (const [ev, fn] of Object.entries(v)) el.addEventListener(ev, fn);
    else if (k === 'value') el.value = v;
    else if (k === 'checked') el.checked = !!v;
    else if (k === 'selected') el.selected = !!v;
    else if (k === 'disabled') el.disabled = !!v;
    else if (k === 'html') el.innerHTML = v;
    else if (k === 'dataset') Object.assign(el.dataset, v);
    else el.setAttribute(k, v === true ? '' : v);
  }
  append(el, children);
  return el;
}

function append(el, children) {
  for (const c of children.flat(Infinity)) {
    if (c === null || c === undefined || c === false) continue;
    el.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
}

export function clear(el) {
  while (el.firstChild) el.removeChild(el.firstChild);
  return el;
}

// Champ texte/nombre lié à un objet : met à jour obj[key] à chaque saisie.
export function input(obj, key, { type = 'text', placeholder = '', onInput, onChange, cls = '', parse, min, max, step, title } = {}) {
  const el = h('input', { type, placeholder, class: cls, min, max, step, title });
  el.value = obj[key] ?? '';
  el.addEventListener('input', () => {
    let v = el.value;
    if (type === 'number') v = v === '' ? '' : Number(v);
    if (parse) v = parse(v);
    obj[key] = v;
    onInput?.(v, el);
  });
  if (onChange) el.addEventListener('change', () => onChange(obj[key], el));
  return el;
}

export function checkbox(obj, key, label, { onChange, title } = {}) {
  const box = h('input', { type: 'checkbox', checked: !!obj[key] });
  box.addEventListener('change', () => {
    obj[key] = box.checked;
    onChange?.(box.checked);
  });
  return label === undefined ? box : h('label', { class: 'check', title }, box, label);
}

export function select(obj, key, options, { onChange, cls = '' } = {}) {
  const el = h(
    'select',
    { class: cls },
    options.map((o) => {
      const opt = typeof o === 'object' ? o : { value: o, label: o };
      return h('option', { value: opt.value, selected: String(obj[key]) === String(opt.value), disabled: opt.disabled }, opt.label);
    })
  );
  el.addEventListener('change', () => {
    const opt = options.find((o) => String(typeof o === 'object' ? o.value : o) === el.value);
    obj[key] = typeof opt === 'object' ? opt.value : el.value;
    onChange?.(obj[key]);
  });
  return el;
}

export function field(label, control, help) {
  return h('label', { class: 'field' }, h('span', {}, label), control, help ? h('small', {}, help) : null);
}

export function card(title, ...children) {
  return h('section', { class: 'card' }, title ? h('h2', {}, title) : null, ...children);
}

export function pageHead(title, text, ...actions) {
  return h('div', { class: 'page-head' }, h('div', {}, h('h1', {}, title), text ? h('p', {}, text) : null), actions.length ? h('div', { class: 'btn-row' }, actions) : null);
}

export function button(label, onClick, cls = '', attrs = {}) {
  return h('button', { class: `btn ${cls}`, type: 'button', on: { click: onClick }, ...attrs }, label);
}

export function toast(message, kind = '') {
  const box = document.getElementById('toasts');
  const t = h('div', { class: `toast ${kind}` }, message);
  box.append(t);
  setTimeout(() => t.remove(), kind === 'error' ? 6000 : 3000);
}

// Fenêtre modale ; build(close) retourne le contenu.
export function modal(build) {
  const backdrop = document.getElementById('modal');
  clear(backdrop);
  const close = () => {
    backdrop.hidden = true;
    clear(backdrop);
  };
  const box = h('div', { class: 'modal', role: 'dialog' });
  append(box, [build(close)]);
  backdrop.append(box);
  backdrop.hidden = false;
  backdrop.onclick = (e) => {
    if (e.target === backdrop) close();
  };
  const first = box.querySelector('input, select, textarea, button');
  first?.focus();
  return close;
}

export function confirmDialog(title, message, okLabel = 'Confirmer') {
  return new Promise((resolve) => {
    modal((close) =>
      h(
        'div',
        {},
        h('h2', {}, title),
        h('p', {}, message),
        h(
          'div',
          { class: 'actions' },
          button('Annuler', () => {
            close();
            resolve(false);
          }),
          button(
            okLabel,
            () => {
              close();
              resolve(true);
            },
            'primary'
          )
        )
      )
    );
  });
}

export function issueBadge(issues) {
  const errors = issues.filter((i) => i.level === 'error').length;
  const warnings = issues.filter((i) => i.level === 'warning').length;
  if (errors) return h('span', { class: 'badge err', title: `${errors} erreur(s)` }, errors);
  if (warnings) return h('span', { class: 'badge warn', title: `${warnings} avertissement(s)` }, warnings);
  return null;
}

export function issueList(issues, onGoto) {
  if (!issues.length) return h('p', { class: 'muted' }, 'Aucun problème détecté.');
  return h(
    'ul',
    { class: 'issues' },
    issues.map((i) =>
      h(
        'li',
        { class: i.level },
        h('span', { class: `badge ${i.level === 'error' ? 'err' : 'warn'}` }, i.level === 'error' ? 'Erreur' : 'Attention'),
        h('span', { class: 'spacer' }, i.message),
        onGoto && i.step ? h('a', { href: '#', class: 'where', on: { click: (e) => (e.preventDefault(), onGoto(i.step)) } }, '→ corriger') : null
      )
    )
  );
}

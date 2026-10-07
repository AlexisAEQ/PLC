// Champ d'expression (réceptivité, valeur) avec vérification immédiate et suggestions.

import { tryParse, projectContext, AXIS_PROPS } from '/shared/expr.js';
import { axisList } from '/shared/axes.js';
import { h } from './ui.js';

const KEYWORDS = ['ET', 'OU', 'NON', 'VRAI', 'FAUX', 'FM(', 'FD(', 'MIN(', 'MAX(', 'ABS('];

// options : { multiline, expect: 'bool'|'num'|null, placeholder, onInput(value), steps (Set), allowEmpty }
export function exprInput(store, obj, key, options = {}) {
  const wrap = h('div', { class: 'expr-input' });
  const field = options.multiline
    ? h('textarea', { rows: 2, class: 'mono', placeholder: options.placeholder || '', spellcheck: 'false' })
    : h('input', { type: 'text', class: 'mono', placeholder: options.placeholder || '', spellcheck: 'false', autocomplete: 'off' });
  field.value = obj[key] ?? '';
  const status = h('div', { class: 'expr-status' });
  const suggest = h('div', { class: 'expr-suggest', hidden: true });
  wrap.append(field, suggest, status);

  const steps = () => options.steps || new Set((store.project.logic?.steps || []).map((s) => s.num));

  const check = () => {
    const text = field.value;
    if (!text.trim()) {
      status.className = 'expr-status';
      status.textContent = options.allowEmpty ? options.emptyHint || '' : 'Vide';
      field.classList.toggle('invalid', !options.allowEmpty);
      return;
    }
    const r = tryParse(text, projectContext(store.project, { steps: steps() }));
    let error = r.error;
    if (!error && options.expect === 'bool' && r.ast.type !== 'bool') error = 'Une condition (booléenne) est attendue.';
    if (!error && options.expect === 'num' && r.ast.type !== 'int' && r.ast.type !== 'float') error = 'Une valeur numérique est attendue.';
    field.classList.toggle('invalid', !!error);
    status.className = `expr-status ${error ? 'error' : 'ok'}`;
    status.textContent = error ? error : '✓';
  };

  // Suggestions de symboles pour le mot en cours de saisie.
  let items = [];
  let active = 0;
  const candidates = () => {
    const vars = store.project.variables.map((v) => ({ text: v.symbol, hint: v.label }));
    const stp = [...steps()].map((n) => ({ text: `X${n}`, hint: `étape ${n}` }));
    const srv = axisList(store.project).flatMap((a) => Object.entries(AXIS_PROPS).map(([k, d]) => ({ text: `${a.symbol}.${k}`, hint: `${a.label} : ${d.label}` })));
    const kw = KEYWORDS.map((k) => ({ text: k, hint: 'mot-clé' }));
    return [...vars, ...stp, ...srv, ...kw];
  };
  const currentWord = () => {
    const pos = field.selectionStart ?? field.value.length;
    const before = field.value.slice(0, pos);
    const m = /[A-Za-z_À-ÿ][A-Za-z0-9_.À-ÿ]*$/.exec(before);
    return m ? { word: m[0], start: pos - m[0].length, end: pos } : null;
  };
  const hide = () => {
    suggest.hidden = true;
    items = [];
  };
  const showSuggest = () => {
    const cw = currentWord();
    if (!cw || cw.word.length < 1) return hide();
    const w = cw.word.toLowerCase();
    items = candidates()
      .filter((c) => c.text.toLowerCase().startsWith(w) && c.text.toLowerCase() !== w)
      .slice(0, 8);
    if (!items.length) return hide();
    active = 0;
    drawSuggest();
    suggest.hidden = false;
  };
  const drawSuggest = () => {
    suggest.replaceChildren(
      ...items.map((c, i) =>
        h(
          'div',
          {
            class: `expr-suggest-item ${i === active ? 'active' : ''}`,
            on: {
              mousedown: (e) => {
                e.preventDefault();
                insert(c.text);
              },
            },
          },
          h('span', { class: 'mono' }, c.text),
          h('span', { class: 'muted' }, c.hint)
        )
      )
    );
  };
  const insert = (text) => {
    const cw = currentWord();
    if (!cw) return;
    const v = field.value;
    field.value = v.slice(0, cw.start) + text + v.slice(cw.end);
    const caret = cw.start + text.length;
    field.setSelectionRange(caret, caret);
    hide();
    commit();
  };
  const commit = () => {
    obj[key] = field.value;
    check();
    options.onInput?.(field.value);
  };

  field.addEventListener('input', () => {
    commit();
    showSuggest();
  });
  field.addEventListener('keydown', (e) => {
    if (suggest.hidden) return;
    if (e.key === 'ArrowDown') {
      active = (active + 1) % items.length;
      drawSuggest();
      e.preventDefault();
    } else if (e.key === 'ArrowUp') {
      active = (active - 1 + items.length) % items.length;
      drawSuggest();
      e.preventDefault();
    } else if (e.key === 'Enter' || e.key === 'Tab') {
      insert(items[active].text);
      e.preventDefault();
    } else if (e.key === 'Escape') {
      hide();
    }
  });
  field.addEventListener('blur', () => setTimeout(hide, 150));
  check();
  wrap.recheck = check;
  return wrap;
}

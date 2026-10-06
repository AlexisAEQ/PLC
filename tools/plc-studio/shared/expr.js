// Langage des réceptivités et des expressions d'action.
//
//   Depart ET NON Porte_ouverte
//   FM(Capteur) OU ↑Bouton            front montant (FD / ↓ : front descendant)
//   X3.t >= 5s     ou   t/X3/5s       temps d'activité d'une étape
//   Compteur >= Nb_pieces             comparaisons : = <> < <= > >=
//   Servo.enPosition                  états du servo
//
// Opérateurs logiques : ET/AND/&&, OU/OR/||, NON/NOT/!. Arithmétique : + - * / %.
// Fonctions : FM(x), FD(x), MIN(a,b), MAX(a,b), ABS(a). Durées : 200ms, 5s, 2min, 1h.
//
// Le même arbre syntaxique est évalué par le simulateur (evaluate) et traduit en C++ (toCpp),
// ce qui garantit le même comportement.

export class ExprError extends Error {
  constructor(message, pos = 0) {
    super(message);
    this.pos = pos;
  }
}

const KEYWORDS = {
  ET: 'and',
  AND: 'and',
  OU: 'or',
  OR: 'or',
  NON: 'not',
  NOT: 'not',
  VRAI: 'true',
  TRUE: 'true',
  FAUX: 'false',
  FALSE: 'false',
};
const FUNCS = ['FM', 'FD', 'MIN', 'MAX', 'ABS'];
const UNITS = { ms: 1, s: 1000, min: 60000, h: 3600000 };

export const SERVO_PROPS = {
  pret: { type: 'bool', label: 'servo prêt (alimenté, sans alarme)', cpp: 'servoIsReady()' },
  enPosition: { type: 'bool', label: 'dernier déplacement terminé', cpp: 'servoInPosition()' },
  enMouvement: { type: 'bool', label: 'déplacement en cours', cpp: 'servoIsMoving()' },
  origineFaite: { type: 'bool', label: 'prise d’origine faite', cpp: 'servoHomeDone()' },
  alarme: { type: 'bool', label: 'alarme active', cpp: 'servoHasAlarm()' },
  position: { type: 'int', label: 'position actuelle (impulsions)', cpp: 'servoPosition()' },
};

// ---------------------------------------------------------------------------
// Analyse lexicale
export function tokenize(src) {
  const tokens = [];
  let i = 0;
  const s = String(src ?? '');
  while (i < s.length) {
    const c = s[i];
    if (/\s/.test(c)) {
      i++;
      continue;
    }
    const start = i;
    // t/X3/5s
    const tm = /^t\/X(\d+)\/(\d+(?:\.\d+)?)(ms|s|min|h)?/i.exec(s.slice(i));
    if (tm) {
      tokens.push({ t: 'tstep', step: Number(tm[1]), ms: Number(tm[2]) * UNITS[(tm[3] || 's').toLowerCase()], pos: start, len: tm[0].length });
      i += tm[0].length;
      continue;
    }
    if (/[0-9]/.test(c)) {
      const m = /^(\d+(?:\.\d+)?)(ms|s|min|h)?(?![A-Za-z_0-9])/.exec(s.slice(i));
      if (!m) throw new ExprError(`Nombre invalide près de « ${s.slice(i, i + 8)} »`, i);
      const isFloat = m[1].includes('.');
      if (m[2]) tokens.push({ t: 'num', v: Math.round(Number(m[1]) * UNITS[m[2]]), float: false, unit: m[2], pos: start });
      else tokens.push({ t: 'num', v: Number(m[1]), float: isFloat, pos: start });
      i += m[0].length;
      continue;
    }
    if (c === '"') {
      let j = i + 1;
      let v = '';
      while (j < s.length && s[j] !== '"') {
        if (s[j] === '\\' && j + 1 < s.length) j++;
        v += s[j++];
      }
      if (s[j] !== '"') throw new ExprError('Texte non terminé (guillemet manquant)', i);
      tokens.push({ t: 'str', v, pos: start });
      i = j + 1;
      continue;
    }
    if (/[A-Za-z_À-ÿ]/.test(c)) {
      const m = /^[A-Za-z_À-ÿ][A-Za-z_0-9À-ÿ]*/.exec(s.slice(i));
      const word = m[0];
      i += word.length;
      const kw = KEYWORDS[word.toUpperCase()];
      if (kw) tokens.push({ t: kw, pos: start });
      else tokens.push({ t: 'id', v: word, pos: start });
      continue;
    }
    const two = s.slice(i, i + 2);
    if (['&&', '||', '<=', '>=', '<>', '!=', '=='].includes(two)) {
      tokens.push({ t: { '&&': 'and', '||': 'or' }[two] || 'op', v: two, pos: start });
      i += 2;
      continue;
    }
    if (c === '!') {
      tokens.push({ t: 'not', pos: start });
      i++;
      continue;
    }
    if (c === '↑' || c === '↓') {
      tokens.push({ t: 'edge', rising: c === '↑', pos: start });
      i++;
      continue;
    }
    if ('()+-*/%<>=,.'.includes(c)) {
      tokens.push({ t: c === '(' || c === ')' || c === ',' || c === '.' ? c : 'op', v: c, pos: start });
      i++;
      continue;
    }
    throw new ExprError(`Caractère inattendu « ${c} »`, i);
  }
  tokens.push({ t: 'eof', pos: s.length });
  return tokens;
}

// ---------------------------------------------------------------------------
// Analyse syntaxique (descente récursive) + typage
//
// ctx.resolve(name) -> { type: 'bool'|'int'|'float'|'text', symbol } ou null
// ctx.steps        -> Set des numéros d'étape existants (optionnel)
// ctx.servo        -> true si un servo est présent
export function parse(src, ctx = {}) {
  const tokens = tokenize(src);
  let k = 0;
  const peek = () => tokens[k];
  const next = () => tokens[k++];
  const expect = (t, msg) => {
    if (peek().t !== t) throw new ExprError(msg, peek().pos);
    return next();
  };

  function orExpr() {
    let a = andExpr();
    while (peek().t === 'or') {
      const tok = next();
      const b = andExpr();
      a = { k: 'or', a: needBool(a, tok), b: needBool(b, tok), type: 'bool' };
    }
    return a;
  }
  function andExpr() {
    let a = notExpr();
    while (peek().t === 'and') {
      const tok = next();
      const b = notExpr();
      a = { k: 'and', a: needBool(a, tok), b: needBool(b, tok), type: 'bool' };
    }
    return a;
  }
  function notExpr() {
    if (peek().t === 'not') {
      const tok = next();
      return { k: 'not', a: needBool(notExpr(), tok), type: 'bool' };
    }
    return cmpExpr();
  }
  function cmpExpr() {
    const a = addExpr();
    const tok = peek();
    if (tok.t === 'op' && ['=', '==', '!=', '<>', '<', '<=', '>', '>='].includes(tok.v)) {
      next();
      const b = addExpr();
      const op = { '=': '==', '==': '==', '<>': '!=', '!=': '!=' }[tok.v] || tok.v;
      if (a.type === 'text' || b.type === 'text') {
        if (a.type !== 'text' || b.type !== 'text') throw new ExprError('Comparaison entre un texte et une valeur non textuelle', tok.pos);
        if (op !== '==' && op !== '!=') throw new ExprError('Un texte ne se compare qu’avec = ou <>', tok.pos);
        return { k: 'cmp', op, a, b, type: 'bool', text: true };
      }
      if (a.type === 'bool' || b.type === 'bool') {
        if (a.type !== b.type || (op !== '==' && op !== '!=')) throw new ExprError('Un booléen ne se compare qu’à un booléen, avec = ou <>', tok.pos);
      }
      return { k: 'cmp', op, a, b, type: 'bool' };
    }
    return a;
  }
  function addExpr() {
    let a = mulExpr();
    while (peek().t === 'op' && (peek().v === '+' || peek().v === '-')) {
      const tok = next();
      const b = mulExpr();
      a = arith(tok, a, b);
    }
    return a;
  }
  function mulExpr() {
    let a = unary();
    while (peek().t === 'op' && ['*', '/', '%'].includes(peek().v)) {
      const tok = next();
      const b = unary();
      a = arith(tok, a, b);
    }
    return a;
  }
  function unary() {
    if (peek().t === 'op' && peek().v === '-') {
      const tok = next();
      const a = needNum(unary(), tok);
      return { k: 'neg', a, type: a.type };
    }
    if (peek().t === 'edge') {
      const tok = next();
      return edge(tok.rising, primary(), tok);
    }
    return primary();
  }
  function primary() {
    const tok = next();
    switch (tok.t) {
      case 'num':
        return { k: 'num', v: tok.v, type: tok.float ? 'float' : 'int' };
      case 'str':
        return { k: 'str', v: tok.v, type: 'text' };
      case 'true':
        return { k: 'bool', v: true, type: 'bool' };
      case 'false':
        return { k: 'bool', v: false, type: 'bool' };
      case 'tstep':
        checkStep(tok.step, tok);
        return { k: 'cmp', op: '>=', a: { k: 'steptime', step: tok.step, type: 'int' }, b: { k: 'num', v: tok.ms, type: 'int' }, type: 'bool' };
      case '(': {
        const e = orExpr();
        expect(')', 'Parenthèse fermante « ) » attendue');
        return e;
      }
      case 'id':
        return identifier(tok);
      case 'eof':
        throw new ExprError('Expression incomplète', tok.pos);
      default:
        throw new ExprError(`Élément inattendu « ${tok.v ?? tok.t} »`, tok.pos);
    }
  }
  function identifier(tok) {
    const name = tok.v;
    const upper = name.toUpperCase();
    if (FUNCS.includes(upper) && peek().t === '(') {
      next();
      const args = [];
      if (peek().t !== ')') {
        args.push(orExpr());
        while (peek().t === ',') {
          next();
          args.push(orExpr());
        }
      }
      expect(')', `Parenthèse fermante attendue après les arguments de ${upper}`);
      if (upper === 'FM' || upper === 'FD') {
        if (args.length !== 1) throw new ExprError(`${upper} attend un seul argument`, tok.pos);
        return edge(upper === 'FM', args[0], tok);
      }
      if (upper === 'ABS') {
        if (args.length !== 1) throw new ExprError('ABS attend un seul argument', tok.pos);
        return { k: 'call', fn: 'ABS', args: [needNum(args[0], tok)], type: args[0].type };
      }
      if (args.length !== 2) throw new ExprError(`${upper} attend deux arguments`, tok.pos);
      args.forEach((a) => needNum(a, tok));
      return { k: 'call', fn: upper, args, type: args.some((a) => a.type === 'float') ? 'float' : 'int' };
    }
    // Étape : X12, X12.t
    const sm = /^X(\d+)$/i.exec(name);
    if (sm) {
      const step = Number(sm[1]);
      checkStep(step, tok);
      if (peek().t === '.') {
        next();
        const prop = expect('id', 'Propriété attendue après « . » (X3.t)');
        if (prop.v !== 't') throw new ExprError(`Propriété d’étape inconnue « ${prop.v} » (seule « t » existe)`, prop.pos);
        return { k: 'steptime', step, type: 'int' };
      }
      return { k: 'step', step, type: 'bool' };
    }
    if (name === 'Servo' || upper === 'SERVO') {
      if (!ctx.servo) throw new ExprError('Aucun servo dans le projet', tok.pos);
      expect('.', 'Propriété attendue après « Servo » (ex. Servo.enPosition)');
      const prop = expect('id', 'Propriété du servo attendue');
      const def = SERVO_PROPS[prop.v];
      if (!def) throw new ExprError(`Propriété du servo inconnue « ${prop.v} ». Disponibles : ${Object.keys(SERVO_PROPS).join(', ')}`, prop.pos);
      return { k: 'servo', prop: prop.v, type: def.type };
    }
    const v = ctx.resolve ? ctx.resolve(name) : null;
    if (!v) throw new ExprError(`Variable inconnue « ${name} »`, tok.pos);
    return { k: 'var', sym: v.symbol, type: v.type === 'float' ? 'float' : v.type === 'int' ? 'int' : v.type };
  }
  function edge(rising, a, tok) {
    if (a.type !== 'bool' || !['var', 'step', 'servo'].includes(a.k)) {
      throw new ExprError(`${rising ? 'FM' : 'FD'} s’applique à une variable ou une étape booléenne`, tok.pos);
    }
    return { k: 'edge', rising, a, type: 'bool' };
  }
  function checkStep(step, tok) {
    if (ctx.steps && !ctx.steps.has(step)) throw new ExprError(`L’étape ${step} n’existe pas`, tok.pos);
  }
  function needBool(e, tok) {
    if (e.type !== 'bool') throw new ExprError(`Opérande booléen attendu (${typeLabel(e.type)} trouvé)`, tok.pos);
    return e;
  }
  function needNum(e, tok) {
    if (e.type !== 'int' && e.type !== 'float') throw new ExprError(`Valeur numérique attendue (${typeLabel(e.type)} trouvé)`, tok.pos);
    return e;
  }
  function arith(tok, a, b) {
    needNum(a, tok);
    needNum(b, tok);
    if (tok.v === '%' && (a.type === 'float' || b.type === 'float')) throw new ExprError('Le modulo % ne s’applique qu’à des entiers', tok.pos);
    return { k: 'arith', op: tok.v, a, b, type: a.type === 'float' || b.type === 'float' ? 'float' : 'int' };
  }

  if (peek().t === 'eof') throw new ExprError('Expression vide', 0);
  const ast = orExpr();
  if (peek().t !== 'eof') throw new ExprError(`Élément en trop « ${tokens[k].v ?? tokens[k].t} »`, peek().pos);
  return ast;
}

export function typeLabel(t) {
  return { bool: 'booléen', int: 'entier', float: 'réel', text: 'texte' }[t] || t;
}

// Parcours de l'arbre.
export function walk(ast, fn) {
  if (!ast) return;
  fn(ast);
  for (const key of ['a', 'b']) if (ast[key]) walk(ast[key], fn);
  if (ast.args) ast.args.forEach((x) => walk(x, fn));
}

export function usedSymbols(ast) {
  const out = new Set();
  walk(ast, (n) => n.k === 'var' && out.add(n.sym));
  return out;
}

// Clé d'identification d'un opérande de front (même opérande -> même mémoire).
export function edgeKey(a) {
  if (a.k === 'var') return `var:${a.sym}`;
  if (a.k === 'step') return `step:${a.step}`;
  if (a.k === 'servo') return `servo:${a.prop}`;
  return null;
}

// ---------------------------------------------------------------------------
// Évaluation (simulateur). env : { get(sym), step(n), stepTime(n), servo(prop), prev(key) }
export function evaluate(ast, env) {
  switch (ast.k) {
    case 'num':
    case 'str':
    case 'bool':
      return ast.v;
    case 'var':
      return env.get(ast.sym);
    case 'step':
      return env.step(ast.step);
    case 'steptime':
      return env.stepTime(ast.step);
    case 'servo':
      return env.servo(ast.prop);
    case 'not':
      return !evaluate(ast.a, env);
    case 'and':
      return !!evaluate(ast.a, env) && !!evaluate(ast.b, env);
    case 'or':
      return !!evaluate(ast.a, env) || !!evaluate(ast.b, env);
    case 'neg':
      return -evaluate(ast.a, env);
    case 'edge': {
      const cur = !!evaluate(ast.a, env);
      const prev = !!env.prev(edgeKey(ast.a));
      return ast.rising ? cur && !prev : !cur && prev;
    }
    case 'cmp': {
      const a = evaluate(ast.a, env);
      const b = evaluate(ast.b, env);
      switch (ast.op) {
        case '==':
          return a === b;
        case '!=':
          return a !== b;
        case '<':
          return a < b;
        case '<=':
          return a <= b;
        case '>':
          return a > b;
        case '>=':
          return a >= b;
      }
      return false;
    }
    case 'arith': {
      const a = evaluate(ast.a, env);
      const b = evaluate(ast.b, env);
      const isInt = ast.type === 'int';
      let r;
      switch (ast.op) {
        case '+':
          r = a + b;
          break;
        case '-':
          r = a - b;
          break;
        case '*':
          r = a * b;
          break;
        case '/':
          r = b === 0 ? 0 : a / b;
          break;
        case '%':
          r = b === 0 ? 0 : a % b;
          break;
      }
      return isInt ? Math.trunc(r) | 0 : Math.fround(r);
    }
    case 'call': {
      const args = ast.args.map((x) => evaluate(x, env));
      if (ast.fn === 'MIN') return Math.min(...args);
      if (ast.fn === 'MAX') return Math.max(...args);
      if (ast.fn === 'ABS') return Math.abs(args[0]);
      return 0;
    }
  }
  throw new Error(`Nœud inconnu ${ast.k}`);
}

// ---------------------------------------------------------------------------
// Traduction en C++. names : { var(sym), step(n), stepTime(n), servo(prop), prev(key) }
export function toCpp(ast, names) {
  const c = (x) => toCpp(x, names);
  switch (ast.k) {
    case 'num':
      return ast.type === 'float' ? `${Number.isInteger(ast.v) ? ast.v + '.0' : ast.v}f` : String(ast.v);
    case 'str':
      return JSON.stringify(ast.v);
    case 'bool':
      return ast.v ? 'true' : 'false';
    case 'var':
      return names.var(ast.sym);
    case 'step':
      return names.step(ast.step);
    case 'steptime':
      return names.stepTime(ast.step);
    case 'servo':
      return names.servo(ast.prop);
    case 'not':
      return `!${c(ast.a)}`;
    case 'and':
      return `(${c(ast.a)} && ${c(ast.b)})`;
    case 'or':
      return `(${c(ast.a)} || ${c(ast.b)})`;
    case 'neg':
      return `(-${c(ast.a)})`;
    case 'edge': {
      const cur = c(ast.a);
      const prev = names.prev(edgeKey(ast.a));
      return ast.rising ? `(${cur} && !${prev})` : `(!${cur} && ${prev})`;
    }
    case 'cmp':
      if (ast.text) return `(strcmp(${c(ast.a)}, ${c(ast.b)}) ${ast.op} 0)`;
      return `(${c(ast.a)} ${ast.op} ${c(ast.b)})`;
    case 'arith':
      if (ast.op === '/') return ast.type === 'int' ? `plcDivI(${c(ast.a)}, ${c(ast.b)})` : `plcDivF(${c(ast.a)}, ${c(ast.b)})`;
      if (ast.op === '%') return `plcMod(${c(ast.a)}, ${c(ast.b)})`;
      return `(${c(ast.a)} ${ast.op} ${c(ast.b)})`;
    case 'call': {
      const args = ast.args.map(c);
      const t = ast.type === 'float' ? 'F' : 'I';
      if (ast.fn === 'ABS') return `plcAbs${t}(${args[0]})`;
      return `plc${ast.fn === 'MIN' ? 'Min' : 'Max'}${t}(${args.join(', ')})`;
    }
  }
  throw new Error(`Nœud inconnu ${ast.k}`);
}

// Contexte de résolution standard à partir du projet.
export function projectContext(project, { steps, servo } = {}) {
  const bySymbol = new Map(project.variables.map((v) => [v.symbol, v]));
  return {
    servo: servo ?? project.equipment.some((e) => e.type === 'SureServo'),
    steps,
    resolve(name) {
      const v = bySymbol.get(name);
      if (!v) return null;
      return { symbol: v.symbol, type: v.dataType, variable: v };
    },
  };
}

// Analyse sans exception : { ast } ou { error, pos }.
export function tryParse(src, ctx) {
  try {
    return { ast: parse(src, ctx) };
  } catch (e) {
    if (e instanceof ExprError) return { error: e.message, pos: e.pos };
    throw e;
  }
}

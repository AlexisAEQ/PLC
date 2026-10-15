// Lecteur JSONC du référentiel d'infrastructure (infrastructure.jsonc).
//
// - commentaires // et /* */, virgules finales tolérées ;
// - clés en double REFUSÉES (la première serait ignorée en silence par JSON.parse) ;
// - clés __proto__, constructor et prototype refusées ;
// - numéro de ligne de chaque clé et de chaque élément conservé (POSITIONS), pour que les
//   messages du chargeur citent « infrastructure.jsonc:<ligne> ».
// Aucune dépendance : fonctionne dans Node et dans le navigateur. shared/jsonc.js reste le
// lecteur des fichiers data/hardware (format plus permissif, sans positions).

export const POSITIONS = new WeakMap(); // objet|tableau -> { ligne, cles: { cle: ligne } } | { ligne, lignes: [ligne] }

const CLES_INTERDITES = new Set(['__proto__', 'constructor', 'prototype']);

export class ErreurLecture extends Error {
  constructor(message, ligne, colonne) {
    super(`ligne ${ligne}, colonne ${colonne} : ${message}`);
    this.texte = message;
    this.ligne = ligne;
    this.colonne = colonne;
  }
}

export function lireJsonc(texte) {
  texte = String(texte).replace(/^﻿/, ''); // readFileSync garde le BOM, fetch le retire
  let i = 0;
  let ligne = 1;
  let debutLigne = 0;
  const col = () => i - debutLigne + 1;
  const erreur = (msg) => {
    throw new ErreurLecture(msg, ligne, col());
  };
  const avance = () => {
    if (texte[i] === '\n') {
      ligne++;
      debutLigne = i + 1;
    }
    i++;
  };
  const blancs = () => {
    for (;;) {
      const c = texte[i];
      if (c === ' ' || c === '\t' || c === '\r' || c === '\n') avance();
      else if (c === '/' && texte[i + 1] === '/') while (i < texte.length && texte[i] !== '\n') avance();
      else if (c === '/' && texte[i + 1] === '*') {
        const l0 = ligne;
        const c0 = col();
        avance();
        avance();
        while (i < texte.length && !(texte[i] === '*' && texte[i + 1] === '/')) avance();
        if (i >= texte.length) throw new ErreurLecture('commentaire /* jamais fermé par */', l0, c0);
        avance();
        avance();
      } else return;
    }
  };
  const chaine = () => {
    const l0 = ligne;
    const c0 = col();
    const debut = i;
    avance(); // "
    while (i < texte.length && texte[i] !== '"') {
      if (texte[i] === '\n') throw new ErreurLecture('guillemet fermant manquant (la chaîne continue sur la ligne suivante)', l0, c0);
      if (texte.charCodeAt(i) < 0x20) erreur('caractère de contrôle (tabulation…) dans un texte : écrire \\t ou une espace');
      if (texte[i] === '\\') avance();
      avance();
    }
    if (i >= texte.length) throw new ErreurLecture('guillemet fermant manquant', l0, c0);
    avance();
    try {
      return JSON.parse(texte.slice(debut, i));
    } catch {
      throw new ErreurLecture('séquence d’échappement invalide dans la chaîne (\\n, \\", \\\\, \\uXXXX)', l0, c0);
    }
  };
  const NOMBRE = /-?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?(?![\w.])|true(?!\w)|false(?!\w)|null(?!\w)/y;
  const valeur = () => {
    blancs();
    const c = texte[i];
    if (i >= texte.length) erreur('fin du fichier inattendue : valeur manquante, ou accolade / crochet jamais fermé');
    if (c === '{') return objet();
    if (c === '[') return tableau();
    if (c === '"') return chaine();
    if (c === "'") erreur('les textes s’écrivent entre guillemets doubles "…", pas entre apostrophes');
    if (/^-?0\d/.test(texte.slice(i, i + 3))) erreur('nombre avec un zéro en tête : écrire 127 (ou "0127" entre guillemets pour un texte)');
    NOMBRE.lastIndex = i;
    const m = NOMBRE.exec(texte);
    if (!m) {
      const mot = /^[^\s,:\]}]+/.exec(texte.slice(i, i + 40))?.[0] || (i < texte.length ? texte[i] : 'fin du fichier');
      erreur(
        `valeur inattendue « ${mot} »` +
          (/^(vrai|faux|oui|non|True|False|TRUE|FALSE)$/.test(mot) ? ' (écrire true ou false)' : /^[A-Za-z_]/.test(mot) ? ' (texte sans guillemets ?)' : '')
      );
    }
    for (let k = 0; k < m[0].length; k++) avance();
    const v = JSON.parse(m[0]);
    if (typeof v === 'number' && !Number.isFinite(v)) erreur(`nombre hors limites « ${m[0]} »`);
    return v;
  };
  const objet = () => {
    const pos = { ligne, cles: {} };
    const o = {};
    avance(); // {
    for (;;) {
      blancs();
      if (texte[i] === '}') {
        avance();
        break;
      }
      if (texte[i] !== '"') {
        erreur(i >= texte.length ? 'accolade fermante « } » manquante' : texte[i] === "'" ? 'les clés s’écrivent entre guillemets doubles "…"' : `nom de clé entre guillemets attendu, trouvé « ${texte[i]} »`);
      }
      const lCle = ligne;
      const cle = chaine();
      if (CLES_INTERDITES.has(cle)) throw new ErreurLecture(`clé « ${cle} » interdite`, lCle, 1);
      if (Object.prototype.hasOwnProperty.call(pos.cles, cle)) {
        throw new ErreurLecture(`clé « ${cle} » en double (déjà définie ligne ${pos.cles[cle]}) : la première serait ignorée en silence`, lCle, 1);
      }
      blancs();
      if (texte[i] !== ':') erreur(`« : » attendu après la clé « ${cle} »`);
      avance();
      pos.cles[cle] = lCle;
      o[cle] = valeur();
      blancs();
      if (texte[i] === ',') {
        avance();
        continue;
      }
      if (texte[i] === '}') {
        avance();
        break;
      }
      erreur(texte[i] === '"' ? `virgule manquante après la valeur de « ${cle} »` : i >= texte.length ? 'accolade fermante « } » manquante' : `« , » ou « } » attendu après la valeur de « ${cle} »`);
    }
    POSITIONS.set(o, pos);
    return o;
  };
  const tableau = () => {
    const pos = { ligne, lignes: [] };
    const t = [];
    avance(); // [
    for (;;) {
      blancs();
      if (texte[i] === ']') {
        avance();
        break;
      }
      if (i >= texte.length) erreur('crochet fermant « ] » manquant');
      pos.lignes.push(ligne);
      t.push(valeur());
      blancs();
      if (texte[i] === ',') {
        avance();
        continue;
      }
      if (texte[i] === ']') {
        avance();
        break;
      }
      erreur(i >= texte.length ? 'crochet fermant « ] » manquant' : '« , » ou « ] » attendu dans la liste (virgule manquante ?)');
    }
    POSITIONS.set(t, pos);
    return t;
  };
  const v = valeur();
  blancs();
  if (i < texte.length) erreur('texte en trop après la fin du document (accolade fermante en trop ?)');
  return v;
}

// Ligne d'une clé ou d'un élément : ligneDe(parent, cle|index) ; sans clé, ligne du parent.
export function ligneDe(parent, cle) {
  const p = parent && typeof parent === 'object' ? POSITIONS.get(parent) : null;
  if (!p) return null;
  if (cle === undefined) return p.ligne;
  if (p.cles && Object.prototype.hasOwnProperty.call(p.cles, cle)) return p.cles[cle];
  if (p.lignes && p.lignes[cle] !== undefined) return p.lignes[cle];
  return p.ligne;
}

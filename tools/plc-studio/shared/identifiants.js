// Identifiants « gelés » du catalogue : ceux que les projets enregistrent (clé d'équipement,
// clé de voie, clé d'option, clé de signal) ou qui entrent dans le hash des nœuds (suffixe de
// section, ids et noms des nœuds). Les renommer dans infrastructure.jsonc casserait les projets
// existants ou changerait les identifiants de interface.json et du C++.
//
// test/snapshots/identifiants.json en garde une copie (plc infra figer) ; le test
// referentiel.test.js compare le catalogue actuel à cette copie avec verifierIdentifiants().
// Les ajouts sont toujours acceptés ; un retrait ou un renommage exige un alias (section
// « alias » du référentiel) ou, pour un identifiant haché, une décision explicite (figer).

export function identifiantsDuCatalogue(catalog) {
  const out = {};
  for (const [type, e] of Object.entries(catalog)) {
    out[type] = {
      groupes: Object.fromEntries(
        e.groups.map((g) => [g.key, { suffixe: g.short, prefixe: g.prefix, ...(g.bank ? { bits: g.bank.bits } : { ids: g.ids }), ...(g.names ? { noms: g.names } : {}) }])
      ),
      options: (e.options || []).map((o) => o.key),
      signaux: (e.signals || []).map((s) => s.key),
      sections: Object.fromEntries((e.sections || []).map((s) => [s.suffix, s.nodes.map((n) => `${n.id}:${n.name}:${n.field}`)])),
    };
  }
  return out;
}

// Compare le catalogue actuel à la copie figée. Renvoie la liste des problèmes (texte).
export function verifierIdentifiants(fige, catalog, alias = {}) {
  const actuel = identifiantsDuCatalogue(catalog);
  const pbs = [];
  const aliasT = alias.types || {};
  for (const [type, f] of Object.entries(fige)) {
    const nouveauType = actuel[type] ? type : aliasT[type];
    const a = actuel[nouveauType];
    if (!a) {
      pbs.push(`type « ${type} » retiré : les projets qui l'utilisent ne s'ouvriraient plus. Ajoutez alias.types.${type} vers son remplaçant.`);
      continue;
    }
    const aliasG = alias.groupes?.[nouveauType] || {};
    for (const [cle, g] of Object.entries(f.groupes)) {
      const ga = a.groupes[cle] || a.groupes[aliasG[cle]];
      if (!ga) {
        pbs.push(`${type} : voie « ${cle} » retirée (câblages des projets perdus). Ajoutez alias.groupes.${nouveauType}.${cle}.`);
        continue;
      }
      if (ga.suffixe !== g.suffixe) pbs.push(`${type} : suffixe de section de la voie « ${cle} » changé (« ${g.suffixe} » → « ${ga.suffixe} ») : hash des nœuds changés, réinstaller les projets puis « plc infra figer ».`);
      for (const id of g.ids || []) if (!(ga.ids || []).includes(id)) pbs.push(`${type} : id ${id} retiré de la voie « ${cle} » (câblages des projets perdus).`);
      if (g.bits && (!ga.bits || ga.bits < g.bits)) pbs.push(`${type} : banc de la voie « ${cle} » réduit (${g.bits} → ${ga.bits || 0} bits).`);
    }
    const aliasO = alias.options?.[nouveauType] || {};
    for (const k of f.options) if (!a.options.includes(k) && !a.options.includes(aliasO[k])) pbs.push(`${type} : option « ${k} » retirée (réglage des projets perdu). Ajoutez alias.options.${nouveauType}.${k}.`);
    for (const k of f.signaux) if (!a.signaux.includes(k)) pbs.push(`${type} : signal « ${k} » retiré (câblage des projets perdu).`);
    for (const [suffixe, noeuds] of Object.entries(f.sections)) {
      const na = a.sections[suffixe];
      if (!na) {
        pbs.push(`${type} : section « ${suffixe} » retirée ou renommée : hash des nœuds changés, réinstaller les projets puis « plc infra figer ».`);
        continue;
      }
      for (const n of noeuds) if (!na.includes(n)) pbs.push(`${type} : nœud « ${n} » (id:nom:champ) de la section « ${suffixe} » changé ou retiré : hash ou câblage changés.`);
    }
  }
  return pbs;
}

// Texte du fichier figé : JSON indenté, listes de valeurs simples sur une ligne.
export function texteIdentifiants(catalog) {
  return JSON.stringify(identifiantsDuCatalogue(catalog), null, 1).replace(/\[\s*([^[\]{}]*?)\s*\]/g, (_, inner) => `[${inner.split(/,\s*/).join(', ')}]`) + '\n';
}

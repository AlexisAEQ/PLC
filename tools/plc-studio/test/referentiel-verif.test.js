import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { REFERENTIEL } from '../shared/catalog.js';
import { chargerReferentiel } from '../shared/referentiel.js';
import { ligneDe } from '../shared/jsonc-pos.js';
import { parseJsonc } from '../shared/jsonc.js';
import { verifierReferentiel, definitionsCpp, lireIni, sansCommentaires, defautsParValeur } from '../shared/referentiel-verif.js';

const RACINE = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..', '..');

// Fichiers du dépôt, modifiés en mémoire : modifs[rel](texte) et listes[rel](noms).
function fichiers({ modifs = {}, listes = {} } = {}) {
  const ou = (f, rel) => {
    try {
      return f(path.join(RACINE, rel));
    } catch {
      return null;
    }
  };
  return {
    lire: (rel) => (modifs[rel] || ((t) => t))(ou((p) => readFileSync(p, 'utf8'), rel)),
    lister: (rel) => (listes[rel] || ((l) => l))(ou(readdirSync, rel)),
  };
}
const verifier = (opts, ref = REFERENTIEL) => verifierReferentiel(ref, fichiers(opts));
const erreurs = (r) => r.constats.filter((c) => c.niveau === 'erreur');
const attentions = (r) => r.constats.filter((c) => c.niveau === 'attention');
const lister = (cs) => cs.map((c) => `${c.niveau} ${c.code} ${c.chemin} : ${c.message}`).join('\n');

// Remplacement qui échoue si le texte d'origine a changé (le test ne doit pas passer à vide).
const remplacer = (avant, apres) => (t) => {
  assert.ok(typeof avant === 'string' ? t.includes(avant) : avant.test(t), `motif ${avant} absent du fichier`);
  return t.replace(avant, apres);
};
// Modification d'un fichier matériel JSONC par objet.
const materiel = (f) => (t) => {
  const h = parseJsonc(t);
  f(h);
  return JSON.stringify(h, null, 2);
};

// Le dépôt réel : aucune erreur, et les avertissements sont exactement les écarts connus déclarés
// (un écart qui apparaît sans être déclaré, ou un écart déclaré qui disparaît, fait échouer).
test('référentiel : le dépôt est conforme, seuls les écarts connus déclarés restent (cliquet)', () => {
  const r = verifier();
  assert.deepEqual(erreurs(r), [], lister(erreurs(r)));
  const declares = Object.entries(REFERENTIEL.ecartsConnus).filter(([, x]) => x.controle != null).map(([id]) => id).sort();
  assert.deepEqual([...new Set(attentions(r).map((c) => c.ecart))].sort(), declares, lister(attentions(r)));
  assert.ok(attentions(r).every((c) => c.message.startsWith(`[écart connu ${c.ecart}] `)), lister(attentions(r)));
  assert.ok(r.constats.some((c) => c.code === 'S1'));
});

// Les tests suivants partent de la copie figée du référentiel (test/donnees) : ils ne dépendent
// pas des écarts présents aujourd'hui dans le dépôt.
const ESSAI = chargerReferentiel(readFileSync(path.join(RACINE, 'tools/plc-studio/test/donnees/infrastructure-essai.jsonc'), 'utf8'));
const A8S = 'data/hardware/Kincony_KC868_A8S.json';
const vitesseA8S = (bauds) => materiel((h) => (h.RS485.speed = bauds));

test('écarts connus : constat accepté seulement s’il correspond au motif ; écart corrigé signalé', () => {
  const r1 = verifier({ modifs: { [A8S]: vitesseA8S(115000) } }, ESSAI);
  const e07 = attentions(r1).find((c) => c.ecart === 'E07');
  assert.ok(e07 && e07.chemin === 'automates.Kincony_KC868_A8S.rs485.vitesse', lister(r1.constats));
  // Autre vitesse hors des choix au même endroit : pas l'écart E07, donc une erreur.
  const r2 = verifier({ modifs: { [A8S]: vitesseA8S(9601) } }, ESSAI);
  assert.ok(erreurs(r2).some((c) => c.code === 'H7' && c.chemin === 'automates.Kincony_KC868_A8S.rs485.vitesse'), lister(r2.constats));
  // Vitesse corrigée : l'écart demande à être retiré.
  const r3 = verifier({ modifs: { [A8S]: vitesseA8S(115200) } }, ESSAI);
  assert.match(attentions(r3).find((c) => c.chemin === 'ecartsConnus.E07')?.message || '', /E07 corrigé \? .*retirez-le de ecartsConnus/);
  // Écart retiré du référentiel : redevient une erreur.
  const sansE07 = chargerReferentiel(readFileSync(path.join(RACINE, 'tools/plc-studio/test/donnees/infrastructure-essai.jsonc'), 'utf8').replace(/^\s*"E07": .*\n.*\n/m, ''));
  assert.deepEqual(sansE07.erreurs, []);
  assert.ok(erreurs(verifier({ modifs: { [A8S]: vitesseA8S(115000) } }, sansE07)).some((c) => c.code === 'H7'));
});

test('écarts connus : un autre problème de partitions sous E14 reste une erreur', () => {
  const r = verifier({ modifs: { 'platformio.ini': remplacer('board_build.partitions = partitions_16MB.csv', 'board_build.partitions = partitions_XYZ.csv') } }, ESSAI);
  assert.ok(erreurs(r).some((c) => c.code === 'P2' && c.chemin === 'compilation.envs.esp32dev.partitions'), lister(r.constats));
});

test('écarts connus : le plus précis l’emporte quand deux écarts couvrent un constat', () => {
  const texte = readFileSync(path.join(RACINE, 'tools/plc-studio/test/donnees/infrastructure-essai.jsonc'), 'utf8').replace('"ecartsConnus": {', '"ecartsConnus": {\n    "E01": { "controle": "axes", "texte": "large" },');
  const ref = chargerReferentiel(texte);
  assert.deepEqual(ref.erreurs, []);
  const r = verifier({}, ref);
  assert.ok(attentions(r).some((c) => c.ecart === 'E02' && c.chemin === 'axes.LichuanA5.vitesseMax'), lister(r.constats));
  assert.ok(!attentions(r).some((c) => c.chemin === 'ecartsConnus.E02'), 'E02 ne doit pas être déclaré corrigé');
});

// Chaque modification de la vérité du firmware doit produire l'erreur indiquée.
const POE = 'data/hardware/Waveshare_ESP32S3_POE_8DI8DO.json';
const MUTATIONS = [
  { nom: 'broche GPIO déplacée', modifs: { [POE]: remplacer('{"id": 1, "pin": 40}', '{"id": 1, "pin": 41}') }, code: 'H4', chemin: 'automates.Waveshare_ESP32S3_POE_8DI8DO.voies[2].broches', motif: /GPIO40 .*broche 41/ },
  {
    nom: 'nb_values d’un banc changé',
    modifs: { 'data/hardware/Waveshare_8DIO.json': materiel((h) => (h.ModbusReadMultipleInputsStatus[0].nb_values = 4)) },
    code: 'H3',
    chemin: 'modules.Waveshare_8DIO.voies[0].banc',
    motif: /banc de 8 bits.*"nb_values" vaut 4/,
  },
  { nom: 'NAME_LENGHT changé dans Node.h', modifs: { 'src/Node/Node.h': remplacer(/(#define NAME_LENGHT\s+)80/, '$190') }, code: 'F2', chemin: 'firmware.limites.octetsNomNoeud', motif: /src\/Node\/Node\.h : NAME_LENGHT = 90 - 2 = 88/ },
  { nom: 'champ retiré de ServoNodes', modifs: { 'src/SureServo/SureServo.h': remplacer('Uint16OutputNode* maxTorque = nullptr;', '') }, code: 'F5', chemin: 'pilotes.sureservo.sections[1].noeuds[3]', motif: /« maxTorque » absent de struct ServoNodes/ },
  { nom: 'signal retiré de ServoNodes', modifs: { 'src/SureServo/SureServo.h': remplacer('BooleanOutputNode* immediateStop = nullptr;', '') }, code: 'F5', chemin: 'pilotes.sureservo.signaux[1]' },
  { nom: 'champ par modèle retiré de LichuanNodes', modifs: { 'src/LichuanServo/LichuanServo.h': remplacer(/Uint16OutputNode\* jogSpeed\s+= nullptr;/, '') }, code: 'F5', chemin: 'axes.LichuanA5.noeuds.registres ecrits[3]' },
  {
    nom: 'maxPeakCurrent changé dans StepperOnlineRS.cpp',
    modifs: { 'src/StepperOnlineRS/StepperOnlineRS.cpp': remplacer('o.maxPeakCurrent = 82;', 'o.maxPeakCurrent = 80;') },
    code: 'F6',
    chemin: 'axes.StepperOnlineDM_RS.modelesVariateur.DM882RS.courantCreteMax',
    motif: /DM882RS.*= 82 ici, maxPeakCurrent = 80 dans le firmware/,
  },
  { nom: 'maxAccelMs changé (branche else de defaultsFor)', modifs: { 'src/LichuanServo/LichuanServo.cpp': remplacer('o.maxAccelMs = 2500;', 'o.maxAccelMs = 3000;') }, code: 'F6', chemin: 'axes.LichuanA6.rampeMaxMs' },
  {
    nom: 'environnement retiré de platformio.ini',
    modifs: { 'platformio.ini': (t) => t.slice(0, t.indexOf('[env:quatre_mb_huge]')) + t.slice(t.indexOf('[env:esp32dev]')) },
    code: 'P1',
    chemin: 'compilation.envs.quatre_mb_huge',
  },
  { nom: 'carte changée (héritée par extends)', modifs: { 'platformio.ini': remplacer('board = esp32-s3-devkitc-1', 'board = esp32-s3-devkitm-1') }, code: 'P1', chemin: 'compilation.envs.m5stack_stamplc.carte' },
  {
    nom: 'casse d’un CSV de partitions',
    listes: { '.': (l) => l.map((n) => (n === 'partitions_4MB_no_ota.csv' ? 'partitions_4mb_no_ota.csv' : n)) },
    code: 'P2',
    chemin: 'compilation.envs.quatre_mb_huge.partitions',
    motif: /s'appelle partitions_4mb_no_ota\.csv/,
  },
  { nom: '"hardware" faux', modifs: { 'data/hardware/Waveshare_8AI.json': materiel((h) => (h.hardware = 'Waveshare_8AIx')) }, code: 'H1', chemin: 'modules.Waveshare_8AI.fichierMateriel' },
  { nom: 'id absent du fichier matériel', modifs: { 'data/hardware/Waveshare_8AI.json': materiel((h) => h.ModbusReadInputRegister.pop()) }, code: 'H2', chemin: 'modules.Waveshare_8AI.voies[0]', motif: /id 8 absent/ },
  { nom: 'broche du buzzer', modifs: { [POE]: materiel((h) => (h.board.buzzer.pin = 45)) }, code: 'H5', chemin: 'automates.Waveshare_ESP32S3_POE_8DI8DO.voies[3].broche' },
  { nom: 'bloc ETH retiré', modifs: { 'data/hardware/Kincony_KC868_A8v3.json': materiel((h) => delete h.ETH) }, code: 'H6', chemin: 'automates.Kincony_KC868_A8v3.ethernet' },
  { nom: 'puce d’horloge différente', modifs: { 'data/hardware/M5Stack_StamPLC.json': materiel((h) => (h.I2C.RTC = 'PCF8563')) }, code: 'H8', chemin: 'automates.M5Stack_StamPLC.horloge' },
  { nom: 'horloge non auto-détectée en "auto"', modifs: { 'data/hardware/Homemaster_MiniPLC.json': materiel((h) => (h.I2C.RTC = 'auto')) }, code: 'H8', chemin: 'automates.Homemaster_MiniPLC.horloge', motif: /PCF8563 n'est pas auto-détectée/ },
  { nom: 'version du firmware', modifs: { 'src/BorneUniverselle/borneUniverselle.h': remplacer('"Borne Universelle 2.2.2"', '"Borne Universelle 2.3.0"') }, code: 'F1', chemin: 'firmware.version' },
  { nom: 'type de section renommé', modifs: { 'src/BorneUniverselle/borneUniverselle.h': remplacer('"EXP_tx-bool"', '"EXP_tx-boolean"') }, code: 'F3', chemin: 'firmware.typesSection.EXP_tx-bool' },
  { nom: 'puce d’extension retirée', modifs: { 'src/IoExpander/IoExpander.cpp': remplacer('if (!strcasecmp(chip, "AW9523")) return new Aw9523Expander(addr);', '') }, code: 'F4', chemin: 'firmware.pucesExtension', motif: /AW9523/ },
  { nom: 'famille renommée dans l’enum', modifs: { 'src/LichuanServo/LichuanServo.h': remplacer(/\bA5\s*=\s*1/, 'A5X = 1') }, code: 'F5', chemin: 'pilotes.lichuan.familles' },
  // Échec fermé : un symbole ou un fichier introuvable est une erreur, jamais un contrôle sauté.
  { nom: 'symbole de limite introuvable', modifs: { 'src/Node/Node.h': remplacer(/#define MAX_TEXT_SIZE\b/, '#define MAX_TEXT_LEN') }, code: 'F2', chemin: 'firmware.limites.octetsTexte', motif: /MAX_TEXT_SIZE introuvable dans src\/Node\/Node\.h/ },
  { nom: 'fichier de limite introuvable', modifs: { 'src/MyModbus/MyModbus.h': () => null }, code: 'F2', chemin: 'firmware.limites.esclavesModbusSuivis', motif: /src\/MyModbus\/MyModbus\.h introuvable/ },
  { nom: 'type d’un champ changé dans ServoNodes', modifs: { 'src/SureServo/SureServo.h': remplacer('Uint16OutputNode* maxTorque = nullptr;', 'Uint32OutputNode* maxTorque = nullptr;') }, code: 'F5', chemin: 'pilotes.sureservo.sections[1].noeuds[3]', motif: /déclaré Uint32OutputNode\*/ },
  { nom: 'vitesse de jog bornée par le firmware', modifs: { 'src/LichuanServo/LichuanServo.h': remplacer(/uint16_t maxSpeedRpm\s*=\s*3000;/, 'uint16_t maxSpeedRpm = 2000;') }, code: 'F6', chemin: 'axes.LichuanA6.vitesseJogMax' },
  { nom: 'borne non littérale dans defaultsFor', modifs: { 'src/LichuanServo/LichuanServo.cpp': remplacer('o.maxAccelMs = 2500;', 'o.maxAccelMs = A6_MAX_ACCEL;') }, code: 'F6', chemin: 'axes.LichuanA6.rampeMaxMs', motif: /pas un entier littéral/ },
  { nom: 'affectation après la chaîne if/else', modifs: { 'src/LichuanServo/LichuanServo.cpp': remplacer(/return o;/, 'o.maxAccelMs = 1000; return o;') }, code: 'F6', chemin: 'axes.LichuanA6.rampeMaxMs', motif: /maxAccelMs = 1000/ },
  { nom: 'condition sans l’enum dans defaultsFor', modifs: { 'src/LichuanServo/LichuanServo.cpp': remplacer(/return o;/, 'if (o.maxAccelMs > 60000) o.maxAccelMs = 60000; return o;') }, code: 'F6', chemin: 'pilotes.lichuan.verite', motif: /structure non reconnue/ },
  { nom: 'en-tête inclus par le C++ généré introuvable', ref: 'entete', code: 'F5', chemin: 'pilotes.sureservo.entete' },
];

// Référentiel d'essai dont l'« entete » du pilote SureServo est faux.
const ENTETE_FAUX = chargerReferentiel(readFileSync(path.join(RACINE, 'tools/plc-studio/test/donnees/infrastructure-essai.jsonc'), 'utf8').replace('"entete": "SureServo/SureServo.h"', '"entete": "SureServo/SureServ.h"'));

for (const m of MUTATIONS) {
  test(`référentiel : ${m.nom} -> erreur ${m.code}`, () => {
    const r = verifier(m, m.ref === 'entete' ? ENTETE_FAUX : ESSAI);
    const e = erreurs(r).find((c) => c.code === m.code && c.chemin === m.chemin);
    assert.ok(e, `erreur ${m.code} ${m.chemin} attendue ; constats :\n${lister(r.constats)}`);
    if (m.motif) assert.match(e.message, m.motif);
    assert.ok(Number.isInteger(e.ligne), 'ligne du référentiel donnée');
  });
}

test('référentiel : lecture C++ et INI (commentaires, constexpr, extends, defaultsFor en switch)', () => {
  const cpp = sansCommentaires('// #define A 1\n#define B 0x10 // seize\n/* #define C 3 */\nstatic constexpr uint8_t D = 7;\nconst char* s = "// pas un commentaire";\n');
  assert.deepEqual(definitionsCpp(cpp, 'A'), []);
  assert.deepEqual(definitionsCpp(cpp, 'B'), [{ texte: '0x10', valeur: 16 }]);
  assert.deepEqual(definitionsCpp(cpp, 'C'), []);
  assert.deepEqual(definitionsCpp(cpp, 'D'), [{ texte: '7', valeur: 7 }]);
  assert.match(cpp, /"\/\/ pas un commentaire"/);

  const ini = lireIni('[platformio]\ndefault_envs = a\n\n[env:a]\nboard = x ; commentaire\nbuild_flags =\n\t-DUN\n\t-DDEUX\n[env:b]\nextends = env:a\n');
  assert.equal(ini['env:a'].board, 'x');
  assert.equal(ini['env:a'].build_flags, '\n-DUN\n-DDEUX');
  assert.equal(ini['env:b'].extends, 'env:a');

  const corps = 'O o; o.y = 1; switch (m) { case E::A: case E::B: o.x = 5; break; case E::C: o.x = 6; break; default: o.x = 9; } return o;';
  const d = defautsParValeur(corps, 'E', ['A', 'B', 'C', 'D']);
  assert.deepEqual([d.A.x, d.B.x, d.C.x, d.D.x, d.D.y, d[''].x], [5, 5, 6, 9, 1, undefined]);
  assert.equal(defautsParValeur('if (flag) { o.x = 1; }', 'E', ['A']), null, 'condition sans l’enum : structure non reconnue');
});

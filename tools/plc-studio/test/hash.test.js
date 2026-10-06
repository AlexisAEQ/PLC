import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { nodeHash, crc32 } from '../shared/hash.js';
import { parseJsonc } from '../shared/jsonc.js';

const repo = fileURLToPath(new URL('../../../', import.meta.url));

test('crc32 standard (zlib)', () => {
  assert.equal(crc32(new TextEncoder().encode('123456789')), 0xcbf43926);
});

test('hash = crc32(id + nom de section), exemples du firmware', () => {
  assert.equal(nodeHash(1, 'Direct outputs'), 1711197005); // BUZZER
  assert.equal(nodeHash(1, 'Inputs Kincony'), 721063196); // ESTOPA
  assert.equal(nodeHash(2, 'Textes'), 1218981442); // STATES_MACHINE
  assert.equal(nodeHash(11, 'Virtual bool inputs'), 59448638); // V_SET_PLC_CLOCK
});

test('tous les hash de data/config.json sont retrouvés', () => {
  const cfg = parseJsonc(readFileSync(repo + 'data/config.json', 'utf8'));
  let n = 0;
  for (const section of cfg.children) {
    for (const child of section.children || []) {
      assert.equal(nodeHash(child.id, section.name), child.hash, `${section.name}/${child.name}`);
      n++;
    }
  }
  assert.ok(n > 50);
});

test("tous les #define de RessortRoyal2.h correspondent à un nœud", () => {
  const cfg = parseJsonc(readFileSync(repo + 'data/config.json', 'utf8'));
  const hashes = new Set(cfg.children.flatMap((s) => (s.children || []).map((c) => c.hash)));
  const header = readFileSync(repo + 'src/RessortRoyal2/RessortRoyal2.h', 'utf8');
  const defs = [...header.matchAll(/#define\s+\w+\s+(\d{6,10})\b/g)].map((m) => Number(m[1]));
  assert.ok(defs.length > 40);
  for (const h of defs) assert.ok(hashes.has(h), `hash ${h}`);
});

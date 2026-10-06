// Calcul des identifiants (hash) de nœuds, identique au firmware.
//
// Firmware (src/Node/Node.cpp) :
//   sprintf(text, "%d%s", id, parentName);       // parentName = nom de la section
//   hash = crc32_le(...)                         // CRC-32 IEEE standard (zlib)
//
// L'id est converti en uint8_t par le parseur (borneUniverselle.cpp), d'où le & 0xFF.

const TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

const encoder = new TextEncoder();

export function crc32(bytes) {
  let c = 0xffffffff;
  for (const b of bytes) c = TABLE[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

export function nodeHash(id, sectionName) {
  return crc32(encoder.encode(String(id & 0xff) + sectionName));
}

export function utf8Length(text) {
  return encoder.encode(String(text ?? '')).length;
}

// Tronque une chaîne pour qu'elle tienne dans maxBytes octets UTF-8 sans couper un caractère.
export function truncateUtf8(text, maxBytes) {
  let out = '';
  for (const ch of String(text ?? '')) {
    if (utf8Length(out + ch) > maxBytes) break;
    out += ch;
  }
  return out;
}

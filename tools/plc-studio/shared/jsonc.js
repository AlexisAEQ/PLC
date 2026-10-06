// Lecture de fichiers JSON avec commentaires (// et /* */), comme ArduinoJson avec
// ARDUINOJSON_ENABLE_COMMENTS. Les chaînes sont respectées.

export function stripJsonComments(text) {
  let out = '';
  let i = 0;
  let inString = false;
  while (i < text.length) {
    const c = text[i];
    const n = text[i + 1];
    if (inString) {
      out += c;
      if (c === '\\') {
        out += n ?? '';
        i += 2;
        continue;
      }
      if (c === '"') inString = false;
      i++;
      continue;
    }
    if (c === '"') {
      inString = true;
      out += c;
      i++;
    } else if (c === '/' && n === '/') {
      while (i < text.length && text[i] !== '\n') i++;
    } else if (c === '/' && n === '*') {
      i += 2;
      while (i < text.length && !(text[i] === '*' && text[i + 1] === '/')) i++;
      i += 2;
    } else {
      out += c;
      i++;
    }
  }
  return out;
}

export function parseJsonc(text) {
  return JSON.parse(stripJsonComments(text).replace(/,(\s*[}\]])/g, '$1'));
}

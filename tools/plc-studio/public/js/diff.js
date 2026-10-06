// Diff ligne à ligne (plus longue sous-séquence commune), regroupé en blocs avec contexte.

export function lineDiff(oldText, newText, context = 3) {
  const a = oldText.split('\n');
  const b = newText.split('\n');
  const n = a.length;
  const m = b.length;
  if (n * m > 4_000_000) return [{ type: 'hunk', text: 'Fichier trop volumineux pour afficher les différences.' }];

  // Table LCS (de la fin vers le début).
  const dp = Array.from({ length: n + 1 }, () => new Uint32Array(m + 1));
  for (let i = n - 1; i >= 0; i--) {
    for (let j = m - 1; j >= 0; j--) {
      dp[i][j] = a[i] === b[j] ? dp[i + 1][j + 1] + 1 : Math.max(dp[i + 1][j], dp[i][j + 1]);
    }
  }
  const ops = [];
  let i = 0;
  let j = 0;
  while (i < n && j < m) {
    if (a[i] === b[j]) {
      ops.push({ type: 'same', text: a[i], ln: j + 1 });
      i++;
      j++;
    } else if (dp[i + 1][j] >= dp[i][j + 1]) {
      ops.push({ type: 'del', text: a[i++] });
    } else {
      ops.push({ type: 'add', text: b[j], ln: j + 1 });
      j++;
    }
  }
  while (i < n) ops.push({ type: 'del', text: a[i++] });
  while (j < m) ops.push({ type: 'add', text: b[j], ln: ++j });

  // Ne garder que les changements et leur contexte.
  const keep = new Array(ops.length).fill(false);
  ops.forEach((op, k) => {
    if (op.type !== 'same') for (let x = Math.max(0, k - context); x <= Math.min(ops.length - 1, k + context); x++) keep[x] = true;
  });
  const out = [];
  let skipping = false;
  ops.forEach((op, k) => {
    if (keep[k]) {
      skipping = false;
      out.push(op);
    } else if (!skipping) {
      skipping = true;
      out.push({ type: 'hunk', text: '…' });
    }
  });
  return out;
}

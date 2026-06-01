// AiLang symbol indexer — pure (no vscode dependency) so it can be unit-tested
// in plain node. Regex-based, not a real parser: it finds definitions
// (functions, externs, structs, enums, enum variants, bindings, params, loop
// vars, import aliases) with their source position, a display signature, and
// any contiguous leading `//` / `/* */` comment block.
//
// Used by the Definition and Hover providers in extension.js.

const IDENT = '[A-Za-z_][A-Za-z0-9_]*';

// Split `s` on `sep` at bracket-depth 0, returning [{ text, start }] where
// start is the offset of each chunk within `s`. Handles () [] {} nesting so a
// param like `f:fn(a,b)->c` or `m:{str:i64}` isn't split mid-type.
function splitTopLevel(s, sep) {
  const out = [];
  let depth = 0;
  let chunkStart = 0;
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (c === '(' || c === '[' || c === '{') depth++;
    else if (c === ')' || c === ']' || c === '}') depth--;
    else if (c === sep && depth === 0) {
      out.push({ text: s.slice(chunkStart, i), start: chunkStart });
      chunkStart = i + 1;
    }
  }
  out.push({ text: s.slice(chunkStart), start: chunkStart });
  return out;
}

// Build an offset → { line, character } mapper for `text`.
function makeLineMap(text) {
  const starts = [0];
  for (let i = 0; i < text.length; i++) {
    if (text[i] === '\n') starts.push(i + 1);
  }
  return function (offset) {
    let lo = 0;
    let hi = starts.length - 1;
    while (lo < hi) {
      const mid = (lo + hi + 1) >> 1;
      if (starts[mid] <= offset) lo = mid;
      else hi = mid - 1;
    }
    return { line: lo, character: offset - starts[lo] };
  };
}

// Collect the contiguous comment block immediately above `lineIdx` (no blank
// line in between). Returns cleaned text, or ''.
function leadingDoc(lines, lineIdx) {
  const collected = [];
  let i = lineIdx - 1;

  // One-line block comment directly above: `/* ... */`
  // or a multi-line block ending on the line above.
  while (i >= 0) {
    const raw = lines[i];
    const t = raw.trim();
    if (t === '') break;
    if (t.startsWith('//')) {
      collected.unshift(t.replace(/^\/\/\s?/, ''));
      i--;
      continue;
    }
    if (t.endsWith('*/')) {
      // Walk up to the line that opens the block.
      const block = [];
      let j = i;
      while (j >= 0) {
        block.unshift(lines[j]);
        if (lines[j].trim().startsWith('/*') || lines[j].includes('/*')) break;
        j--;
      }
      const cleaned = block
        .join('\n')
        .replace(/\/\*+/, '')
        .replace(/\*+\//, '')
        .split('\n')
        .map((l) => l.replace(/^\s*\*?\s?/, '').trimEnd())
        .join('\n')
        .trim();
      if (cleaned) collected.unshift(cleaned);
      break;
    }
    break;
  }

  return collected.join('\n').trim();
}

function pushSym(symbols, lineMap, lines, name, kind, offset, signature, doc) {
  const pos = lineMap(offset);
  symbols.push({
    name,
    kind,
    line: pos.line,
    character: pos.character,
    endCharacter: pos.character + name.length,
    signature: signature.trim(),
    doc: doc !== undefined ? doc : leadingDoc(lines, pos.line),
  });
}

// Scan an `en Name { ... }` body starting at the brace after `headerOffset`,
// returning [{ name, offset }] for each top-level variant.
function scanEnumVariants(text, headerOffset) {
  let i = text.indexOf('{', headerOffset);
  if (i < 0) return [];
  const variants = [];
  let depth = 0;
  let paren = 0;
  let expectVariant = false;
  for (; i < text.length; i++) {
    const c = text[i];
    if (c === '{') {
      depth++;
      if (depth === 1) expectVariant = true;
      continue;
    }
    if (c === '}') {
      depth--;
      if (depth === 0) break;
      continue;
    }
    if (depth !== 1) continue;
    if (c === '(') {
      paren++;
      continue;
    }
    if (c === ')') {
      paren--;
      continue;
    }
    if (paren > 0) continue;
    if (c === ',') {
      expectVariant = true;
      continue;
    }
    if (expectVariant && /[A-Za-z_]/.test(c)) {
      let j = i;
      while (j < text.length && /[A-Za-z0-9_]/.test(text[j])) j++;
      variants.push({ name: text.slice(i, j), offset: i });
      expectVariant = false;
      i = j - 1;
    }
  }
  return variants;
}

/**
 * Index AiLang source text.
 * @returns {{ symbols: Array, imports: Array<{spec:string, line:number}> }}
 */
function indexSource(text) {
  const lines = text.split('\n');
  const lineMap = makeLineMap(text);
  const symbols = [];
  const imports = [];
  let m;

  // Imports + aliases.
  const reImport = new RegExp(`^[ \\t]*im[ \\t]+"([^"]+)"(?:[ \\t]+as[ \\t]+(${IDENT}))?`, 'gm');
  while ((m = reImport.exec(text)) !== null) {
    imports.push({ spec: m[1], line: lineMap(m.index).line });
    if (m[2]) {
      const off = m.index + m[0].lastIndexOf(m[2]);
      pushSym(symbols, lineMap, lines, m[2], 'import', off, m[0].trim());
    }
  }

  // Functions and externs (single-line signatures, AiLang's normal form).
  const reFn = new RegExp(
    `^[ \\t]*(ex[ \\t]+(?:"[^"]*"[ \\t]+)?)?fn[ \\t]+(${IDENT})[ \\t]*(<[^>]*>)?[ \\t]*\\(([^)]*)\\)([^\\n{]*)`,
    'gm'
  );
  while ((m = reFn.exec(text)) !== null) {
    const isExtern = !!m[1];
    const name = m[2];
    const fnRel = m[0].indexOf('fn', (m[1] || '').length);
    const nameRel = m[0].indexOf(name, fnRel + 2);
    const nameOff = m.index + nameRel;
    const signature = m[0]
      .slice(fnRel)
      .replace(/\/\/.*$/, '') // drop a trailing line comment
      .replace(/\s+/g, ' ')
      .trim();
    pushSym(symbols, lineMap, lines, name, isExtern ? 'extern' : 'function', nameOff, signature);

    // Parameters.
    const parenRel = m[0].indexOf('(', nameRel);
    const paramsStr = m[4];
    const paramsBase = m.index + parenRel + 1;
    for (const part of splitTopLevel(paramsStr, ',')) {
      const pm = new RegExp(`^[ \\t]*(?:mu[ \\t]+)?(${IDENT})`).exec(part.text);
      if (pm) {
        const off = paramsBase + part.start + pm[0].lastIndexOf(pm[1]);
        pushSym(symbols, lineMap, lines, pm[1], 'param', off, `(parameter) ${part.text.trim()}`, '');
      }
    }
  }

  // Structs and enums.
  const reType = new RegExp(`^[ \\t]*(st|en)[ \\t]+(${IDENT})`, 'gm');
  while ((m = reType.exec(text)) !== null) {
    const kind = m[1] === 'st' ? 'struct' : 'enum';
    const name = m[2];
    const off = m.index + m[0].lastIndexOf(name);
    pushSym(symbols, lineMap, lines, name, kind, off, m[0].trim());
    if (kind === 'enum') {
      const headerLine = lineMap(off).line;
      for (const v of scanEnumVariants(text, off)) {
        pushSym(symbols, lineMap, lines, v.name, 'variant', v.offset, `${v.name} (variant of ${name})`, leadingDoc(lines, headerLine));
      }
    }
  }

  // Variable bindings (`x := …`, `mu x := …`, also after `;` on the same line).
  const reBind = new RegExp(`(?:^|;)[ \\t]*(?:mu[ \\t]+)?(${IDENT})[ \\t]*(?::[ \\t]*[^=;\\n]+?)?:=`, 'gm');
  while ((m = reBind.exec(text)) !== null) {
    const name = m[1];
    const off = m.index + m[0].indexOf(name);
    pushSym(symbols, lineMap, lines, name, 'variable', off, m[0].replace(/^[;\s]+/, '').trim());
  }

  // Loop bindings: `lp x in …` and `lp (k, v) in …`.
  const reLp1 = new RegExp(`^[ \\t]*lp[ \\t]+(${IDENT})[ \\t]+in\\b`, 'gm');
  while ((m = reLp1.exec(text)) !== null) {
    const off = m.index + m[0].lastIndexOf(m[1]);
    pushSym(symbols, lineMap, lines, m[1], 'variable', off, `(loop binding) ${m[1]}`, '');
  }
  const reLp2 = new RegExp(`^[ \\t]*lp[ \\t]+\\([ \\t]*(${IDENT})[ \\t]*,[ \\t]*(${IDENT})[ \\t]*\\)[ \\t]+in\\b`, 'gm');
  while ((m = reLp2.exec(text)) !== null) {
    for (const nm of [m[1], m[2]]) {
      const off = m.index + m[0].indexOf(nm, m[0].indexOf('('));
      pushSym(symbols, lineMap, lines, nm, 'variable', off, `(loop binding) ${nm}`, '');
    }
  }

  return { symbols, imports };
}

const DEF_KINDS = new Set(['function', 'extern', 'struct', 'enum', 'variant', 'import']);

/**
 * Resolve `name` used at `usageLine` against an index.
 * Top-level defs (fn/st/en/variant/extern/import) win regardless of position;
 * locals (variable/param) resolve to the closest binding at or above the use.
 */
function resolve(index, name, usageLine) {
  const cands = index.symbols.filter((s) => s.name === name);
  if (cands.length === 0) return null;

  const defs = cands.filter((s) => DEF_KINDS.has(s.kind));
  if (defs.length) return defs[0];

  const above = cands.filter((s) => s.line <= usageLine).sort((a, b) => b.line - a.line);
  if (above.length) return above[0];
  return cands.sort((a, b) => a.line - b.line)[0];
}

module.exports = { indexSource, resolve, leadingDoc, splitTopLevel, makeLineMap, DEF_KINDS };

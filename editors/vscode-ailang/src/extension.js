// Thin diagnostic provider: shell out to `ailangc compile --emit-c` on save,
// parse ariadne's plain-text diagnostics, and publish them as VS Code
// Diagnostics so the editor shows red/yellow squiggles.

const vscode = require('vscode');
const cp = require('child_process');
const path = require('path');
const fs = require('fs');
const { KEYWORDS, CONSTANTS, TYPES, BUILTINS } = require('./vocabulary');
const { indexSource, resolve, DEF_KINDS } = require('./symbols');

let diagnosticCollection;
let outputChannel;

function activate(context) {
  diagnosticCollection = vscode.languages.createDiagnosticCollection('ailang');
  outputChannel = vscode.window.createOutputChannel('AiLang');
  context.subscriptions.push(diagnosticCollection, outputChannel);

  registerCompletion(context);
  registerNavigation(context);

  const checkIfAil = (doc) => {
    if (doc && doc.languageId === 'ailang' && doc.uri.scheme === 'file') {
      checkDocument(doc);
    }
  };

  context.subscriptions.push(
    vscode.workspace.onDidSaveTextDocument(checkIfAil),
    vscode.workspace.onDidOpenTextDocument(checkIfAil),
    vscode.workspace.onDidCloseTextDocument((doc) => {
      diagnosticCollection.delete(doc.uri);
    })
  );

  // Already-open editors when the extension activates.
  for (const editor of vscode.window.visibleTextEditors) {
    checkIfAil(editor.document);
  }
}

function deactivate() {
  if (diagnosticCollection) diagnosticCollection.dispose();
  if (outputChannel) outputChannel.dispose();
}

// ---------------------------------------------------------------------------
// Completion: keywords, primitive types, constants, and always-in-scope
// builtins (with signatures). User-defined names still complete via VS Code's
// built-in word-based suggestions, which run alongside this provider.
// ---------------------------------------------------------------------------

let cachedItems = null;

function buildCompletionItems() {
  if (cachedItems) return cachedItems;
  const K = vscode.CompletionItemKind;
  const items = [];

  for (const [kw, desc] of KEYWORDS) {
    const it = new vscode.CompletionItem(kw, K.Keyword);
    it.detail = `keyword — ${desc}`;
    items.push(it);
  }

  for (const c of CONSTANTS) {
    const it = new vscode.CompletionItem(c, K.Constant);
    it.detail = 'constant';
    items.push(it);
  }

  for (const t of TYPES) {
    const it = new vscode.CompletionItem(t, K.TypeParameter);
    it.detail = 'primitive type';
    items.push(it);
  }

  for (const b of BUILTINS) {
    const it = new vscode.CompletionItem(b.name, K.Function);
    it.detail = `${b.name}${b.sig}`;
    const docParts = [];
    if (b.doc) docParts.push(b.doc);
    if (b.needsImport) docParts.push(`Requires \`im "${b.needsImport}"\`.`);
    if (docParts.length) it.documentation = new vscode.MarkdownString(docParts.join('\n\n'));
    // Insert `name(<cursor>)` so the call parens are pre-typed; zero-arg
    // builtins get `name()` with the cursor after.
    it.insertText = new vscode.SnippetString(b.noArgs ? `${b.name}()$0` : `${b.name}($0)`);
    items.push(it);
  }

  cachedItems = items;
  return items;
}

function registerCompletion(context) {
  const provider = {
    provideCompletionItems(document, position) {
      // Don't offer the vocabulary inside comments or plain string bodies
      // (but DO offer inside ${...} interpolation — handled by leaving the
      // common case alone; suppressing only obvious comment/string lines keeps
      // this cheap without a full token scan).
      const line = document.lineAt(position.line).text;
      const before = line.slice(0, position.character);
      if (/\/\//.test(before)) {
        // crude: if there's a // before the cursor and we're not past a string,
        // assume comment. Good enough for a keyword list.
        const lastSlash = before.lastIndexOf('//');
        const inString = (before.slice(0, lastSlash).match(/"/g) || []).length % 2 === 1;
        if (!inString) return undefined;
      }
      return buildCompletionItems();
    },
  };
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider('ailang', provider)
  );
}

// ---------------------------------------------------------------------------
// Navigation: Go to Definition (Cmd/Ctrl+click) and Hover.
// Backed by the regex symbol indexer in symbols.js. Resolves names in the
// current file first, then follows `im "..."` imports one level for
// cross-module jumps (e.g. clicking `min` jumps into std/math.ail).
// ---------------------------------------------------------------------------

const BUILTIN_MAP = new Map(BUILTINS.map((b) => [b.name, b]));
const KEYWORD_MAP = new Map(KEYWORDS);
const TYPE_SET = new Set(TYPES);
const CONSTANT_SET = new Set(CONSTANTS);

const KIND_LABELS = {
  function: 'function',
  extern: 'extern function (C ABI)',
  struct: 'struct',
  enum: 'enum',
  variant: 'enum variant',
  variable: 'variable',
  param: 'parameter',
  import: 'import alias',
};

// uri string -> { version, index }
const docIndexCache = new Map();
// file path -> { mtimeMs, index }
const fileIndexCache = new Map();

function indexDocument(document) {
  const key = document.uri.toString();
  const cached = docIndexCache.get(key);
  if (cached && cached.version === document.version) return cached.index;
  const index = indexSource(document.getText());
  docIndexCache.set(key, { version: document.version, index });
  return index;
}

function indexFile(filePath) {
  try {
    const stat = fs.statSync(filePath);
    const cached = fileIndexCache.get(filePath);
    if (cached && cached.mtimeMs === stat.mtimeMs) return cached.index;
    const index = indexSource(fs.readFileSync(filePath, 'utf8'));
    fileIndexCache.set(filePath, { mtimeMs: stat.mtimeMs, index });
    return index;
  } catch (e) {
    return null;
  }
}

// Resolve an `im "spec"` payload to a file on disk: relative to the importing
// file first, then to each workspace folder (mirrors the driver's lookup).
function resolveImportPath(spec, fromDocPath) {
  const candidates = [path.resolve(path.dirname(fromDocPath), spec)];
  for (const folder of vscode.workspace.workspaceFolders || []) {
    candidates.push(path.resolve(folder.uri.fsPath, spec));
  }
  return candidates.find((c) => fs.existsSync(c)) || null;
}

// Returns { sym, uri, word, range, source } — sym/uri null if no definition
// found (word/range still populated so hover can fall back to builtins).
function findSymbol(document, position) {
  const range = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
  if (!range) return null;
  const word = document.getText(range);

  const index = indexDocument(document);
  const local = resolve(index, word, position.line);
  if (local) return { sym: local, uri: document.uri, word, range, source: null };

  for (const imp of index.imports) {
    const p = resolveImportPath(imp.spec, document.uri.fsPath);
    if (!p) continue;
    const fi = indexFile(p);
    if (!fi) continue;
    const hit = fi.symbols.find((s) => s.name === word && DEF_KINDS.has(s.kind));
    if (hit) return { sym: hit, uri: vscode.Uri.file(p), word, range, source: imp.spec };
  }
  return { sym: null, uri: null, word, range, source: null };
}

function registerNavigation(context) {
  const definitionProvider = {
    provideDefinition(document, position) {
      const r = findSymbol(document, position);
      if (!r || !r.sym) return null;
      return new vscode.Location(r.uri, new vscode.Position(r.sym.line, r.sym.character));
    },
  };

  const hoverProvider = {
    provideHover(document, position) {
      const r = findSymbol(document, position);
      if (!r) return null;

      // A real definition (local or imported).
      if (r.sym) {
        const md = new vscode.MarkdownString();
        md.appendCodeblock(r.sym.signature, 'ailang');
        let meta = `_${KIND_LABELS[r.sym.kind] || r.sym.kind}_`;
        if (r.source) meta += ` · from \`${r.source}\``;
        md.appendMarkdown(meta);
        if (r.sym.doc) md.appendMarkdown('\n\n' + r.sym.doc);
        return new vscode.Hover(md, r.range);
      }

      // Builtin function.
      const b = BUILTIN_MAP.get(r.word);
      if (b) {
        const md = new vscode.MarkdownString();
        md.appendCodeblock(`${r.word}${b.sig}`, 'ailang');
        md.appendMarkdown('_builtin function_');
        if (b.doc) md.appendMarkdown('\n\n' + b.doc);
        if (b.needsImport) md.appendMarkdown(`\n\nRequires \`im "${b.needsImport}"\`.`);
        return new vscode.Hover(md, r.range);
      }

      // Keyword / type / constant.
      if (KEYWORD_MAP.has(r.word)) {
        return new vscode.Hover(
          new vscode.MarkdownString(`\`${r.word}\` — _keyword_: ${KEYWORD_MAP.get(r.word)}`),
          r.range
        );
      }
      if (TYPE_SET.has(r.word)) {
        return new vscode.Hover(new vscode.MarkdownString(`\`${r.word}\` — _primitive type_`), r.range);
      }
      if (CONSTANT_SET.has(r.word)) {
        return new vscode.Hover(new vscode.MarkdownString(`\`${r.word}\` — _constant_`), r.range);
      }
      return null;
    },
  };

  context.subscriptions.push(
    vscode.languages.registerDefinitionProvider('ailang', definitionProvider),
    vscode.languages.registerHoverProvider('ailang', hoverProvider),
    vscode.workspace.onDidCloseTextDocument((doc) => docIndexCache.delete(doc.uri.toString()))
  );
}

function getConfig() {
  const cfg = vscode.workspace.getConfiguration('ailang');
  return {
    enabled: cfg.get('diagnostics.enable', true),
    compilerPath: cfg.get('compilerPath', 'ailangc'),
  };
}

let warnedAboutMissingCompiler = false;

function checkDocument(doc) {
  const { enabled, compilerPath } = getConfig();
  if (!enabled) {
    diagnosticCollection.delete(doc.uri);
    return;
  }

  const filePath = doc.fileName;
  const cwd = path.dirname(filePath);

  cp.execFile(
    compilerPath,
    ['compile', filePath, '--emit-c'],
    { cwd, env: { ...process.env, NO_COLOR: '1' }, maxBuffer: 4 * 1024 * 1024 },
    (err, stdout, stderr) => {
      if (err && err.code === 'ENOENT') {
        if (!warnedAboutMissingCompiler) {
          warnedAboutMissingCompiler = true;
          vscode.window.showWarningMessage(
            `AiLang: "${compilerPath}" not found on PATH. Set "ailang.compilerPath" or disable "ailang.diagnostics.enable" to silence this.`
          );
        }
        diagnosticCollection.delete(doc.uri);
        return;
      }
      const text = stripAnsi((stderr || '') + (stdout || ''));
      const diags = parseDiagnostics(text, filePath, doc);
      diagnosticCollection.set(doc.uri, diags);
      if (diags.length) outputChannel.appendLine(`${filePath}: ${diags.length} diagnostic(s)`);
    }
  );
}

const ANSI_RE = /\x1b\[[0-9;]*m/g;
function stripAnsi(s) {
  return s.replace(ANSI_RE, '');
}

// Matches one ariadne diagnostic: `Error: <msg>` or `Warning: <msg>`
// followed by the `╭─[file:line:col]` location header.
const DIAG_RE = /(Error|Warning):\s*([^\n]+)\n\s*╭─\[([^\]]+):(\d+):(\d+)\]/g;
// One-line summary fallback for lex errors etc. that don't get a positioned report.
const SUMMARY_RE = /^ailangc:\s*([^\n]+error[^\n]*)$/m;
// Library modules (e.g. files under std/) have no top-level code, so compiling
// them standalone trips the compiler's "no main function" check. Filter that
// specific diagnostic so opening a library file doesn't show a spurious error.
// Real errors in the same file still surface — only this one message is dropped.
const NO_MAIN_RE = /no\s+`?main`?\s+function/i;

function parseDiagnostics(output, docPath, doc) {
  const diags = [];
  const docAbs = path.resolve(docPath);
  const docCwd = path.dirname(docAbs);
  let suppressedNoMain = false;
  let m;
  while ((m = DIAG_RE.exec(output)) !== null) {
    const [, severity, message, file, lineStr, colStr] = m;
    // Drop the "no main function" diagnostic — see NO_MAIN_RE comment.
    if (NO_MAIN_RE.test(message)) {
      suppressedNoMain = true;
      continue;
    }
    const reported = path.isAbsolute(file) ? file : path.resolve(docCwd, file);
    // Only attach diagnostics whose file resolves to THIS document.
    // Errors in imported files would otherwise land on the wrong file.
    if (path.resolve(reported) !== docAbs) continue;

    const line = Math.max(0, parseInt(lineStr, 10) - 1);
    const col = Math.max(0, parseInt(colStr, 10) - 1);

    // ariadne points at the first offending character; pull the backtick-quoted
    // identifier out of the message (if any) to size the underline. Otherwise
    // underline to end-of-line.
    let endCol;
    const identMatch = message.match(/`([^`]+)`/);
    if (identMatch) {
      endCol = col + identMatch[1].length;
    } else if (doc && line < doc.lineCount) {
      endCol = doc.lineAt(line).text.length;
    } else {
      endCol = col + 1;
    }

    const range = new vscode.Range(line, col, line, endCol);
    const diag = new vscode.Diagnostic(
      range,
      message.trim(),
      severity === 'Warning'
        ? vscode.DiagnosticSeverity.Warning
        : vscode.DiagnosticSeverity.Error
    );
    diag.source = 'ailangc';
    diags.push(diag);
  }

  // If we got no per-position diagnostics but the compiler reported a summary
  // (e.g. "ailangc: lex error: ..."), pin it to line 0 so something shows up.
  // Only fire the summary fallback if we genuinely have no diagnostics AND
  // didn't suppress a no-main (which would have produced its own summary line
  // we'd otherwise misreport as a line-0 error).
  if (diags.length === 0 && !suppressedNoMain) {
    const sm = output.match(SUMMARY_RE);
    if (sm && !NO_MAIN_RE.test(sm[1])) {
      const range = new vscode.Range(0, 0, 0, 1);
      const diag = new vscode.Diagnostic(
        range,
        sm[1].trim(),
        vscode.DiagnosticSeverity.Error
      );
      diag.source = 'ailangc';
      diags.push(diag);
    }
  }

  return diags;
}

module.exports = { activate, deactivate };

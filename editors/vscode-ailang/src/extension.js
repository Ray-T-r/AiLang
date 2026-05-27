// Thin diagnostic provider: shell out to `ailangc compile --emit-c` on save,
// parse ariadne's plain-text diagnostics, and publish them as VS Code
// Diagnostics so the editor shows red/yellow squiggles.

const vscode = require('vscode');
const cp = require('child_process');
const path = require('path');

let diagnosticCollection;
let outputChannel;

function activate(context) {
  diagnosticCollection = vscode.languages.createDiagnosticCollection('ailang');
  outputChannel = vscode.window.createOutputChannel('AiLang');
  context.subscriptions.push(diagnosticCollection, outputChannel);

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

function parseDiagnostics(output, docPath, doc) {
  const diags = [];
  const docAbs = path.resolve(docPath);
  const docCwd = path.dirname(docAbs);
  let m;
  while ((m = DIAG_RE.exec(output)) !== null) {
    const [, severity, message, file, lineStr, colStr] = m;
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
  if (diags.length === 0) {
    const sm = output.match(SUMMARY_RE);
    if (sm) {
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

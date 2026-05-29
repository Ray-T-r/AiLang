//! AiLang compiler driver — pipeline orchestration.
//!
//! Exposed entry points:
//! - [`dump_tokens`]   — M0 lexer dump
//! - [`dump_ast`]      — M1 parser dump
//! - [`compile`]       — full pipeline: source → executable
//! - [`run`]           — compile, then exec the binary
//!
//! Currently the codegen backend is C transpilation (clang/cc as the
//! actual code generator). LLVM-IR-via-inkwell is the planned long-term
//! backend; swapping is local to `ailang-codegen-llvm`.

use std::collections::{HashMap, HashSet};
use std::fs;
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::OnceLock;

use ailang_codegen_llvm::emit_c;
use ailang_diag::{report, Diagnostic};
use ailang_lexer::lex;
use ailang_parser::parse;
use ailang_sema::analyze;
use ailang_syntax::ast::{
    Block, ElseBranch, Expr, ExprKind, Item, LambdaBody, LoopHead, Module, Stmt,
};
use ailang_syntax::{Token, TokenKind};

// ============================================================================
// Embedded stdlib + auto-import
// ============================================================================
//
// The 10 `std/*.ail` modules ship bundled in the compiler binary via
// `include_str!`. Two consequences:
//
//   1. `im "std/sock.ail"` works whether or not the user has the source
//      tree checked out — installed `ailangc` doesn't need a sibling
//      `std/` directory.
//   2. Auto-import: after walking the user's explicit `im` statements,
//      the driver scans for referenced-but-undefined names. Anything that
//      matches a name exported by a stdlib module gets that module
//      silently imported. So `redis_set(c, "k", "v")` without any `im`
//      statement Just Works — the driver injects `im "std/redis.ail"`
//      before sema runs.
//
// User definitions and explicit imports always win — auto-import only
// fires when the name is genuinely unresolved. Stdlib modules that
// aren't referenced are never parsed, so libpq/libssl/libhiredis stay
// unlinked for hello-world programs.

static STDLIB_FILES: &[(&str, &str)] = &[
    ("std/sock.ail",  include_str!("../../../std/sock.ail")),
    ("std/http.ail",  include_str!("../../../std/http.ail")),
    ("std/redis.ail", include_str!("../../../std/redis.ail")),
    ("std/pg.ail",    include_str!("../../../std/pg.ail")),
    ("std/tls.ail",   include_str!("../../../std/tls.ail")),
    ("std/ws.ail",    include_str!("../../../std/ws.ail")),
    ("std/json.ail",  include_str!("../../../std/json.ail")),
    ("std/math.ail",  include_str!("../../../std/math.ail")),
    ("std/str.ail",   include_str!("../../../std/str.ail")),
    ("std/time.ail",  include_str!("../../../std/time.ail")),
];

fn stdlib_source(path: &str) -> Option<&'static str> {
    STDLIB_FILES.iter().find(|(p, _)| *p == path).map(|(_, s)| *s)
}

/// Exported fn name → owning stdlib path. Built once by parsing every
/// embedded `std/*.ail`. Leaks owned `String`s into `&'static str` so the
/// table can live for the process lifetime — total leakage is ~100 short
/// strings (under 2KB), paid at first compile only.
fn stdlib_symbol_index() -> &'static HashMap<&'static str, &'static str> {
    static INSTANCE: OnceLock<HashMap<&'static str, &'static str>> = OnceLock::new();
    INSTANCE.get_or_init(|| {
        let mut out: HashMap<&'static str, &'static str> = HashMap::new();
        for (path, source) in STDLIB_FILES {
            let tokens = lex(source);
            let (module, _errs) = parse(source, &tokens);
            for item in &module.items {
                let name = match item {
                    Item::Fn(f) => Some(f.name.name.clone()),
                    Item::Extern(e) => Some(e.sig.name.name.clone()),
                    _ => None,
                };
                if let Some(n) = name {
                    let leaked: &'static str = Box::leak(n.into_boxed_str());
                    out.entry(leaked).or_insert(*path);
                }
            }
        }
        out
    })
}

#[derive(thiserror::Error, Debug)]
pub enum DriverError {
    #[error("could not read {path}: {source}")]
    Read {
        path: String,
        #[source]
        source: io::Error,
    },
    #[error("lex error: {count} unrecognized token(s) in {path}")]
    LexErrors { path: String, count: usize },
    #[error("parse error: {count} problem(s) in {path}")]
    ParseErrors { path: String, count: usize },
    #[error("semantic error: {count} problem(s) in {path}")]
    SemaErrors { path: String, count: usize },
    #[error("C compiler failed (exit {status})")]
    CcFailed { status: i32 },
    #[error("could not invoke C compiler `{cc}`: {source}")]
    CcSpawn {
        cc: String,
        #[source]
        source: io::Error,
    },
    #[error("could not run binary {path}: {source}")]
    RunFailed {
        path: String,
        #[source]
        source: io::Error,
    },
}

// ============================================================================
// M0: tokens dump
// ============================================================================

pub fn dump_tokens(path: &Path, out: &mut dyn Write) -> Result<(), DriverError> {
    let source = read_source(path)?;
    let tokens = lex(&source);
    let err_count = tokens.iter().filter(|t| t.kind == TokenKind::Error).count();
    print_tokens(&source, &tokens, out);
    if err_count > 0 {
        return Err(DriverError::LexErrors {
            path: path.display().to_string(),
            count: err_count,
        });
    }
    Ok(())
}

fn print_tokens(source: &str, tokens: &[Token], out: &mut dyn Write) {
    for tok in tokens {
        match tok.kind {
            TokenKind::Eof => {
                let _ = writeln!(out, "{:>5}..{:<5}  {}", tok.span.start, tok.span.end, tok.kind);
            }
            _ => {
                let slice = tok.span.slice(source);
                let _ = writeln!(
                    out,
                    "{:>5}..{:<5}  {:<10}  {}",
                    tok.span.start,
                    tok.span.end,
                    tok.kind,
                    Escape(slice)
                );
            }
        }
    }
}

struct Escape<'a>(&'a str);

impl std::fmt::Display for Escape<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        const MAX: usize = 40;
        let mut count = 0;
        for ch in self.0.chars() {
            if count >= MAX {
                f.write_str("...")?;
                break;
            }
            match ch {
                '\n' => f.write_str("\\n")?,
                '\r' => f.write_str("\\r")?,
                '\t' => f.write_str("\\t")?,
                c => write!(f, "{}", c)?,
            }
            count += 1;
        }
        Ok(())
    }
}

// ============================================================================
// M1: AST dump
// ============================================================================

pub fn dump_ast(path: &Path, out: &mut dyn Write) -> Result<(), DriverError> {
    let source = read_source(path)?;
    let tokens = lex(&source);
    let (module, errors) = parse(&source, &tokens);
    if !errors.is_empty() {
        let diags: Vec<Diagnostic> = errors
            .iter()
            .map(|e| {
                let mut d = Diagnostic::error(e.message.clone(), e.span);
                if let Some(h) = &e.help {
                    d = d.with_help(h.clone());
                }
                d
            })
            .collect();
        report(&path.display().to_string(), &source, &diags);
        return Err(DriverError::ParseErrors {
            path: path.display().to_string(),
            count: errors.len(),
        });
    }
    let _ = writeln!(out, "{module:#?}");
    Ok(())
}

// ============================================================================
// M2: compile / run
// ============================================================================

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Backend {
    /// Default: AST → C99/C11, hand off to clang/cc for codegen.
    C,
    /// M6+: AST → LLVM IR text (.ll), still linked through clang. Only a
    /// small subset (println of literals, implicit main) currently lowered.
    Ir,
}

#[derive(Clone)]
pub struct CompileOptions {
    /// Output binary path; default places it next to the source.
    pub output: Option<PathBuf>,
    /// Emit the generated source (C or LLVM IR depending on backend) and
    /// stop before invoking the C compiler.
    pub emit_c_only: bool,
    /// Pass extra args (e.g. `-lgc`) to the linker. Reserved for M4+.
    pub extra_cc_args: Vec<String>,
    /// Verbose: print every command we run.
    pub verbose: bool,
    /// Which codegen backend to use.
    pub backend: Backend,
}

impl Default for CompileOptions {
    fn default() -> Self {
        Self {
            output: None,
            emit_c_only: false,
            extra_cc_args: Vec::new(),
            verbose: false,
            backend: Backend::C,
        }
    }
}

pub struct CompileResult {
    /// The generated C source.
    pub c_source: String,
    /// Path to the produced binary (`None` if `emit_c_only`).
    pub binary: Option<PathBuf>,
}

pub fn compile(path: &Path, opts: &CompileOptions) -> Result<CompileResult, DriverError> {
    let source = read_source(path)?;
    let path_disp = path.display().to_string();

    // ---- Parse (recursively, following `im "path"` imports) ----
    let mut visited: HashSet<PathBuf> = HashSet::new();
    let mut items: Vec<Item> = Vec::new();
    parse_with_imports(path, &source, &mut items, &mut visited, /*is_root=*/ true)?;

    // ---- Auto-import: pull in stdlib modules whose exported names appear
    //      in user code but haven't been defined or explicitly imported.
    //      Loops to a fixpoint so transitive stdlib deps land too. ----
    auto_import_stdlib(&mut items, &mut visited)?;

    let module = Module { items };

    // ---- Sema ----
    let (resolved, sema_diags) = analyze(module);
    let sema_errs = sema_diags
        .iter()
        .filter(|d| matches!(d.severity, ailang_diag::Severity::Error))
        .count();
    if !sema_diags.is_empty() {
        report(&path_disp, &source, &sema_diags);
    }
    if sema_errs > 0 {
        return Err(DriverError::SemaErrors {
            path: path_disp,
            count: sema_errs,
        });
    }

    // ---- Codegen ----
    let (source_str, ext) = match opts.backend {
        Backend::C => (emit_c(&resolved), "c"),
        Backend::Ir => (ailang_codegen_ir::emit_ir(&resolved), "ll"),
    };

    if opts.emit_c_only {
        return Ok(CompileResult { c_source: source_str, binary: None });
    }

    // ---- Write tmp source and invoke clang ----
    let binary = invoke_c_compiler(path, &source_str, ext, opts)?;
    Ok(CompileResult {
        c_source: source_str,
        binary: Some(binary),
    })
}

pub fn run(path: &Path, opts: &CompileOptions) -> Result<i32, DriverError> {
    let result = compile(path, opts)?;
    let bin = result
        .binary
        .expect("compile() returned no binary when emit_c_only=false");
    let status = Command::new(&bin)
        .status()
        .map_err(|e| DriverError::RunFailed {
            path: bin.display().to_string(),
            source: e,
        })?;
    Ok(status.code().unwrap_or(-1))
}

fn invoke_c_compiler(
    src_path: &Path,
    src_text: &str,
    ext: &str,
    opts: &CompileOptions,
) -> Result<PathBuf, DriverError> {
    // Pick output binary location.
    let bin_path = match &opts.output {
        Some(p) => p.clone(),
        None => {
            let stem = src_path.file_stem().unwrap_or_default();
            let mut p = src_path.with_file_name(stem);
            p.set_extension("");
            p
        }
    };

    // Write generated source (C or LLVM IR) to a temp file next to the
    // binary so the user can inspect it if needed.
    let mut tmp_path = bin_path.clone();
    tmp_path.set_extension(ext);
    fs::write(&tmp_path, src_text).map_err(|e| DriverError::Read {
        path: tmp_path.display().to_string(),
        source: e,
    })?;

    // Pick the C compiler: respect $CC, otherwise prefer clang then cc.
    let cc = std::env::var("CC").unwrap_or_else(|_| "clang".to_string());

    let mut cmd = Command::new(&cc);
    // `clang` treats `.ll` as LLVM IR automatically; `-std=c11` only applies
    // to the C path. Apply C-only flags conditionally.
    if ext == "c" {
        cmd.arg("-std=c11");
    }
    cmd.arg("-O2")
        .arg("-Wno-everything") // silence noise from the generated code
        .arg("-o")
        .arg(&bin_path)
        .arg(&tmp_path);

    // The C backend's prelude uses Boehm GC; the IR backend's initial drop
    // doesn't. Only chase down `-lgc` when we actually need it.
    if ext == "c" {
        // Locate Boehm GC and add the appropriate -I / -L / -lgc flags.
        // Order of probes:
        //   1. $BDW_GC_PREFIX (explicit override)
        //   2. `pkg-config --variable=prefix bdw-gc`
        //   3. `brew --prefix bdw-gc`
        //   4. fall through and hope the system linker finds `libgc`
        let gc_prefix = std::env::var("BDW_GC_PREFIX")
            .ok()
            .or_else(|| pkgconfig_prefix("bdw-gc"))
            .or_else(|| brew_prefix("bdw-gc"));
        if let Some(prefix) = &gc_prefix {
            cmd.arg(format!("-I{prefix}/include"));
            cmd.arg(format!("-L{prefix}/lib"));
        }
        cmd.arg("-lgc");
        // libm is universally available and harmless to link unconditionally;
        // `std/math.ail` declares sqrt/sin/cos/log/exp/... as externs.
        cmd.arg("-lm");

        // libpq: only link if the generated C actually references the
        // Postgres builtins. Probe order mirrors bdw-gc: $PG_PREFIX →
        // pkg-config → brew. If detected, add -I/-L; -lpq is always
        // appended when needed (link error if libpq is missing).
        let c_uses_libpq = std::fs::read_to_string(&tmp_path)
            .map(|s| s.contains("PQconnectdb") || s.contains("PQexec"))
            .unwrap_or(false);
        if c_uses_libpq {
            let pg_prefix = std::env::var("PG_PREFIX")
                .ok()
                .or_else(|| pkgconfig_prefix("libpq"))
                .or_else(|| brew_prefix("libpq"));
            if let Some(prefix) = &pg_prefix {
                cmd.arg(format!("-I{prefix}/include"));
                cmd.arg(format!("-L{prefix}/lib"));
            }
            cmd.arg("-lpq");
        }

        // OpenSSL (libssl + libcrypto) — same conditional pattern.
        let c_uses_openssl = std::fs::read_to_string(&tmp_path)
            .map(|s| s.contains("SSL_new") || s.contains("SSL_CTX_new") || s.contains("SHA1("))
            .unwrap_or(false);
        if c_uses_openssl {
            let ssl_prefix = std::env::var("OPENSSL_PREFIX")
                .ok()
                .or_else(|| pkgconfig_prefix("openssl"))
                .or_else(|| brew_prefix("openssl@3"))
                .or_else(|| brew_prefix("openssl"));
            if let Some(prefix) = &ssl_prefix {
                cmd.arg(format!("-I{prefix}/include"));
                cmd.arg(format!("-L{prefix}/lib"));
            }
            cmd.arg("-lssl");
            cmd.arg("-lcrypto");
        }
    }

    for extra in &opts.extra_cc_args {
        cmd.arg(extra);
    }

    if opts.verbose {
        eprintln!("ailangc: {} {}",
            cc,
            cmd.get_args()
                .map(|a| a.to_string_lossy().to_string())
                .collect::<Vec<_>>()
                .join(" "));
    }

    let status = cmd.status().map_err(|e| DriverError::CcSpawn {
        cc: cc.clone(),
        source: e,
    })?;
    if !status.success() {
        return Err(DriverError::CcFailed {
            status: status.code().unwrap_or(-1),
        });
    }
    Ok(bin_path)
}

fn pkgconfig_prefix(pkg: &str) -> Option<String> {
    let out = Command::new("pkg-config")
        .args(["--variable=prefix", pkg])
        .output()
        .ok()?;
    if !out.status.success() {
        return None;
    }
    let s = String::from_utf8(out.stdout).ok()?.trim().to_string();
    if s.is_empty() { None } else { Some(s) }
}

fn brew_prefix(pkg: &str) -> Option<String> {
    let out = Command::new("brew").args(["--prefix", pkg]).output().ok()?;
    if !out.status.success() {
        return None;
    }
    let s = String::from_utf8(out.stdout).ok()?.trim().to_string();
    if s.is_empty() { None } else { Some(s) }
}

fn read_source(path: &Path) -> Result<String, DriverError> {
    fs::read_to_string(path).map_err(|e| DriverError::Read {
        path: path.display().to_string(),
        source: e,
    })
}

/// Recursively parse a file plus its `im "path"` dependencies and accumulate
/// the items into `items`. Imported files contribute their `fn`/`st`/`ex`
/// declarations; if a non-root file has a (real or synthesized) `fn main`,
/// it's silently dropped so the root file's main is the one that runs.
///
/// Circular imports are detected via the `visited` set (canonical paths).
/// Sema diagnostics for cross-file errors still point at the importing
/// file's `source` — full multi-file diagnostics land with M6 polish.
fn parse_with_imports(
    path: &Path,
    root_source: &str,
    items: &mut Vec<Item>,
    visited: &mut HashSet<PathBuf>,
    is_root: bool,
) -> Result<(), DriverError> {
    // Stdlib paths route to embedded source — bypass the filesystem entirely
    // so installed `ailangc` (which doesn't ship a sibling `std/` dir) keeps
    // working. We key the visited set by the verbatim stdlib path string,
    // since `canonicalize` would fail without a real file.
    let path_str = path.to_string_lossy().to_string();
    let stdlib_src = stdlib_source(&path_str);
    let canonical = if stdlib_src.is_some() {
        PathBuf::from(&path_str)
    } else {
        fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf())
    };
    if !visited.insert(canonical) {
        return Ok(()); // already imported
    }

    let path_disp = path.display().to_string();
    let source = if is_root {
        root_source.to_string()
    } else if let Some(embedded) = stdlib_src {
        embedded.to_string()
    } else {
        read_source(path)?
    };

    // Lex
    let tokens = lex(&source);
    let lex_errs = tokens.iter().filter(|t| t.kind == TokenKind::Error).count();
    if lex_errs > 0 {
        return Err(DriverError::LexErrors {
            path: path_disp,
            count: lex_errs,
        });
    }

    // Parse
    let (module, parse_errs) = parse(&source, &tokens);
    if !parse_errs.is_empty() {
        let diags: Vec<Diagnostic> = parse_errs
            .iter()
            .map(|e| {
                let mut d = Diagnostic::error(e.message.clone(), e.span);
                if let Some(h) = &e.help {
                    d = d.with_help(h.clone());
                }
                d
            })
            .collect();
        report(&path_disp, &source, &diags);
        return Err(DriverError::ParseErrors {
            path: path_disp,
            count: parse_errs.len(),
        });
    }

    // Walk imports first so libraries appear before the files that use them.
    // Resolution order:
    //   1. embedded stdlib (any path matching a `std/*.ail` we ship)
    //   2. relative to the importing file's directory (most common for
    //      user code)
    //   3. relative to the current working directory
    let dir = path.parent().unwrap_or_else(|| Path::new("."));
    for item in &module.items {
        if let Item::Import(imp) = item {
            let import_path = if stdlib_source(&imp.path.value).is_some() {
                PathBuf::from(&imp.path.value)
            } else {
                let candidates: Vec<PathBuf> = vec![
                    dir.join(&imp.path.value),
                    PathBuf::from(&imp.path.value),
                ];
                candidates
                    .iter()
                    .find(|c| c.exists())
                    .cloned()
                    .unwrap_or_else(|| candidates[0].clone())
            };
            parse_with_imports(&import_path, root_source, items, visited, /*is_root=*/ false)?;
        }
    }

    // Then this file's own items (drop Import; drop non-root main).
    for item in module.items {
        match item {
            Item::Import(_) => continue,
            Item::Fn(f) if !is_root && f.name.name == "main" => continue,
            other => items.push(other),
        }
    }

    Ok(())
}

/// Scan all collected items for referenced-but-unresolved names. Any name
/// that matches a stdlib export triggers `parse_with_imports` on the owning
/// stdlib module. Loops to a fixpoint so transitive deps land — std/pg.ail
/// might depend on names from std/str.ail, etc.
fn auto_import_stdlib(
    items: &mut Vec<Item>,
    visited: &mut HashSet<PathBuf>,
) -> Result<(), DriverError> {
    let index = stdlib_symbol_index();
    loop {
        let mut defined: HashSet<String> = HashSet::new();
        for item in items.iter() {
            match item {
                Item::Fn(f) => { defined.insert(f.name.name.clone()); }
                Item::Extern(e) => { defined.insert(e.sig.name.name.clone()); }
                Item::Struct(s) => { defined.insert(s.name.name.clone()); }
                Item::Enum(e) => {
                    defined.insert(e.name.name.clone());
                    for v in &e.variants { defined.insert(v.name.name.clone()); }
                }
                Item::Import(_) => {}
            }
        }
        let mut referenced: HashSet<String> = HashSet::new();
        collect_referenced_names(items, &mut referenced);

        let mut to_import: HashSet<&'static str> = HashSet::new();
        for name in referenced.difference(&defined) {
            if let Some(stdlib_path) = index.get(name.as_str()) {
                if !visited.contains(&PathBuf::from(*stdlib_path)) {
                    to_import.insert(*stdlib_path);
                }
            }
        }
        if to_import.is_empty() {
            return Ok(());
        }
        // Iterate in sorted order so the import sequence is deterministic
        // across runs (purely for tidier diagnostics; behavior is identical).
        let mut sorted: Vec<&'static str> = to_import.into_iter().collect();
        sorted.sort();
        for path in sorted {
            parse_with_imports(
                Path::new(path),
                /* root_source unused for non-root */ "",
                items,
                visited,
                /*is_root=*/ false,
            )?;
        }
    }
}

// ---------- Name collection helpers (auto-import scan) ----------

fn collect_referenced_names(items: &[Item], out: &mut HashSet<String>) {
    for item in items {
        if let Item::Fn(f) = item {
            collect_in_block(&f.body, out);
        }
    }
}

fn collect_in_block(block: &Block, out: &mut HashSet<String>) {
    for stmt in &block.stmts {
        collect_in_stmt(stmt, out);
    }
    if let Some(tail) = &block.tail_expr {
        collect_in_expr(tail, out);
    }
}

fn collect_in_stmt(stmt: &Stmt, out: &mut HashSet<String>) {
    match stmt {
        Stmt::Decl { value, .. } => collect_in_expr(value, out),
        Stmt::Assign { target, value, .. } => {
            collect_in_expr(target, out);
            collect_in_expr(value, out);
        }
        Stmt::Expr(e) => collect_in_expr(e, out),
        Stmt::Return { value: Some(e), .. } => collect_in_expr(e, out),
        Stmt::Return { value: None, .. } | Stmt::Break(_) | Stmt::Continue(_) => {}
        Stmt::If(i) => {
            collect_in_expr(&i.cond, out);
            collect_in_block(&i.then_branch, out);
            collect_in_else(i.else_branch.as_ref(), out);
        }
        Stmt::Loop(lp) => {
            if let Some(head) = &lp.head {
                match head {
                    LoopHead::ForIn { iter, .. } => collect_in_expr(iter, out),
                    LoopHead::While(c) => collect_in_expr(c, out),
                }
            }
            collect_in_block(&lp.body, out);
        }
        Stmt::Match(m) => {
            collect_in_expr(&m.scrutinee, out);
            for arm in &m.arms { collect_in_expr(&arm.body, out); }
        }
    }
}

fn collect_in_else(else_branch: Option<&ElseBranch>, out: &mut HashSet<String>) {
    match else_branch {
        Some(ElseBranch::Block(b)) => collect_in_block(b, out),
        Some(ElseBranch::If(i)) => {
            collect_in_expr(&i.cond, out);
            collect_in_block(&i.then_branch, out);
            collect_in_else(i.else_branch.as_ref(), out);
        }
        None => {}
    }
}

fn collect_in_expr(e: &Expr, out: &mut HashSet<String>) {
    match &e.kind {
        ExprKind::Ident(n) => { out.insert(n.clone()); }
        ExprKind::Call { callee, args } => {
            collect_in_expr(callee, out);
            for a in args { collect_in_expr(a, out); }
        }
        ExprKind::Index { container, index } => {
            collect_in_expr(container, out);
            collect_in_expr(index, out);
        }
        ExprKind::Field { container, .. } => collect_in_expr(container, out),
        ExprKind::Binary { lhs, rhs, .. } => {
            collect_in_expr(lhs, out);
            collect_in_expr(rhs, out);
        }
        ExprKind::Unary { operand, .. } => collect_in_expr(operand, out),
        ExprKind::Ternary { cond, then_, else_ } => {
            collect_in_expr(cond, out);
            collect_in_expr(then_, out);
            collect_in_expr(else_, out);
        }
        ExprKind::Pipe { lhs, rhs } => {
            collect_in_expr(lhs, out);
            collect_in_expr(rhs, out);
        }
        ExprKind::Lambda { body, .. } => match body {
            LambdaBody::Expr(inner) => collect_in_expr(inner, out),
            LambdaBody::Block(b) => collect_in_block(b, out),
        },
        ExprKind::Array(items) => for it in items { collect_in_expr(it, out); },
        ExprKind::Map(entries) => for (k, v) in entries {
            collect_in_expr(k, out);
            collect_in_expr(v, out);
        },
        ExprKind::Tuple(items) => for it in items { collect_in_expr(it, out); },
        ExprKind::StructLit { fields, .. } => for (_, v) in fields { collect_in_expr(v, out); },
        ExprKind::Block(b) => collect_in_block(b, out),
        ExprKind::If(i) => {
            collect_in_expr(&i.cond, out);
            collect_in_block(&i.then_branch, out);
            collect_in_else(i.else_branch.as_ref(), out);
        }
        ExprKind::Match(m) => {
            collect_in_expr(&m.scrutinee, out);
            for arm in &m.arms { collect_in_expr(&arm.body, out); }
        }
        ExprKind::Try(inner) => collect_in_expr(inner, out),
        ExprKind::Lit(_) | ExprKind::Underscore => {}
    }
}


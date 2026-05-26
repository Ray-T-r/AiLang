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

use std::collections::HashSet;
use std::fs;
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::process::Command;

use ailang_codegen_llvm::emit_c;
use ailang_diag::{report, Diagnostic};
use ailang_lexer::lex;
use ailang_parser::parse;
use ailang_sema::analyze;
use ailang_syntax::ast::{Item, Module};
use ailang_syntax::{Token, TokenKind};

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
    let canonical = fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
    if !visited.insert(canonical) {
        return Ok(()); // already imported
    }

    let path_disp = path.display().to_string();
    let source = if is_root {
        root_source.to_string()
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
    //   1. relative to the importing file's directory (most common)
    //   2. relative to the current working directory (lets a project that
    //      uses `cwd = repo root` find `std/foo.ail` from anywhere)
    let dir = path.parent().unwrap_or_else(|| Path::new("."));
    for item in &module.items {
        if let Item::Import(imp) = item {
            let candidates: Vec<PathBuf> = vec![
                dir.join(&imp.path.value),
                PathBuf::from(&imp.path.value),
            ];
            let import_path = candidates
                .iter()
                .find(|c| c.exists())
                .cloned()
                .unwrap_or_else(|| candidates[0].clone());
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

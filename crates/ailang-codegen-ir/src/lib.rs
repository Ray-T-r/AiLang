//! AiLang → LLVM IR (textual) backend.
//!
//! This is the **M6+ backend**: instead of transpiling to C99 and handing the
//! result to `clang -O2`, we emit LLVM IR text (`.ll`) directly and ask
//! `clang` (still as the host driver, but now treating us as a frontend) to
//! optimize and link. Skipping the C layer means:
//!
//! - **One fewer parser invocation per compile.** Clang's C frontend is
//!   ~40% of `clang -O2`'s wall time on small inputs; for AiLang programs
//!   that's a measurable improvement.
//! - **Direct control over IR shape.** Inline-cost hints, calling-convention
//!   choices, and SSA construction live in our codegen rather than relying on
//!   clang inferring intent from C.
//! - **A landing pad for richer types** (sum types, traits, generics-via-
//!   monomorphization) that don't fit C's value-semantics cleanly.
//!
//! ## Scope of this initial drop
//!
//! What works end-to-end via `--backend=ir`:
//!
//! - Top-level `println(int_literal)` and `println(string_literal)`.
//! - `fn main()` with a body of such calls.
//! - Implicit-main wrapping (driven by the parser, same as the C backend).
//!
//! What's **stubbed**: arithmetic, control flow, user-defined functions,
//! arrays/maps, GC, FFI. Each will land as a focused follow-up — the IR
//! emission helpers below are intentionally small and orthogonal so adding
//! a new construct touches one section.
//!
//! ## Why text IR and not `inkwell`
//!
//! `inkwell` (Rust bindings over the LLVM C++ API) requires a system LLVM
//! with a version exactly matching the crate's feature flag (`llvm15`,
//! `llvm17`, …). The local toolchain here is LLVM 22, ahead of any inkwell
//! release. Emitting text IR keeps us fully version-agnostic: any `clang`
//! ≥ 15 reads it, and we can swap in `inkwell` later without touching any
//! caller (the public surface is just `emit_ir(&ResolvedModule) -> String`).

use ailang_sema::ResolvedModule;
use ailang_syntax::ast::*;
use std::fmt::Write;

/// Compile a resolved module to a textual LLVM IR string.
pub fn emit_ir(resolved: &ResolvedModule) -> String {
    let mut s = String::new();
    s.push_str(HEADER);

    // Collect string literals so we can declare them once at module scope.
    // Each gets a unique `@.str.N` global of type `[N x i8]`.
    let mut strings: Vec<String> = Vec::new();
    collect_strings(&resolved.module, &mut strings);
    for (i, lit) in strings.iter().enumerate() {
        let bytes = lit.as_bytes();
        let with_nul = bytes.len() + 1;
        let _ = writeln!(
            s,
            "@.str.{i} = private unnamed_addr constant [{with_nul} x i8] c\"{}\\00\"",
            escape_for_ir(lit)
        );
    }
    s.push('\n');

    // Declare external runtime helpers we'll call from main.
    s.push_str("declare i32 @puts(ptr)\n");
    s.push_str("declare i32 @printf(ptr, ...)\n");
    s.push_str("\n@.int_fmt = private unnamed_addr constant [6 x i8] c\"%lld\\0A\\00\"\n\n");

    // Walk items: only `fn main` is emitted in this initial drop; other
    // user functions stay unimplemented and would surface a TODO comment.
    for item in &resolved.module.items {
        if let Item::Fn(f) = item {
            if f.name.name == "main" {
                emit_main(&mut s, f, &strings);
            } else {
                let _ = writeln!(s, "; TODO M6+: emit user fn `{}`", f.name.name);
            }
        }
    }

    s
}

fn emit_main(s: &mut String, f: &FnDecl, strings: &[String]) {
    s.push_str("define i32 @main() {\n");
    s.push_str("entry:\n");

    // Track which `@.str.N` index we've consumed as we walk left-to-right.
    let mut next_str = 0usize;
    let mut next_tmp = 0usize;
    for stmt in &f.body.stmts {
        emit_stmt(s, stmt, strings, &mut next_str, &mut next_tmp);
    }
    if let Some(tail) = &f.body.tail_expr {
        emit_expr_stmt(s, tail, strings, &mut next_str, &mut next_tmp);
    }

    s.push_str("  ret i32 0\n");
    s.push_str("}\n");
}

fn emit_stmt(
    s: &mut String,
    stmt: &Stmt,
    strings: &[String],
    next_str: &mut usize,
    next_tmp: &mut usize,
) {
    match stmt {
        Stmt::Expr(e) => emit_expr_stmt(s, e, strings, next_str, next_tmp),
        _ => {
            let _ = writeln!(s, "  ; TODO M6+: stmt {:?}", std::mem::discriminant(stmt));
        }
    }
}

fn emit_expr_stmt(
    s: &mut String,
    e: &Expr,
    strings: &[String],
    next_str: &mut usize,
    next_tmp: &mut usize,
) {
    // Only handle `println(arg)` for now.
    if let ExprKind::Call { callee, args } = &e.kind {
        if let ExprKind::Ident(name) = &callee.kind {
            if (name == "println" || name == "print") && args.len() == 1 {
                let arg = &args[0];
                match &arg.kind {
                    ExprKind::Lit(LitExpr {
                        kind: Lit::Str(_), ..
                    }) => {
                        let idx = *next_str;
                        *next_str += 1;
                        let bytes = strings[idx].as_bytes().len() + 1;
                        let _ = writeln!(
                            s,
                            "  call i32 @puts(ptr getelementptr inbounds ([{} x i8], ptr @.str.{}, i64 0, i64 0))",
                            bytes, idx
                        );
                        return;
                    }
                    ExprKind::Lit(LitExpr {
                        kind: Lit::Int { value, .. },
                        ..
                    }) => {
                        let t = *next_tmp;
                        *next_tmp += 1;
                        let _ = writeln!(
                            s,
                            "  %t{t} = call i32 (ptr, ...) @printf(ptr getelementptr inbounds ([6 x i8], ptr @.int_fmt, i64 0, i64 0), i64 {value})"
                        );
                        return;
                    }
                    _ => {}
                }
            }
        }
    }
    let _ = writeln!(
        s,
        "  ; TODO M6+: expr {:?}",
        std::mem::discriminant(&e.kind)
    );
}

/// Collect every string literal appearing in `println("...")` calls so we
/// can emit them as module-scope globals in source order — matches the index
/// the per-call code uses.
fn collect_strings(m: &Module, out: &mut Vec<String>) {
    for item in &m.items {
        if let Item::Fn(f) = item {
            walk_block(&f.body, out);
        }
    }
}

fn walk_block(b: &Block, out: &mut Vec<String>) {
    for s in &b.stmts {
        if let Stmt::Expr(e) = s {
            walk_expr(e, out);
        }
    }
    if let Some(t) = &b.tail_expr {
        walk_expr(t, out);
    }
}

fn walk_expr(e: &Expr, out: &mut Vec<String>) {
    if let ExprKind::Call { callee, args } = &e.kind {
        if let ExprKind::Ident(n) = &callee.kind {
            if (n == "println" || n == "print") && args.len() == 1 {
                if let ExprKind::Lit(LitExpr {
                    kind: Lit::Str(s), ..
                }) = &args[0].kind
                {
                    out.push(s.clone());
                }
            }
        }
    }
}

/// Escape a string for the textual `c"..."` IR string-literal form. LLVM
/// requires `\NN` (two hex digits) for any byte outside the printable ASCII
/// range, plus `\\` for backslash and `\22` for the double quote.
fn escape_for_ir(s: &str) -> String {
    let mut out = String::new();
    for b in s.bytes() {
        match b {
            b'"' => out.push_str("\\22"),
            b'\\' => out.push_str("\\5C"),
            0x20..=0x7E => out.push(b as char),
            other => {
                let _ = write!(out, "\\{:02X}", other);
            }
        }
    }
    out
}

const HEADER: &str = "; Generated by ailangc (M6+ LLVM IR backend, initial drop).\n\
                      ; Compile with: clang -O2 <file>.ll -o <file>\n\
                      ;\n";

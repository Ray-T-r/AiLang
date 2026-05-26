//! Code generation.
//!
//! **Current backend: transpile to portable C99/C11 source.** The plan calls
//! for LLVM IR via `inkwell`, but the C transpiler gets the whole pipeline
//! working end-to-end with zero LLVM environment setup, and `clang -O2` on
//! the output produces machine code competitive with anything we'd emit by
//! hand. Switching to LLVM IR is an isolated codegen-only change for a future
//! milestone — the rest of the compiler (lexer/parser/sema/driver) is backend-
//! agnostic.

use ailang_sema::{ResolvedModule, Ty};
use ailang_syntax::ast::*;
use std::fmt::Write;

pub fn emit_c(resolved: &ResolvedModule) -> String {
    let mut out = String::new();
    out.push_str(PRELUDE);
    out.push('\n');

    let module = &resolved.module;

    // ----- Forward declarations for all user functions (handles mutual recursion) -----
    for item in &module.items {
        if let Item::Fn(f) = item {
            emit_fn_signature(&mut out, f, resolved);
            out.push_str(";\n");
        }
    }

    // ----- Extern declarations from `ex` items -----
    for item in &module.items {
        if let Item::Extern(e) = item {
            emit_extern(&mut out, e);
            out.push('\n');
        }
    }

    out.push('\n');

    // ----- Function bodies -----
    for item in &module.items {
        if let Item::Fn(f) = item {
            emit_fn(&mut out, f, resolved);
            out.push('\n');
        }
    }

    out
}

fn emit_fn_signature(out: &mut String, f: &FnDecl, res: &ResolvedModule) {
    // Special-case `main` so it returns `int` (C entry point) — also keep
    // its name un-mangled.
    let is_main = f.name.name == "main";
    let ret = if is_main {
        "int".to_string()
    } else if f.return_ty.is_some() {
        c_ty_for_ret(f.return_ty.as_ref())
    } else {
        // No annotation — use sema's inferred return type (lives in fn_table).
        match res.fn_table.get(&f.name.name).map(|s| &s.return_ty) {
            Some(Ty::Unit) => "void".to_string(),
            Some(t) => c_ty_for(t),
            None => "void".to_string(),
        }
    };
    let mangled = if is_main { "main".to_string() } else { c_safe_name(&f.name.name) };
    let _ = write!(out, "{} {}(", ret, mangled);
    if f.params.is_empty() {
        out.push_str("void");
    } else {
        for (i, p) in f.params.iter().enumerate() {
            if i > 0 {
                out.push_str(", ");
            }
            let ty = c_ty_for_param(p.ty.as_ref());
            let _ = write!(out, "{} {}", ty, c_safe_name(&p.name.name));
        }
    }
    out.push(')');
}

fn emit_fn(out: &mut String, f: &FnDecl, res: &ResolvedModule) {
    emit_fn_signature(out, f, res);
    out.push_str(" {\n");
    let ctx = EmitCtx { fns: res };
    let is_main = f.name.name == "main";
    if is_main {
        // Initialize Boehm GC before any allocations.
        out.push_str("    GC_init();\n");
    }
    let returns_value = !is_main && match f.return_ty.as_ref().map(ailang_sema::ast_ty_kind_to_ty) {
        Some(Ty::Unit) => false,
        Some(_) => true,
        None => ailang_sema::fn_returns_value(&f.body, &res.fn_table),
    };
    emit_block_body(out, &f.body, 1, &ctx, returns_value);
    if is_main {
        out.push_str("    return 0;\n");
    }
    out.push_str("}\n");
}

fn emit_extern(out: &mut String, e: &ExternDecl) {
    let ret = c_ty_for_ret(e.sig.return_ty.as_ref());
    let _ = write!(out, "extern {} {}(", ret, e.sig.name.name);
    if e.sig.params.is_empty() && !e.sig.variadic {
        out.push_str("void");
    } else {
        for (i, p) in e.sig.params.iter().enumerate() {
            if i > 0 {
                out.push_str(", ");
            }
            let ty = c_ty_for_param(p.ty.as_ref());
            let _ = write!(out, "{} {}", ty, p.name.name);
        }
        if e.sig.variadic {
            if !e.sig.params.is_empty() {
                out.push_str(", ");
            }
            out.push_str("...");
        }
    }
    out.push_str(");");
}

struct EmitCtx<'a> {
    #[allow(dead_code)] // reserved for codegen passes that need the symbol table
    fns: &'a ResolvedModule,
}

fn indent(out: &mut String, level: usize) {
    for _ in 0..level {
        out.push_str("    ");
    }
}

/// Emit a block body (without the surrounding braces). When `returns_value`
/// is true and the block has a `tail_expr`, the tail becomes an implicit
/// `return`.
fn emit_block_body(out: &mut String, block: &Block, level: usize, ctx: &EmitCtx, returns_value: bool) {
    for stmt in &block.stmts {
        emit_stmt(out, stmt, level, ctx);
    }
    if let Some(tail) = &block.tail_expr {
        indent(out, level);
        if returns_value {
            out.push_str("return ");
        }
        emit_expr(out, tail, ctx);
        out.push_str(";\n");
    }
}

fn emit_block_braced(out: &mut String, block: &Block, level: usize, ctx: &EmitCtx) {
    out.push_str("{\n");
    emit_block_body(out, block, level + 1, ctx, false);
    indent(out, level);
    out.push('}');
}

fn emit_stmt(out: &mut String, stmt: &Stmt, level: usize, ctx: &EmitCtx) {
    match stmt {
        Stmt::Decl { name, ty, value, .. } => {
            indent(out, level);
            let cty = c_ty_for_decl(ty.as_ref(), value, ctx);
            let _ = write!(out, "{} {} = ", cty, c_safe_name(&name.name));
            emit_expr(out, value, ctx);
            out.push_str(";\n");
        }
        Stmt::Assign { target, value, .. } => {
            indent(out, level);
            // Special case: `m[k] = v` where `m` is a map → setter call.
            if let ExprKind::Index { container, index } = &target.kind {
                if matches!(
                    ctx.fns.expr_types.get(&container.span),
                    Some(Ty::Map(_, _))
                ) {
                    out.push_str("ailang_map_ii_set(");
                    emit_expr(out, container, ctx);
                    out.push_str(", ");
                    emit_expr(out, index, ctx);
                    out.push_str(", ");
                    emit_expr(out, value, ctx);
                    out.push_str(");\n");
                    return;
                }
            }
            emit_expr(out, target, ctx);
            out.push_str(" = ");
            emit_expr(out, value, ctx);
            out.push_str(";\n");
        }
        Stmt::Expr(e) => {
            indent(out, level);
            emit_expr(out, e, ctx);
            out.push_str(";\n");
        }
        Stmt::Return { value, .. } => {
            indent(out, level);
            out.push_str("return");
            if let Some(v) = value {
                out.push(' ');
                emit_expr(out, v, ctx);
            }
            out.push_str(";\n");
        }
        Stmt::Break(_) => {
            indent(out, level);
            out.push_str("break;\n");
        }
        Stmt::Continue(_) => {
            indent(out, level);
            out.push_str("continue;\n");
        }
        Stmt::If(if_) => emit_if_stmt(out, if_, level, ctx),
        Stmt::Loop(lp) => emit_loop_stmt(out, lp, level, ctx),
        Stmt::Match(mt) => emit_match_stmt(out, mt, level, ctx),
    }
}

/// Match codegen — translate `mt scrut { pat => body; ... }` into an if/else
/// chain. To keep things simple in M3:
///
/// - The scrutinee may be either a tuple expression or a scalar expression.
///   For a tuple, each element gets its own temporary; for a scalar, one temp.
/// - Each arm's pattern is checked against the same vector of temporaries.
///   Literal patterns produce equality conditions; wildcard and binding
///   patterns are vacuously true.
/// - Bindings introduce typed C locals at the start of the arm body.
/// - Nested tuple patterns and non-trivial nested patterns are rejected with
///   a comment (M3 supports only one level of tuple destructuring).
fn emit_match_stmt(out: &mut String, mt: &MatchStmt, level: usize, ctx: &EmitCtx) {
    // 1. Extract per-element scrutinees.
    let scruts: Vec<&Expr> = match &mt.scrutinee.kind {
        ExprKind::Tuple(elems) => elems.iter().collect(),
        _ => vec![&mt.scrutinee],
    };

    // 2. Pick a unique prefix using the span of the match so nested matches
    //    don't collide.
    let label = format!("__m{}", mt.span.start);
    let tmps: Vec<String> = (0..scruts.len()).map(|i| format!("{label}_{i}")).collect();

    indent(out, level);
    out.push_str("{\n");
    // Evaluate scrutinees once.
    for (var, expr) in tmps.iter().zip(&scruts) {
        indent(out, level + 1);
        // Use the codegen heuristic for declaration type.
        let cty = c_ty_for_decl(None, expr, ctx);
        let _ = write!(out, "{} {} = ", cty, var);
        emit_expr(out, expr, ctx);
        out.push_str(";\n");
    }

    // 3. Emit each arm as `if (cond) { bindings; body; }` chained with `else`.
    let mut first = true;
    for arm in &mt.arms {
        let elem_pats: Vec<&Pattern> = match &arm.pattern {
            Pattern::Tuple { elems, .. } => elems.iter().collect(),
            other => vec![other],
        };

        // Build the condition string from each element pattern.
        let mut conds: Vec<String> = Vec::new();
        for (i, p) in elem_pats.iter().enumerate() {
            if i >= tmps.len() {
                continue;
            }
            if let Some(c) = pattern_cond(p, &tmps[i], ctx) {
                conds.push(c);
            }
        }
        let cond = if conds.is_empty() {
            "1".to_string()
        } else {
            conds.join(" && ")
        };

        indent(out, level + 1);
        if first {
            let _ = write!(out, "if ({cond}) ");
            first = false;
        } else {
            let _ = write!(out, "else if ({cond}) ");
        }
        out.push_str("{\n");

        // Emit binding declarations.
        for (i, p) in elem_pats.iter().enumerate() {
            if i >= tmps.len() {
                continue;
            }
            if let Pattern::Binding(id) = p {
                indent(out, level + 2);
                let _ = writeln!(out, "int64_t {} = {};", c_safe_name(&id.name), tmps[i]);
            }
        }

        // Emit the arm body.
        indent(out, level + 2);
        emit_expr(out, &arm.body, ctx);
        out.push_str(";\n");

        indent(out, level + 1);
        out.push_str("}\n");
    }

    indent(out, level);
    out.push_str("}\n");
}

/// Build a C boolean expression that's true when `pat` matches `var`.
/// Returns `None` for patterns that always succeed (wildcard / binding).
fn pattern_cond(pat: &Pattern, var: &str, _ctx: &EmitCtx) -> Option<String> {
    match pat {
        Pattern::Wildcard(_) | Pattern::Binding(_) => None,
        Pattern::Literal(lit) => {
            let mut s = String::new();
            emit_lit(&mut s, lit);
            Some(format!("({var} == {s})"))
        }
        Pattern::Tuple { .. } => {
            // Nested tuple destructuring isn't unwound yet — emit `0` so the
            // arm never fires and codegen stays warning-free.
            Some(format!("/* nested tuple pattern */ 0"))
        }
    }
}

fn emit_if_stmt(out: &mut String, if_: &IfStmt, level: usize, ctx: &EmitCtx) {
    indent(out, level);
    out.push_str("if (");
    emit_expr(out, &if_.cond, ctx);
    out.push_str(") ");
    emit_block_braced(out, &if_.then_branch, level, ctx);
    match &if_.else_branch {
        Some(ElseBranch::Block(b)) => {
            out.push_str(" else ");
            emit_block_braced(out, b, level, ctx);
            out.push('\n');
        }
        Some(ElseBranch::If(inner)) => {
            out.push_str(" else ");
            // Inline: avoid an extra newline+indent.
            out.push_str("if (");
            emit_expr(out, &inner.cond, ctx);
            out.push_str(") ");
            emit_block_braced(out, &inner.then_branch, level, ctx);
            // Recursive emit for `el if ... el ...` chain.
            emit_else_chain(out, &inner.else_branch, level, ctx);
            out.push('\n');
        }
        None => out.push('\n'),
    }
}

fn emit_else_chain(out: &mut String, eb: &Option<ElseBranch>, level: usize, ctx: &EmitCtx) {
    match eb {
        Some(ElseBranch::Block(b)) => {
            out.push_str(" else ");
            emit_block_braced(out, b, level, ctx);
        }
        Some(ElseBranch::If(inner)) => {
            out.push_str(" else if (");
            emit_expr(out, &inner.cond, ctx);
            out.push_str(") ");
            emit_block_braced(out, &inner.then_branch, level, ctx);
            emit_else_chain(out, &inner.else_branch, level, ctx);
        }
        None => {}
    }
}

fn emit_loop_stmt(out: &mut String, lp: &LoopStmt, level: usize, ctx: &EmitCtx) {
    indent(out, level);
    match &lp.head {
        None => {
            out.push_str("for (;;) ");
        }
        Some(LoopHead::While(cond)) => {
            out.push_str("while (");
            emit_expr(out, cond, ctx);
            out.push_str(") ");
        }
        Some(LoopHead::ForIn { var, iter }) => {
            // Range form: `lp i in 1..10` / `lp i in 1..=10`.
            // Array form: `lp x in arr` — iterates over the elements of an
            //   `[i64]` or `[str]` array (sema's inferred type drives the
            //   element binding's C type).
            let safe = c_safe_name(&var.name);
            match &iter.kind {
                ExprKind::Binary { op: BinOp::Range, lhs, rhs }
                | ExprKind::Binary { op: BinOp::RangeEq, lhs, rhs } => {
                    let inclusive = matches!(iter.kind, ExprKind::Binary { op: BinOp::RangeEq, .. });
                    let _ = write!(out, "for (int64_t {} = ", safe);
                    emit_expr(out, lhs, ctx);
                    let cmp = if inclusive { "<=" } else { "<" };
                    let _ = write!(out, "; {} {} ", safe, cmp);
                    emit_expr(out, rhs, ctx);
                    let _ = write!(out, "; {}++) ", safe);
                }
                _ => {
                    // Array iteration. The element type is taken from sema's
                    // expression types where possible, defaulting to i64.
                    let (elem_ty, container_ty) = match infer_iter_ty(iter, ctx) {
                        Some(Ty::Str) => ("const char*", "ailang_arr_str"),
                        _ => ("int64_t", "ailang_arr_i64"),
                    };
                    let tag = lp.span.start;
                    let arr = format!("__arr{tag}");
                    let idx = format!("__i{tag}");
                    // Snapshot the iterable, then for-i over it.
                    out.push_str("{\n");
                    indent(out, level + 1);
                    let _ = write!(out, "{} {} = ", container_ty, arr);
                    emit_expr(out, iter, ctx);
                    out.push_str(";\n");
                    indent(out, level + 1);
                    let _ = write!(
                        out,
                        "for (int64_t {idx} = 0; {idx} < {arr}.len; {idx}++) {{ {elem_ty} {safe} = {arr}.data[{idx}];\n"
                    );
                    emit_block_body(out, &lp.body, level + 2, ctx, false);
                    indent(out, level + 1);
                    out.push_str("}\n");
                    indent(out, level);
                    out.push_str("}\n");
                    return;
                }
            }
        }
    }
    emit_block_braced(out, &lp.body, level, ctx);
    out.push('\n');
}

/// Conservatively guess the element type of an iterable for `lp x in expr`.
/// First consults sema's per-expression type table (handles `lp x in arr`
/// where `arr` is a local var), then falls back to AST shape inspection
/// (handles `lp x in [1,2,3]` literals when sema didn't visit the iter).
fn infer_iter_ty(e: &Expr, ctx: &EmitCtx) -> Option<Ty> {
    if let Some(Ty::Array(elem)) = ctx.fns.expr_types.get(&e.span) {
        return Some((**elem).clone());
    }
    match &e.kind {
        ExprKind::Array(xs) => {
            if let Some(first) = xs.first() {
                if matches!(&first.kind, ExprKind::Lit(l) if matches!(l.kind, Lit::Str(_))) {
                    return Some(Ty::Str);
                }
            }
            Some(Ty::I64)
        }
        ExprKind::Call { callee, .. } => {
            if let ExprKind::Ident(n) = &callee.kind {
                if let Some(sig) = ctx.fns.fn_table.get(n) {
                    if let Ty::Array(elem) = &sig.return_ty {
                        return Some((**elem).clone());
                    }
                }
            }
            None
        }
        _ => None,
    }
}

fn emit_expr(out: &mut String, e: &Expr, ctx: &EmitCtx) {
    match &e.kind {
        ExprKind::Lit(l) => emit_lit(out, l),
        ExprKind::Ident(name) => out.push_str(&c_safe_name(name)),
        ExprKind::Underscore => out.push_str("/*_*/0"),
        ExprKind::Call { callee, args } => {
            emit_expr(out, callee, ctx);
            out.push('(');
            for (i, a) in args.iter().enumerate() {
                if i > 0 {
                    out.push_str(", ");
                }
                emit_expr(out, a, ctx);
            }
            out.push(')');
        }
        ExprKind::Binary { op: BinOp::Concat, lhs, rhs } => {
            // `++` always means string concat (legacy explicit form).
            out.push_str("ailang_str_concat(");
            emit_expr(out, lhs, ctx);
            out.push_str(", ");
            emit_expr(out, rhs, ctx);
            out.push(')');
        }
        ExprKind::Binary { op: BinOp::Add, lhs, rhs }
            if is_str_expr(lhs, ctx) || is_str_expr(rhs, ctx) =>
        {
            // Unified `+`: when either operand is statically known to be a
            // string, dispatch to the concat runtime instead of integer add.
            out.push_str("ailang_str_concat(");
            emit_expr(out, lhs, ctx);
            out.push_str(", ");
            emit_expr(out, rhs, ctx);
            out.push(')');
        }
        ExprKind::Binary { op, lhs, rhs } => {
            out.push('(');
            emit_expr(out, lhs, ctx);
            out.push(' ');
            out.push_str(c_binop(*op));
            out.push(' ');
            emit_expr(out, rhs, ctx);
            out.push(')');
        }
        ExprKind::Unary { op, operand } => {
            out.push('(');
            out.push_str(c_unop(*op));
            emit_expr(out, operand, ctx);
            out.push(')');
        }
        ExprKind::Ternary { cond, then_, else_ } => {
            out.push('(');
            emit_expr(out, cond, ctx);
            out.push_str(" ? ");
            emit_expr(out, then_, ctx);
            out.push_str(" : ");
            emit_expr(out, else_, ctx);
            out.push(')');
        }
        ExprKind::Index { container, index } => {
            // Polymorphic — `_Generic` dispatches on the container type so
            // `s[i]` works for strings and `a[i]` works for `[i64]`/`[str]`.
            out.push_str("ailang_at(");
            emit_expr(out, container, ctx);
            out.push_str(", ");
            emit_expr(out, index, ctx);
            out.push(')');
        }
        ExprKind::Field { container, name } => {
            emit_expr(out, container, ctx);
            out.push('.');
            out.push_str(&name.name);
        }
        ExprKind::Pipe { lhs, rhs } => {
            // `x |> f(a, b)`  becomes `f(x, a, b)` — left value is inserted
            // as the **first** argument. This matches the convention in F#,
            // Elixir, OCaml, and AiLang's spec §1.3.
            // `x |> f` (bare identifier) becomes `f(x)`.
            match &rhs.kind {
                ExprKind::Call { callee, args } => {
                    emit_expr(out, callee, ctx);
                    out.push('(');
                    emit_expr(out, lhs, ctx);
                    for a in args {
                        out.push_str(", ");
                        emit_expr(out, a, ctx);
                    }
                    out.push(')');
                }
                _ => {
                    emit_expr(out, rhs, ctx);
                    out.push('(');
                    emit_expr(out, lhs, ctx);
                    out.push(')');
                }
            }
        }
        ExprKind::Lambda { .. } => {
            out.push_str("/* lambda not supported in M2 */0");
        }
        ExprKind::Array(elems) => {
            // Pick the macro based on the first element's static type. Mixed
            // arrays fall back to the i64 ctor; user code shouldn't write
            // mixed arrays at this point in the language anyway.
            let is_str = matches!(
                elems.first().map(|e| &e.kind),
                Some(ExprKind::Lit(l)) if matches!(l.kind, Lit::Str(_))
            );
            let macro_name = if is_str { "AILANG_ARR_STR" } else { "AILANG_ARR_I64" };
            let _ = write!(out, "{}({}", macro_name, elems.len());
            for e in elems {
                out.push_str(", ");
                emit_expr(out, e, ctx);
            }
            out.push(')');
        }
        ExprKind::Map(entries) => {
            // {k1:v1, k2:v2, ...} → statement-expression building a fresh
            // map and setting each entry. Initial capacity rounded up from
            // the literal size so we avoid one grow on construction.
            let cap = (entries.len() * 2).max(8);
            let _ = write!(out, "({{ ailang_map_ii __m = ailang_map_ii_make({}); ", cap);
            for (k, v) in entries {
                out.push_str("ailang_map_ii_set(__m, ");
                emit_expr(out, k, ctx);
                out.push_str(", ");
                emit_expr(out, v, ctx);
                out.push_str("); ");
            }
            out.push_str("__m; })");
        }
        ExprKind::Tuple(_) => {
            out.push_str("/* tuple literal not yet supported */0");
        }
        ExprKind::Block(_) => {
            out.push_str("/* block expression not yet supported in M2 */0");
        }
        ExprKind::If(_) | ExprKind::Match(_) => {
            out.push_str("/* if/match as expression not yet supported in M2 */0");
        }
        ExprKind::Try(_) => {
            out.push_str("/* `?` propagation not supported in M2 */0");
        }
    }
}

fn emit_lit(out: &mut String, l: &LitExpr) {
    match &l.kind {
        Lit::Int { value, .. } => {
            let _ = write!(out, "INT64_C({value})");
        }
        Lit::Float { value, .. } => {
            // Use repr that round-trips and disambiguates as double.
            let s = format!("{value:?}");
            let _ = write!(out, "{}", s);
        }
        Lit::Str(s) => {
            out.push('"');
            for ch in s.chars() {
                match ch {
                    '\\' => out.push_str("\\\\"),
                    '"' => out.push_str("\\\""),
                    '\n' => out.push_str("\\n"),
                    '\r' => out.push_str("\\r"),
                    '\t' => out.push_str("\\t"),
                    '\0' => out.push_str("\\0"),
                    c if (c as u32) < 0x20 => {
                        let _ = write!(out, "\\x{:02x}", c as u32);
                    }
                    c => out.push(c),
                }
            }
            out.push('"');
        }
        Lit::Char(c) => {
            let _ = write!(out, "INT64_C({})", *c as u32);
        }
        Lit::Bool(b) => out.push_str(if *b { "true" } else { "false" }),
        Lit::Nil => out.push_str("NULL"),
    }
}

/// Does `e` statically evaluate to a string? Used to dispatch unified `+`
/// to `ailang_str_concat`. Conservative — only returns true when at least
/// one syntactic form clearly produces a string. For identifiers we'd need
/// a local symbol table; until then, programs concatenating two bare string
/// vars (`a + b`) still need explicit `++`.
fn is_str_expr(e: &Expr, ctx: &EmitCtx) -> bool {
    match &e.kind {
        ExprKind::Lit(l) => matches!(l.kind, Lit::Str(_)),
        ExprKind::Binary { op: BinOp::Concat, .. } => true,
        ExprKind::Binary { op: BinOp::Add, lhs, rhs } => {
            is_str_expr(lhs, ctx) || is_str_expr(rhs, ctx)
        }
        ExprKind::Call { callee, .. } => {
            if let ExprKind::Ident(name) = &callee.kind {
                if let Some(sig) = ctx.fns.fn_table.get(name) {
                    return matches!(sig.return_ty, Ty::Str);
                }
            }
            false
        }
        ExprKind::Pipe { rhs, .. } => {
            let callee_name = match &rhs.kind {
                ExprKind::Call { callee, .. } => {
                    if let ExprKind::Ident(n) = &callee.kind { Some(n.as_str()) } else { None }
                }
                ExprKind::Ident(n) => Some(n.as_str()),
                _ => None,
            };
            if let Some(name) = callee_name {
                if let Some(sig) = ctx.fns.fn_table.get(name) {
                    return matches!(sig.return_ty, Ty::Str);
                }
            }
            false
        }
        _ => false,
    }
}

fn c_binop(op: BinOp) -> &'static str {
    use BinOp::*;
    match op {
        Add => "+",  Sub => "-",  Mul => "*",  Div => "/",  Mod => "%",
        Eq => "==", Ne => "!=",
        Lt => "<",  Le => "<=",  Gt => ">",   Ge => ">=",
        And => "&&", Or => "||",
        BitAnd => "&", BitOr => "|", BitXor => "^",
        Shl => "<<", Shr => ">>",
        // The remaining ops produce non-trivial C output; sema warns when
        // they appear in an M2 program.
        Concat => "/* ++ */ +",
        Range | RangeEq => "/* range outside for-in */ ,",
        Coalesce => "/* ?? */ ,",
    }
}

fn c_unop(op: UnOp) -> &'static str {
    match op {
        UnOp::Neg => "-",
        UnOp::Not => "!",
        UnOp::Deref => "*",
        UnOp::AddrOf => "&",
    }
}

fn c_ty_for_ret(ty: Option<&Type>) -> String {
    match ty {
        None => "void".to_string(),
        Some(t) => c_ty_from_ast(t),
    }
}

fn c_ty_for_param(ty: Option<&Type>) -> String {
    // Default unannotated lambda params to int64_t.
    match ty {
        None => "int64_t".to_string(),
        Some(t) => c_ty_from_ast(t),
    }
}

/// AST-level type → C type. Used for extern declarations and fn signatures
/// where the *exact* integer width matters (vs the sema `Ty` which collapses
/// every fixed-width integer to a single bucket for permissive checking).
fn c_ty_from_ast(t: &Type) -> String {
    match &t.kind {
        TypeKind::Path(name) => match name.as_str() {
            "i8" => "int8_t".to_string(),
            "i16" => "int16_t".to_string(),
            "i32" => "int32_t".to_string(),
            "i64" => "int64_t".to_string(),
            "u8" => "uint8_t".to_string(),
            "u16" => "uint16_t".to_string(),
            "u32" => "uint32_t".to_string(),
            "u64" => "uint64_t".to_string(),
            "f32" => "float".to_string(),
            "f64" => "double".to_string(),
            "bool" => "bool".to_string(),
            "str" => "const char*".to_string(),
            _ => "int64_t".to_string(),
        },
        TypeKind::Array(inner) => match &inner.kind {
            TypeKind::Path(name) if name == "str" => "ailang_arr_str".to_string(),
            _ => "ailang_arr_i64".to_string(),
        },
        TypeKind::Map(_, _) => "ailang_map_ii".to_string(),
        TypeKind::Ptr(inner) => format!("{}*", c_ty_from_ast(inner)),
        _ => "int64_t".to_string(),
    }
}

fn c_ty_for_decl(ty: Option<&Type>, init: &Expr, ctx: &EmitCtx) -> String {
    if let Some(t) = ty {
        return c_ty_for(&ailang_sema::ast_ty_kind_to_ty(t));
    }
    // Infer from the initializer's shape.
    match &init.kind {
        ExprKind::Lit(l) => match &l.kind {
            Lit::Float { .. } => "double".to_string(),
            Lit::Bool(_) => "bool".to_string(),
            Lit::Str(_) => "const char*".to_string(),
            Lit::Int { .. } | Lit::Char(_) => "int64_t".to_string(),
            Lit::Nil => "void*".to_string(),
        },
        ExprKind::Array(xs) => match xs.first().map(|x| &x.kind) {
            Some(ExprKind::Lit(l)) if matches!(l.kind, Lit::Str(_)) => "ailang_arr_str".to_string(),
            _ => "ailang_arr_i64".to_string(),
        },
        ExprKind::Map(_) => "ailang_map_ii".to_string(),
        ExprKind::Binary { op: BinOp::Concat, .. } => "const char*".to_string(),
        ExprKind::Binary { op: BinOp::Add, lhs, rhs } if is_str_expr(lhs, ctx) || is_str_expr(rhs, ctx) => {
            "const char*".to_string()
        }
        ExprKind::Binary { op, .. } => match op {
            BinOp::Eq | BinOp::Ne | BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge
            | BinOp::And | BinOp::Or => "bool".to_string(),
            _ => "int64_t".to_string(),
        },
        ExprKind::Call { callee, .. } => {
            // Resolve the callee's return type from the symbol table.
            if let ExprKind::Ident(name) = &callee.kind {
                if let Some(sig) = ctx.fns.fn_table.get(name) {
                    return c_ty_for(&sig.return_ty);
                }
            }
            "int64_t".to_string()
        }
        ExprKind::Pipe { rhs, .. } => {
            // Pipe result has the type of the right-hand call's return.
            let callee_name = match &rhs.kind {
                ExprKind::Call { callee, .. } => {
                    if let ExprKind::Ident(n) = &callee.kind { Some(n.as_str()) } else { None }
                }
                ExprKind::Ident(n) => Some(n.as_str()),
                _ => None,
            };
            if let Some(name) = callee_name {
                if let Some(sig) = ctx.fns.fn_table.get(name) {
                    return c_ty_for(&sig.return_ty);
                }
            }
            "int64_t".to_string()
        }
        _ => "int64_t".to_string(),
    }
}

/// Return a C-safe spelling of an AiLang identifier.
/// Identifiers that collide with a C keyword, common stdlib type, or our own
/// runtime helpers get an `__ail_` prefix; everything else is returned
/// unchanged so generated C stays readable.
fn c_safe_name(name: &str) -> String {
    if is_c_reserved(name) {
        format!("__ail_{name}")
    } else {
        name.to_string()
    }
}

fn is_c_reserved(name: &str) -> bool {
    matches!(
        name,
        // C89/C99/C11 keywords
        "auto" | "break" | "case" | "char" | "const" | "continue" | "default"
        | "do" | "double" | "else" | "enum" | "extern" | "float" | "for"
        | "goto" | "if" | "inline" | "int" | "long" | "register" | "restrict"
        | "return" | "short" | "signed" | "sizeof" | "static" | "struct"
        | "switch" | "typedef" | "union" | "unsigned" | "void" | "volatile"
        | "while"
        | "_Bool" | "_Complex" | "_Imaginary" | "_Alignas" | "_Alignof"
        | "_Atomic" | "_Static_assert" | "_Noreturn" | "_Thread_local"
        | "_Generic"
        // common stdlib typedefs/macros
        | "bool" | "true" | "false" | "NULL"
        | "int8_t" | "int16_t" | "int32_t" | "int64_t"
        | "uint8_t" | "uint16_t" | "uint32_t" | "uint64_t"
        | "size_t" | "ssize_t" | "ptrdiff_t" | "intptr_t" | "uintptr_t"
        | "INT64_C" | "UINT64_C"
        // our runtime
        | "ailang_print_i64" | "ailang_println_i64"
        | "ailang_print_f64" | "ailang_println_f64"
        | "ailang_print_bool" | "ailang_println_bool"
        | "ailang_print_str" | "ailang_println_str"
    )
}

fn c_ty_for(t: &Ty) -> String {
    match t {
        Ty::I64 => "int64_t".to_string(),
        Ty::F64 => "double".to_string(),
        Ty::Bool => "bool".to_string(),
        Ty::Str => "const char*".to_string(),
        Ty::Unit => "void".to_string(),
        Ty::Array(elem) => match **elem {
            Ty::Str => "ailang_arr_str".to_string(),
            _ => "ailang_arr_i64".to_string(),
        },
        Ty::Map(_, _) => "ailang_map_ii".to_string(),
        Ty::Unknown => "int64_t".to_string(),
    }
}

// ============================================================================
// C prelude
// ============================================================================

const PRELUDE: &str = r#"// Generated by ailangc. Do not edit by hand.
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <gc.h>

/* -------- print/println -------- */
static inline void ailang_print_i64(int64_t v) { printf("%lld", (long long)v); }
static inline void ailang_println_i64(int64_t v) { printf("%lld\n", (long long)v); }
/* `int` is a distinct C11 type from `int64_t` even when both are 32/64 bits
 * on the current target. FFI return types like `int abs(int)` need this. */
static inline void ailang_print_int(int v) { printf("%d", v); }
static inline void ailang_println_int(int v) { printf("%d\n", v); }
static inline void ailang_print_f64(double v) { printf("%g", v); }
static inline void ailang_println_f64(double v) { printf("%g\n", v); }
static inline void ailang_print_bool(bool v) { printf("%s", v ? "true" : "false"); }
static inline void ailang_println_bool(bool v) { printf("%s\n", v ? "true" : "false"); }
static inline void ailang_print_str(const char* s) { printf("%s", s); }
static inline void ailang_println_str(const char* s) { printf("%s\n", s); }

/* -------- string operations (heap allocated via Boehm GC) --------
 *
 * Strings remain null-terminated `const char*` so they interoperate with C
 * libraries (printf, puts) without conversion. Allocation goes through
 * GC_malloc_atomic — the contents are bytes with no internal pointers,
 * so Boehm can skip scanning them, which is both faster and avoids spurious
 * retention. */
static inline const char* ailang_str_concat(const char* a, const char* b) {
    size_t la = a ? strlen(a) : 0;
    size_t lb = b ? strlen(b) : 0;
    char* out = (char*) GC_malloc_atomic(la + lb + 1);
    if (la) memcpy(out, a, la);
    if (lb) memcpy(out + la, b, lb);
    out[la + lb] = '\0';
    return out;
}

static inline int64_t ailang_str_len(const char* s) {
    return s ? (int64_t) strlen(s) : 0;
}

static inline int64_t ailang_str_at(const char* s, int64_t i) {
    return (int64_t)(unsigned char) s[i];
}

/* -------- arrays (M4+): [T] for T = i64 / str --------
 *
 * Arrays are a struct-by-value (len + data pointer). The backing store is
 * GC_MALLOC'd so it survives across function boundaries without any user-side
 * memory management. We keep struct-by-value so `_Generic` can dispatch on
 * the static type — pointers to different element types would alias. */
typedef struct { int64_t len; int64_t* data; } ailang_arr_i64;
typedef struct { int64_t len; const char** data; } ailang_arr_str;

static inline ailang_arr_i64 ailang_arr_i64_make(int64_t n, const int64_t* src) {
    ailang_arr_i64 a;
    a.len = n;
    a.data = (int64_t*) GC_MALLOC((size_t)n * sizeof(int64_t));
    for (int64_t i = 0; i < n; i++) a.data[i] = src[i];
    return a;
}
static inline ailang_arr_str ailang_arr_str_make(int64_t n, const char* const* src) {
    ailang_arr_str a;
    a.len = n;
    a.data = (const char**) GC_MALLOC((size_t)n * sizeof(const char*));
    for (int64_t i = 0; i < n; i++) a.data[i] = src[i];
    return a;
}

static inline int64_t ailang_arr_i64_len(ailang_arr_i64 a) { return a.len; }
static inline int64_t ailang_arr_str_len(ailang_arr_str a) { return a.len; }
static inline int64_t ailang_arr_i64_at(ailang_arr_i64 a, int64_t i) { return a.data[i]; }
static inline const char* ailang_arr_str_at(ailang_arr_str a, int64_t i) { return a.data[i]; }

/* Compound-literal-based ctors so `[1, 2, 3]` codegens to a one-liner. */
#define AILANG_ARR_I64(n, ...) ailang_arr_i64_make((n), (int64_t[]){__VA_ARGS__})
#define AILANG_ARR_STR(n, ...) ailang_arr_str_make((n), (const char*[]){__VA_ARGS__})

#define len(x) _Generic((x), \
    char*: ailang_str_len, \
    const char*: ailang_str_len, \
    ailang_arr_i64: ailang_arr_i64_len, \
    ailang_arr_str: ailang_arr_str_len, \
    ailang_map_ii: ailang_map_ii_len)(x)

/* `has(m, k)` — map-only membership check. */
#define has(m, k) _Generic((m), \
    ailang_map_ii: ailang_map_ii_has)((m), (k))

/* -------- maps (M4+): {K:V} as open-addressing hash tables --------
 *
 * For M4+ we ship a single key/value combo: `{i64:i64}`. Mixed-type maps and
 * string-keyed maps land in a follow-up — the codegen path is already
 * structured around per-(K,V) function families, so it's an additive change.
 *
 * Maps are *reference* values (struct pointer) so `m[k] = v` mutates in
 * place. Allocation goes through GC_MALLOC; the underlying arrays are
 * resized in-place when load factor exceeds 0.7. */
typedef struct {
    int64_t cap;
    int64_t len;
    int64_t* keys;
    int64_t* values;
    uint8_t* occupied;
} ailang_map_ii_storage;
typedef ailang_map_ii_storage* ailang_map_ii;

static inline uint64_t ailang_hash_i64(int64_t k) {
    uint64_t x = (uint64_t)k;
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static ailang_map_ii ailang_map_ii_make(int64_t initial_cap) {
    int64_t cap = 8;
    while (cap < initial_cap) cap *= 2;
    ailang_map_ii m = (ailang_map_ii) GC_MALLOC(sizeof(ailang_map_ii_storage));
    m->cap = cap;
    m->len = 0;
    m->keys = (int64_t*) GC_MALLOC((size_t)cap * sizeof(int64_t));
    m->values = (int64_t*) GC_MALLOC((size_t)cap * sizeof(int64_t));
    m->occupied = (uint8_t*) GC_MALLOC((size_t)cap);
    return m;
}

static void ailang_map_ii_grow(ailang_map_ii m) {
    int64_t old_cap = m->cap;
    int64_t* old_keys = m->keys;
    int64_t* old_values = m->values;
    uint8_t* old_occupied = m->occupied;
    int64_t new_cap = old_cap * 2;
    m->cap = new_cap;
    m->keys = (int64_t*) GC_MALLOC((size_t)new_cap * sizeof(int64_t));
    m->values = (int64_t*) GC_MALLOC((size_t)new_cap * sizeof(int64_t));
    m->occupied = (uint8_t*) GC_MALLOC((size_t)new_cap);
    uint64_t mask = (uint64_t)(new_cap - 1);
    for (int64_t i = 0; i < old_cap; i++) {
        if (!old_occupied[i]) continue;
        int64_t k = old_keys[i];
        uint64_t h = ailang_hash_i64(k) & mask;
        for (int64_t p = 0; p < new_cap; p++) {
            int64_t j = (int64_t)((h + (uint64_t)p) & mask);
            if (!m->occupied[j]) {
                m->keys[j] = k;
                m->values[j] = old_values[i];
                m->occupied[j] = 1;
                break;
            }
        }
    }
}

static void ailang_map_ii_set(ailang_map_ii m, int64_t k, int64_t v) {
    if (m->len * 10 >= m->cap * 7) ailang_map_ii_grow(m);
    uint64_t mask = (uint64_t)(m->cap - 1);
    uint64_t h = ailang_hash_i64(k) & mask;
    for (int64_t p = 0; p < m->cap; p++) {
        int64_t i = (int64_t)((h + (uint64_t)p) & mask);
        if (!m->occupied[i]) {
            m->keys[i] = k;
            m->values[i] = v;
            m->occupied[i] = 1;
            m->len++;
            return;
        }
        if (m->keys[i] == k) { m->values[i] = v; return; }
    }
}

static int64_t ailang_map_ii_get(ailang_map_ii m, int64_t k) {
    uint64_t mask = (uint64_t)(m->cap - 1);
    uint64_t h = ailang_hash_i64(k) & mask;
    for (int64_t p = 0; p < m->cap; p++) {
        int64_t i = (int64_t)((h + (uint64_t)p) & mask);
        if (!m->occupied[i]) return 0;
        if (m->keys[i] == k) return m->values[i];
    }
    return 0;
}

static int64_t ailang_map_ii_len(ailang_map_ii m) { return m->len; }
static bool ailang_map_ii_has(ailang_map_ii m, int64_t k) {
    uint64_t mask = (uint64_t)(m->cap - 1);
    uint64_t h = ailang_hash_i64(k) & mask;
    for (int64_t p = 0; p < m->cap; p++) {
        int64_t i = (int64_t)((h + (uint64_t)p) & mask);
        if (!m->occupied[i]) return false;
        if (m->keys[i] == k) return true;
    }
    return false;
}

/* Polymorphic indexing: `a[i]` codegens to `ailang_at(a, i)`. */
#define ailang_at(x, i) _Generic((x), \
    char*: ailang_str_at, \
    const char*: ailang_str_at, \
    ailang_arr_i64: ailang_arr_i64_at, \
    ailang_arr_str: ailang_arr_str_at, \
    ailang_map_ii: ailang_map_ii_get)((x), (i))

/* Dispatch print/println by argument type via C11 _Generic.
 *
 * Only types codegen actually produces are listed — we don't include
 * synonyms (`long long` is the same type as `int64_t` on every platform we
 * target, and listing both is a hard error in C11). All integer literals
 * we emit are wrapped in `INT64_C(...)` so they're already `int64_t`. */
#define print(x)   _Generic((x), \
    int64_t: ailang_print_i64, \
    int: ailang_print_int, \
    double: ailang_print_f64, \
    bool: ailang_print_bool, \
    char*: ailang_print_str, \
    const char*: ailang_print_str)(x)

#define println(x) _Generic((x), \
    int64_t: ailang_println_i64, \
    int: ailang_println_int, \
    double: ailang_println_f64, \
    bool: ailang_println_bool, \
    char*: ailang_println_str, \
    const char*: ailang_println_str)(x)
"#;

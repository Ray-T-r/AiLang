//! Statement and expression emission (mutually recursive).
use crate::collect::*;
use crate::decl::*;
use crate::names::*;
use crate::typegen::*;
use ailang_sema::{ResolvedModule, Ty};
use ailang_syntax::ast::*;
use ailang_syntax::token::Span;
use std::fmt::Write;

pub(crate) fn emit_lambda_body(out: &mut String, info: &LambdaInfo, ctx: &EmitCtx) {
    emit_lambda_signature(out, info);
    out.push_str(" {\n");
    if info.captures.is_empty() {
        out.push_str("    (void)__env_;\n");
    } else {
        // Unpack captures from the env struct so the body can use the
        // names directly (matches what user code sees in the outer scope).
        let _ = write!(out, "    {0}* __env = ({0}*)__env_;\n", info.env_name);
        for (n, t) in &info.captures {
            let safe = c_safe_name(n);
            out.push_str("    ");
            out.push_str(&c_decl(&safe, t));
            let _ = write!(out, " = __env->{};\n", safe);
        }
    }
    match &info.body {
        LambdaBody::Expr(e) => {
            out.push_str("    return ");
            emit_expr(out, e, ctx);
            out.push_str(";\n");
        }
        LambdaBody::Block(b) => {
            emit_block_body(out, b, 1, ctx, /*returns_value=*/ true);
        }
    }
    out.push_str("}\n");
}

pub(crate) fn emit_fn(out: &mut String, f: &FnDecl, res: &ResolvedModule, ctx: &EmitCtx) {
    emit_fn_signature(out, f, res);
    out.push_str(" {\n");
    let is_main = f.name.name == "main";
    if is_main {
        // Initialize Boehm GC + stash argc/argv before any user code runs;
        // the `args()` builtin reads from `ailang_argc_` / `ailang_argv_`.
        out.push_str("    GC_init();\n");
        out.push_str("    ailang_argc_ = __argc;\n");
        out.push_str("    ailang_argv_ = __argv;\n");
    }
    let returns_value = !is_main
        && match f.return_ty.as_ref().map(ailang_sema::ast_ty_kind_to_ty) {
            Some(Ty::Unit) => false,
            Some(_) => true,
            None => ailang_sema::fn_returns_value(&f.body, &res.fn_table),
        };
    // Stash this fn's C return type so `expr?` inside the body knows
    // which err-struct shape to propagate.
    let ret_c = if is_main {
        "int".to_string()
    } else if let Some(t) = &f.return_ty {
        c_ty_from_ast(t)
    } else {
        match res.fn_table.get(&f.name.name).map(|s| &s.return_ty) {
            Some(t) => c_ty_for(t),
            None => "int64_t".to_string(),
        }
    };
    let prev_ret = ctx.current_ret_ty.replace(ret_c);
    emit_block_body(out, &f.body, 1, ctx, returns_value);
    ctx.current_ret_ty.replace(prev_ret);
    if is_main {
        out.push_str("    return 0;\n");
    }
    out.push_str("}\n");
}

pub(crate) struct EmitCtx<'a> {
    #[allow(dead_code)] // reserved for codegen passes that need the symbol table
    pub(crate) fns: &'a ResolvedModule,
    /// Lifted lambdas (span → C name + sig). emit_expr substitutes the name
    /// when it encounters the original Lambda expression.
    pub(crate) lambdas: &'a [(Span, LambdaInfo)],
    /// Current function's C return type. Threaded so `expr?` knows what
    /// shape of err-struct to propagate to.
    pub(crate) current_ret_ty: std::cell::RefCell<String>,
}

pub(crate) fn indent(out: &mut String, level: usize) {
    for _ in 0..level {
        out.push_str("    ");
    }
}

/// Emit a block body (without the surrounding braces). When `returns_value`
/// is true and the block has a `tail_expr`, the tail becomes an implicit
/// `return`. Also: when no `tail_expr` and the last stmt is `if`/`mt`,
/// the branches' tails become returns too — so a fn body whose final
/// statement is `if cond X el Y` doesn't need an explicit `rt` on each
/// arm.
pub(crate) fn emit_block_body(
    out: &mut String,
    block: &Block,
    level: usize,
    ctx: &EmitCtx,
    returns_value: bool,
) {
    let last_idx = block.stmts.len().saturating_sub(1);
    for (i, stmt) in block.stmts.iter().enumerate() {
        let is_last = i == last_idx && block.tail_expr.is_none();
        if returns_value && is_last {
            match stmt {
                Stmt::If(if_) => {
                    emit_if_stmt_returning(out, if_, level, ctx);
                    continue;
                }
                Stmt::Match(m) => {
                    emit_match_stmt_returning(out, m, level, ctx);
                    continue;
                }
                _ => {}
            }
        }
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

/// Emit a branch of an if/match that's in tail position: any tail-expr
/// inside becomes `return tail;`.
pub(crate) fn emit_branch_block_returning(
    out: &mut String,
    b: &Block,
    level: usize,
    ctx: &EmitCtx,
) {
    out.push_str("{\n");
    emit_block_body(out, b, level + 1, ctx, /*returns_value=*/ true);
    indent(out, level);
    out.push('}');
}

pub(crate) fn emit_block_braced(out: &mut String, block: &Block, level: usize, ctx: &EmitCtx) {
    out.push_str("{\n");
    emit_block_body(out, block, level + 1, ctx, false);
    indent(out, level);
    out.push('}');
}

pub(crate) fn emit_stmt(out: &mut String, stmt: &Stmt, level: usize, ctx: &EmitCtx) {
    match stmt {
        Stmt::Decl {
            name, ty, value, ..
        } => {
            indent(out, level);
            let cty = c_ty_for_decl(ty.as_ref(), value, ctx);
            let _ = write!(out, "{} = ", c_decl(&c_safe_name(&name.name), &cty));
            emit_expr(out, value, ctx);
            out.push_str(";\n");
        }
        Stmt::Assign { target, value, .. } => {
            indent(out, level);
            // Special case: `m[k] = v` where `m` is a map → setter call.
            if let ExprKind::Index { container, index } = &target.kind {
                if let Some(Ty::Map(kt, vt)) = ctx.fns.expr_types.get(&container.span) {
                    let setter = match (&**kt, &**vt) {
                        (Ty::Str, Ty::Str) => "ailang_map_ss_set",
                        (Ty::Str, _) => "ailang_map_si_set",
                        _ => "ailang_map_ii_set",
                    };
                    let _ = write!(out, "{setter}(");
                    emit_expr(out, container, ctx);
                    out.push_str(", ");
                    emit_expr(out, index, ctx);
                    out.push_str(", ");
                    emit_expr(out, value, ctx);
                    out.push_str(");\n");
                    return;
                }
            }
            // Self-concat append: `name = name + rhs` (or `name ++ rhs`),
            // where the value is a string concat with `name` itself on the
            // LHS. Route through `ailang_str_append` so repeated appends
            // amortize via a thread-local builder buffer. See the prelude
            // doc for `ailang_str_append` for the aliasing caveat.
            if let ExprKind::Ident(tname) = &target.kind {
                if let ExprKind::Binary { op, lhs, rhs } = &value.kind {
                    let is_concat = matches!(op, BinOp::Concat)
                        || (matches!(op, BinOp::Add)
                            && (is_str_expr(lhs, ctx) || is_str_expr(rhs, ctx)));
                    if is_concat {
                        if let ExprKind::Ident(lname) = &lhs.kind {
                            if lname == tname {
                                let safe = c_safe_name(tname);
                                let _ = write!(out, "{safe} = ailang_str_append({safe}, ");
                                emit_expr(out, rhs, ctx);
                                out.push_str(");\n");
                                return;
                            }
                        }
                    }
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
pub(crate) fn emit_match_stmt(out: &mut String, mt: &MatchStmt, level: usize, ctx: &EmitCtx) {
    emit_match_stmt_inner(out, mt, level, ctx, /*returning=*/ false);
}

pub(crate) fn emit_match_stmt_inner(
    out: &mut String,
    mt: &MatchStmt,
    level: usize,
    ctx: &EmitCtx,
    returning: bool,
) {
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
    // Evaluate scrutinees once. Prefer sema's recorded type for the
    // scrutinee — that's the only way to learn it's a struct/enum value
    // (the syntactic fallback `c_ty_for_decl` defaults to int64_t).
    for (var, expr) in tmps.iter().zip(&scruts) {
        indent(out, level + 1);
        let cty = ctx
            .fns
            .expr_types
            .get(&expr.span)
            .map(|t| c_ty_for(t))
            .unwrap_or_else(|| c_ty_for_decl(None, expr, ctx));
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
            match p {
                Pattern::Binding(id) => {
                    indent(out, level + 2);
                    let _ = writeln!(out, "int64_t {} = {};", c_safe_name(&id.name), tmps[i]);
                }
                Pattern::Variant { name, bindings, .. } => {
                    if let Some(en_name) = ctx.fns.variant_to_enum.get(&name.name) {
                        if let Some(en) = ctx.fns.enum_table.get(en_name) {
                            if let Some(v) = en.variants.iter().find(|v| v.name.name == name.name) {
                                let v_name = c_safe_name(&name.name);
                                for (j, b) in bindings.iter().enumerate() {
                                    if b.name == "_" {
                                        continue;
                                    }
                                    if let Some(f) = v.fields.get(j) {
                                        let cty = c_ty_from_ast(&f.ty);
                                        indent(out, level + 2);
                                        let _ = writeln!(
                                            out,
                                            "{} = {}.__data.{}.{};",
                                            c_decl(&c_safe_name(&b.name), &cty),
                                            tmps[i],
                                            v_name,
                                            c_safe_name(&f.name.name)
                                        );
                                    }
                                }
                            }
                        }
                    }
                }
                _ => {}
            }
        }

        // Emit the arm body — wrap with `return` if we're in tail
        // position of a value-returning fn.
        indent(out, level + 2);
        if returning {
            out.push_str("return ");
        }
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
pub(crate) fn pattern_cond(pat: &Pattern, var: &str, ctx: &EmitCtx) -> Option<String> {
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
        Pattern::Variant { name, .. } => {
            // Look up which enum this variant belongs to + its tag index.
            if let Some(en_name) = ctx.fns.variant_to_enum.get(&name.name) {
                if let Some(en) = ctx.fns.enum_table.get(en_name) {
                    if let Some(tag) = en.variants.iter().position(|v| v.name.name == name.name) {
                        return Some(format!("({var}.__tag == {tag})"));
                    }
                }
            }
            Some(format!("/* unknown variant `{}` */ 0", name.name))
        }
    }
}

/// Like `emit_if_stmt` but every branch propagates implicit returns.
/// Used when an `if/el` is the last stmt of a value-returning fn body.
pub(crate) fn emit_if_stmt_returning(out: &mut String, if_: &IfStmt, level: usize, ctx: &EmitCtx) {
    indent(out, level);
    out.push_str("if (");
    emit_expr(out, &if_.cond, ctx);
    out.push_str(") ");
    emit_branch_block_returning(out, &if_.then_branch, level, ctx);
    match &if_.else_branch {
        Some(ElseBranch::Block(b)) => {
            out.push_str(" else ");
            emit_branch_block_returning(out, b, level, ctx);
            out.push('\n');
        }
        Some(ElseBranch::If(inner)) => {
            out.push_str(" else ");
            emit_if_stmt_returning_inline(out, inner, level, ctx);
            out.push('\n');
        }
        None => out.push('\n'),
    }
}

pub(crate) fn emit_if_stmt_returning_inline(
    out: &mut String,
    if_: &IfStmt,
    level: usize,
    ctx: &EmitCtx,
) {
    out.push_str("if (");
    emit_expr(out, &if_.cond, ctx);
    out.push_str(") ");
    emit_branch_block_returning(out, &if_.then_branch, level, ctx);
    match &if_.else_branch {
        Some(ElseBranch::Block(b)) => {
            out.push_str(" else ");
            emit_branch_block_returning(out, b, level, ctx);
        }
        Some(ElseBranch::If(inner)) => {
            out.push_str(" else ");
            emit_if_stmt_returning_inline(out, inner, level, ctx);
        }
        None => {}
    }
}

/// Like `emit_match_stmt` but each arm's body becomes a `return`.
pub(crate) fn emit_match_stmt_returning(
    out: &mut String,
    m: &MatchStmt,
    level: usize,
    ctx: &EmitCtx,
) {
    // Reuse the normal match emitter but make each arm body a return.
    // The existing emitter takes arm bodies as expressions; if the body
    // is a value expression we wrap it with `return ...;`. We synthesize
    // that by post-processing — easier to just emit a parallel impl.
    emit_match_stmt_inner(out, m, level, ctx, /*returning=*/ true);
}

pub(crate) fn emit_if_stmt(out: &mut String, if_: &IfStmt, level: usize, ctx: &EmitCtx) {
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

pub(crate) fn emit_else_chain(
    out: &mut String,
    eb: &Option<ElseBranch>,
    level: usize,
    ctx: &EmitCtx,
) {
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

pub(crate) fn emit_loop_stmt(out: &mut String, lp: &LoopStmt, level: usize, ctx: &EmitCtx) {
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
        Some(LoopHead::ForIn { vars, iter }) => {
            // Three shapes:
            //   single var + range:   `lp i in 1..10`
            //   single var + array:   `lp x in arr`
            //   two vars + map:       `lp (k, v) in m`
            let tag = lp.span.start;

            // Map tuple-destructure case takes priority — only triggers when
            // we have two vars AND the iterable is a map (per sema).
            if vars.len() == 2 {
                if let Some(Ty::Map(kt, vt)) = ctx.fns.expr_types.get(&iter.span) {
                    let (map_ty, key_ty, val_ty) = match (&**kt, &**vt) {
                        (Ty::Str, Ty::Str) => ("ailang_map_ss", "const char*", "const char*"),
                        (Ty::Str, _) => ("ailang_map_si", "const char*", "int64_t"),
                        _ => ("ailang_map_ii", "int64_t", "int64_t"),
                    };
                    let k = c_safe_name(&vars[0].name);
                    let v = c_safe_name(&vars[1].name);
                    let m = format!("__map{tag}");
                    let i = format!("__i{tag}");
                    out.push_str("{\n");
                    indent(out, level + 1);
                    let _ = write!(out, "{} {} = ", map_ty, m);
                    emit_expr(out, iter, ctx);
                    out.push_str(";\n");
                    indent(out, level + 1);
                    let _ = write!(
                        out,
                        "for (int64_t {i} = 0; {i} < {m}->cap; {i}++) {{ \
                         if (!{m}->occupied[{i}]) continue; \
                         {key_ty} {k} = {m}->keys[{i}]; \
                         {val_ty} {v} = {m}->values[{i}];\n"
                    );
                    emit_block_body(out, &lp.body, level + 2, ctx, false);
                    indent(out, level + 1);
                    out.push_str("}\n");
                    indent(out, level);
                    out.push_str("}\n");
                    return;
                }
                // Fallthrough: tuple destructure on non-map iter — let it
                // be parsed as if (just bind both as i64 from sema).
            }

            let safe = c_safe_name(&vars[0].name);
            match &iter.kind {
                ExprKind::Binary {
                    op: BinOp::Range,
                    lhs,
                    rhs,
                }
                | ExprKind::Binary {
                    op: BinOp::RangeEq,
                    lhs,
                    rhs,
                } => {
                    let inclusive = matches!(
                        iter.kind,
                        ExprKind::Binary {
                            op: BinOp::RangeEq,
                            ..
                        }
                    );
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
                    let arr = format!("__arr{tag}");
                    let idx = format!("__i{tag}");
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
pub(crate) fn infer_iter_ty(e: &Expr, ctx: &EmitCtx) -> Option<Ty> {
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
        ExprKind::Call { callee, args } => {
            if let ExprKind::Ident(n) = &callee.kind {
                // `keys(m)` / `values(m)` — derive element type from the map.
                if (n == "keys" || n == "values") && !args.is_empty() {
                    if let Some(Ty::Map(k, v)) = ctx.fns.expr_types.get(&args[0].span) {
                        let elem = if n == "keys" { &**k } else { &**v };
                        return Some(elem.clone());
                    }
                }
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

pub(crate) fn emit_expr(out: &mut String, e: &Expr, ctx: &EmitCtx) {
    match &e.kind {
        ExprKind::Lit(l) => emit_lit(out, l),
        ExprKind::Ident(name) => {
            // A bare uppercase ident that's a known unit variant constructs
            // an instance: `None` → `Maybe_None()`.
            if let Some(en) = ctx.fns.variant_to_enum.get(name) {
                let _ = write!(out, "{}_{}()", c_safe_name(en), c_safe_name(name));
            } else {
                out.push_str(&c_safe_name(name));
            }
        }
        ExprKind::Underscore => out.push_str("/*_*/0"),
        ExprKind::Call { callee, args } => {
            // Variant constructor with payload: `Some(42)` → `Maybe_Some(42)`.
            if let ExprKind::Ident(n) = &callee.kind {
                if let Some(en) = ctx.fns.variant_to_enum.get(n) {
                    let _ = write!(out, "{}_{}(", c_safe_name(en), c_safe_name(n));
                    for (i, a) in args.iter().enumerate() {
                        if i > 0 {
                            out.push_str(", ");
                        }
                        emit_expr(out, a, ctx);
                    }
                    out.push(')');
                    return;
                }
                // Polymorphic `abs(x)` — pick abs_i64 / abs_f64 by the arg's
                // sema-recorded type. Skip when the user shadowed the name
                // with an `ex fn abs(...)` extern (then call libc abs directly).
                if n == "abs" && args.len() == 1 {
                    let user_extern = ctx
                        .fns
                        .fn_table
                        .get("abs")
                        .map(|s| s.is_extern)
                        .unwrap_or(false);
                    if !user_extern {
                        let arg_ty = ctx.fns.expr_types.get(&args[0].span).cloned();
                        let helper = match arg_ty {
                            Some(Ty::F64) => "abs_f64",
                            _ => "abs_i64",
                        };
                        let _ = write!(out, "{helper}(");
                        emit_expr(out, &args[0], ctx);
                        out.push(')');
                        return;
                    }
                }
                // Polymorphic `err(msg)` — dispatch to err_i64/err_str/
                // err_bool/err_f64 based on the enclosing fn's C return type
                // (set by emit_fn before walking the body). Sema already
                // diagnosed the "not inside !T" case; we still default to
                // err_i64 so the error chain doesn't cascade.
                if n == "err" && args.len() == 1 {
                    let user_extern = ctx
                        .fns
                        .fn_table
                        .get("err")
                        .map(|s| s.is_extern)
                        .unwrap_or(false);
                    if !user_extern {
                        let ret_c = ctx.current_ret_ty.borrow().clone();
                        let helper = match ret_c.as_str() {
                            "ailang_result_str" => "err_str",
                            "ailang_result_bool" => "err_bool",
                            "ailang_result_f64" => "err_f64",
                            _ => "err_i64",
                        };
                        let _ = write!(out, "{helper}(");
                        emit_expr(out, &args[0], ctx);
                        out.push(')');
                        return;
                    }
                }
            }
            // Direct call when callee is an ident in the fn_table — emit
            // `name(args)` as before. Otherwise it's an indirect call
            // through a closure value.
            let is_direct = match &callee.kind {
                ExprKind::Ident(n) => ctx.fns.fn_table.contains_key(n),
                _ => false,
            };
            if is_direct {
                emit_expr(out, callee, ctx);
                out.push('(');
                for (i, a) in args.iter().enumerate() {
                    if i > 0 {
                        out.push_str(", ");
                    }
                    emit_expr(out, a, ctx);
                }
                out.push(')');
            } else {
                // Indirect call through a closure. We read the callee's
                // sema type for the return shape; args still default to
                // i64. Mixed-arg closures need an explicit fn-type
                // annotation upstream — out of scope for v1.
                let ret_c = ctx
                    .fns
                    .expr_types
                    .get(&callee.span)
                    .map(|t| c_ty_for(t))
                    .unwrap_or_else(|| "int64_t".to_string());
                let mut sig_params = String::from("void*");
                for _ in args {
                    sig_params.push_str(", int64_t");
                }
                let _ = write!(out, "((({ret_c} (*)({sig_params}))(");
                emit_expr(out, callee, ctx);
                out.push_str(").fn)((");
                emit_expr(out, callee, ctx);
                out.push_str(").env");
                for a in args {
                    out.push_str(", ");
                    emit_expr(out, a, ctx);
                }
                out.push_str("))");
            }
        }
        ExprKind::Binary {
            op: BinOp::Concat,
            lhs,
            rhs,
        } => {
            // `++` always means string concat (legacy explicit form).
            out.push_str("ailang_str_concat(");
            emit_expr(out, lhs, ctx);
            out.push_str(", ");
            emit_expr(out, rhs, ctx);
            out.push(')');
        }
        ExprKind::Binary {
            op: BinOp::Add,
            lhs,
            rhs,
        } if is_str_expr(lhs, ctx) || is_str_expr(rhs, ctx) => {
            // Unified `+`: when either operand is statically known to be a
            // string, dispatch to the concat runtime instead of integer add.
            out.push_str("ailang_str_concat(");
            emit_expr(out, lhs, ctx);
            out.push_str(", ");
            emit_expr(out, rhs, ctx);
            out.push(')');
        }
        ExprKind::Binary {
            op: BinOp::Eq,
            lhs,
            rhs,
        } if is_str_expr(lhs, ctx) && is_str_expr(rhs, ctx) => {
            // `a == b` on strings: pointer compare would be a footgun
            // (false negatives for equal-content strings). Use strcmp.
            out.push_str("(strcmp(");
            emit_expr(out, lhs, ctx);
            out.push_str(", ");
            emit_expr(out, rhs, ctx);
            out.push_str(") == 0)");
        }
        ExprKind::Binary {
            op: BinOp::Ne,
            lhs,
            rhs,
        } if is_str_expr(lhs, ctx) && is_str_expr(rhs, ctx) => {
            out.push_str("(strcmp(");
            emit_expr(out, lhs, ctx);
            out.push_str(", ");
            emit_expr(out, rhs, ctx);
            out.push_str(") != 0)");
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
            // Emit an `ailang_closure_t` literal. If the lifting pass
            // recorded captures, allocate an env struct on the GC heap and
            // copy in each captured value; otherwise pass NULL.
            if let Some((_, info)) = ctx.lambdas.iter().find(|(s, _)| *s == e.span) {
                if info.captures.is_empty() {
                    let _ = write!(
                        out,
                        "((ailang_closure_t){{ .fn = (void*){}, .env = NULL }})",
                        info.c_name
                    );
                } else {
                    let _ = write!(
                        out,
                        "({{ {0}* __e = ({0}*) GC_MALLOC(sizeof({0})); ",
                        info.env_name
                    );
                    for (n, _t) in &info.captures {
                        let safe = c_safe_name(n);
                        let _ = write!(out, "__e->{safe} = {safe}; ");
                    }
                    let _ = write!(
                        out,
                        "(ailang_closure_t){{ .fn = (void*){}, .env = (void*)__e }}; }})",
                        info.c_name
                    );
                }
            } else {
                out.push_str("/* unlifted lambda */0");
            }
        }
        ExprKind::Array(elems) => {
            // Pick the macro based on the first element's static type. Mixed
            // arrays fall back to the i64 ctor; user code shouldn't write
            // mixed arrays at this point in the language anyway.
            let is_str = matches!(
                elems.first().map(|e| &e.kind),
                Some(ExprKind::Lit(l)) if matches!(l.kind, Lit::Str(_))
            );
            let macro_name = if is_str {
                "AILANG_ARR_STR"
            } else {
                "AILANG_ARR_I64"
            };
            let _ = write!(out, "{}({}", macro_name, elems.len());
            for e in elems {
                out.push_str(", ");
                emit_expr(out, e, ctx);
            }
            out.push(')');
        }
        ExprKind::Map(entries) => {
            // {k1:v1, ...} → statement-expression building a fresh map and
            // setting each entry. Pick (K,V)-specific helpers from sema's
            // refined type when available (so empty `{}` whose K/V was
            // pinned by later usage picks the right helpers), else fall
            // back to inspecting the first key+value pair's static shape.
            let from_sema = ctx.fns.expr_types.get(&e.span).and_then(|t| match t {
                Ty::Map(k, v) if !matches!(**k, Ty::Unknown) || !matches!(**v, Ty::Unknown) => {
                    Some(match (&**k, &**v) {
                        (Ty::Str, Ty::Str) => {
                            ("ailang_map_ss", "ailang_map_ss_make", "ailang_map_ss_set")
                        }
                        (Ty::Str, _) => {
                            ("ailang_map_si", "ailang_map_si_make", "ailang_map_si_set")
                        }
                        _ => ("ailang_map_ii", "ailang_map_ii_make", "ailang_map_ii_set"),
                    })
                }
                _ => None,
            });
            let (ty, make_fn, set_fn) = from_sema.unwrap_or_else(|| {
                match entries.first().map(|(k, v)| (&k.kind, &v.kind)) {
                    Some((ExprKind::Lit(lk), ExprKind::Lit(lv)))
                        if matches!(lk.kind, Lit::Str(_)) && matches!(lv.kind, Lit::Str(_)) =>
                    {
                        ("ailang_map_ss", "ailang_map_ss_make", "ailang_map_ss_set")
                    }
                    Some((ExprKind::Lit(lk), _)) if matches!(lk.kind, Lit::Str(_)) => {
                        ("ailang_map_si", "ailang_map_si_make", "ailang_map_si_set")
                    }
                    _ => ("ailang_map_ii", "ailang_map_ii_make", "ailang_map_ii_set"),
                }
            });
            let cap = (entries.len() * 2).max(8);
            let _ = write!(out, "({{ {ty} __m = {make_fn}({cap}); ");
            for (k, v) in entries {
                let _ = write!(out, "{set_fn}(__m, ");
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
        ExprKind::StructLit { name, fields } => {
            // C99 compound literal with designated initializers — field
            // order in source doesn't have to match decl order.
            // Wrapped in an outer `(...)` so the commas inside don't get
            // mistaken for macro-argument separators when the literal is
            // passed to a function-like macro (e.g. `println(Point{...})`).
            let _ = write!(out, "(({}){{ ", c_safe_name(&name.name));
            for (i, (fname, fval)) in fields.iter().enumerate() {
                if i > 0 {
                    out.push_str(", ");
                }
                let _ = write!(out, ".{} = ", c_safe_name(&fname.name));
                emit_expr(out, fval, ctx);
            }
            out.push_str(" })");
        }
        ExprKind::Block(_) => {
            out.push_str("/* block expression not yet supported in M2 */0");
        }
        ExprKind::If(_) | ExprKind::Match(_) => {
            out.push_str("/* if/match as expression not yet supported in M2 */0");
        }
        ExprKind::Try(inner) => {
            // `expr?` — evaluate inner; if `.ok` is false, return early
            // with an err-struct shaped like the enclosing fn's return.
            // Assumes the inner's result shape matches (caller's
            // responsibility — v1 doesn't auto-convert err structs).
            let ret = ctx.current_ret_ty.borrow().clone();
            out.push_str("({ ");
            let _ = write!(out, "{ret} __r = ");
            emit_expr(out, inner, ctx);
            let _ = write!(
                out,
                "; if (!__r.ok) return ({ret}){{ .ok = false, .error = __r.error }}; __r.value; }})"
            );
        }
    }
}

pub(crate) fn emit_lit(out: &mut String, l: &LitExpr) {
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
pub(crate) fn is_str_expr(e: &Expr, ctx: &EmitCtx) -> bool {
    // Sema may have recorded a type for this exact expression — trust it
    // when it says Str. Catches e.g. `lambda_param + ...` where the param
    // was annotated `:str`.
    if matches!(ctx.fns.expr_types.get(&e.span), Some(Ty::Str)) {
        return true;
    }
    match &e.kind {
        ExprKind::Lit(l) => matches!(l.kind, Lit::Str(_)),
        ExprKind::Binary {
            op: BinOp::Concat, ..
        } => true,
        ExprKind::Binary {
            op: BinOp::Add,
            lhs,
            rhs,
        } => is_str_expr(lhs, ctx) || is_str_expr(rhs, ctx),
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
                    if let ExprKind::Ident(n) = &callee.kind {
                        Some(n.as_str())
                    } else {
                        None
                    }
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

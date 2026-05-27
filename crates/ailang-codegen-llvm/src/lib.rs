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
use ailang_syntax::token::Span;
use std::fmt::Write;

pub fn emit_c(resolved: &ResolvedModule) -> String {
    let mut out = String::new();
    out.push_str(PRELUDE);
    out.push('\n');

    let module = &resolved.module;

    // ----- Struct typedefs (must precede anything that references them) -----
    let mut struct_decls: Vec<&StructDecl> = Vec::new();
    for item in &module.items {
        if let Item::Struct(s) = item {
            emit_struct_typedef(&mut out, s);
            struct_decls.push(s);
        }
    }
    // ----- Enum typedefs + per-variant constructors. -----
    for item in &module.items {
        if let Item::Enum(e) = item {
            emit_enum_typedef(&mut out, e);
            for v in &e.variants {
                emit_enum_ctor(&mut out, e, v);
            }
        }
    }
    out.push('\n');

    // ----- Per-struct pretty-printers + extend `print`/`println` _Generic. -----
    for s in &struct_decls {
        emit_struct_printer(&mut out, s);
    }
    if !struct_decls.is_empty() {
        emit_extended_print_macros(&mut out, &struct_decls);
    }

    // ----- Lambda lifting -----
    // Walk every function body, find lambda expressions, synthesize a
    // top-level FnDecl per lambda, and record the generated name keyed by
    // the lambda's span so emit_expr can substitute it later.
    let lambda_table = collect_lambdas(module, resolved);

    // Emit env-struct typedefs for capturing lambdas before any code that
    // might construct them.
    for (_, info) in &lambda_table {
        if !info.captures.is_empty() {
            let _ = write!(out, "typedef struct {{\n");
            for (n, t) in &info.captures {
                let _ = write!(out, "    {};\n", c_decl(&c_safe_name(n), t));
            }
            let _ = write!(out, "}} {};\n", info.env_name);
        }
    }
    out.push('\n');

    // ----- Generic instantiation discovery -----
    // For each `fn name<T, …>` declared in the module, scan every call
    // site to collect the concrete type-tuples used. Generates one C
    // function per (fn, type-tuple), with a dispatch `#define` that
    // routes user-level `name(x)` to the right instance via `_Generic`.
    let generic_instances = collect_generic_instances(module, resolved);

    // ----- Forward declarations for all user functions + lifted lambdas -----
    for item in &module.items {
        if let Item::Fn(f) = item {
            // Generic fns are emitted only as specialized instances below.
            if !f.type_params.is_empty() { continue; }
            emit_fn_signature(&mut out, f, resolved);
            out.push_str(";\n");
        }
    }
    // Forward-declare every generic instance.
    for (g_name, types_set) in &generic_instances {
        if let Some(Item::Fn(f)) = module.items.iter().find(|i| matches!(i, Item::Fn(f) if f.name.name == *g_name)) {
            for type_args in types_set {
                let instance = substitute_fn_decl(f, type_args);
                emit_fn_signature(&mut out, &instance, resolved);
                out.push_str(";\n");
            }
        }
    }
    for (_, info) in &lambda_table {
        emit_lambda_signature(&mut out, info);
        out.push_str(";\n");
    }
    // Dispatch macros for generic fns.
    for (g_name, types_set) in &generic_instances {
        if let Some(Item::Fn(f)) = module.items.iter().find(|i| matches!(i, Item::Fn(f) if f.name.name == *g_name)) {
            emit_generic_dispatch_macro(&mut out, f, types_set);
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

    // ----- Lifted lambda bodies + function bodies -----
    let ctx = EmitCtx {
        fns: resolved,
        lambdas: &lambda_table,
        current_ret_ty: std::cell::RefCell::new("int64_t".to_string()),
    };
    for (_, info) in &lambda_table {
        emit_lambda_body(&mut out, info, &ctx);
        out.push('\n');
    }
    for item in &module.items {
        if let Item::Fn(f) = item {
            if !f.type_params.is_empty() { continue; }
            emit_fn(&mut out, f, resolved, &ctx);
            out.push('\n');
        }
    }
    // Emit a specialized body per (generic fn, type-tuple).
    for (g_name, types_set) in &generic_instances {
        if let Some(Item::Fn(f)) = module.items.iter().find(|i| matches!(i, Item::Fn(f) if f.name.name == *g_name)) {
            for type_args in types_set {
                let instance = substitute_fn_decl(f, type_args);
                emit_fn(&mut out, &instance, resolved, &ctx);
                out.push('\n');
            }
        }
    }

    out
}

// ---------------------------------------------------------------------------
// Generic instantiation
// ---------------------------------------------------------------------------

/// (generic-fn name) → fn decl + set of concrete type-arg tuples seen.
/// Stores the FnDecl alongside so unification can use the param types.
fn collect_generic_instances<'a>(
    m: &'a Module,
    resolved: &ResolvedModule,
) -> std::collections::BTreeMap<String, std::collections::BTreeSet<Vec<Ty>>> {
    let mut out: std::collections::BTreeMap<String, std::collections::BTreeSet<Vec<Ty>>>
        = std::collections::BTreeMap::new();
    // Map of generic fn name → its FnDecl (for unification).
    let generic_fns: std::collections::HashMap<&String, &'a FnDecl> = m
        .items
        .iter()
        .filter_map(|i| match i {
            Item::Fn(f) if !f.type_params.is_empty() => Some((&f.name.name, f)),
            _ => None,
        })
        .collect();
    if generic_fns.is_empty() { return out; }
    let generic_names: std::collections::HashSet<String> =
        generic_fns.keys().map(|s| (*s).clone()).collect();
    for item in &m.items {
        if let Item::Fn(f) = item {
            collect_calls_block(&f.body, &generic_names, &generic_fns, resolved, &mut out);
        }
    }
    out
}

fn collect_calls_block(
    b: &Block,
    generic_names: &std::collections::HashSet<String>,
    generic_fns: &std::collections::HashMap<&String, &FnDecl>,
    res: &ResolvedModule,
    out: &mut std::collections::BTreeMap<String, std::collections::BTreeSet<Vec<Ty>>>,
) {
    for s in &b.stmts { collect_calls_stmt(s, generic_names, generic_fns, res, out); }
    if let Some(t) = &b.tail_expr { collect_calls_expr(t, generic_names, generic_fns, res, out); }
}
fn collect_calls_stmt(
    s: &Stmt,
    generic_names: &std::collections::HashSet<String>,
    generic_fns: &std::collections::HashMap<&String, &FnDecl>,
    res: &ResolvedModule,
    out: &mut std::collections::BTreeMap<String, std::collections::BTreeSet<Vec<Ty>>>,
) {
    match s {
        Stmt::Decl { value, .. } => collect_calls_expr(value, generic_names, generic_fns, res, out),
        Stmt::Assign { target, value, .. } => { collect_calls_expr(target, generic_names, generic_fns, res, out); collect_calls_expr(value, generic_names, generic_fns, res, out); }
        Stmt::Expr(e) => collect_calls_expr(e, generic_names, generic_fns, res, out),
        Stmt::Return { value: Some(e), .. } => collect_calls_expr(e, generic_names, generic_fns, res, out),
        Stmt::If(i) => {
            collect_calls_expr(&i.cond, generic_names, generic_fns, res, out);
            collect_calls_block(&i.then_branch, generic_names, generic_fns, res, out);
            match &i.else_branch {
                Some(ElseBranch::Block(b)) => collect_calls_block(b, generic_names, generic_fns, res, out),
                Some(ElseBranch::If(inner)) => collect_calls_stmt(&Stmt::If((**inner).clone()), generic_names, generic_fns, res, out),
                None => {}
            }
        }
        Stmt::Loop(l) => {
            match &l.head {
                Some(LoopHead::ForIn { iter, .. }) => collect_calls_expr(iter, generic_names, generic_fns, res, out),
                Some(LoopHead::While(c)) => collect_calls_expr(c, generic_names, generic_fns, res, out),
                None => {}
            }
            collect_calls_block(&l.body, generic_names, generic_fns, res, out);
        }
        Stmt::Match(m) => {
            collect_calls_expr(&m.scrutinee, generic_names, generic_fns, res, out);
            for arm in &m.arms { collect_calls_expr(&arm.body, generic_names, generic_fns, res, out); }
        }
        _ => {}
    }
}
fn collect_calls_expr(
    e: &Expr,
    generic_names: &std::collections::HashSet<String>,
    generic_fns: &std::collections::HashMap<&String, &FnDecl>,
    res: &ResolvedModule,
    out: &mut std::collections::BTreeMap<String, std::collections::BTreeSet<Vec<Ty>>>,
) {
    if let ExprKind::Call { callee, args } = &e.kind {
        if let ExprKind::Ident(name) = &callee.kind {
            if generic_names.contains(name) {
                if let Some(f) = generic_fns.get(name) {
                    let type_var_set: std::collections::HashSet<String> =
                        f.type_params.iter().map(|p| p.name.clone()).collect();
                    let mut subst: std::collections::HashMap<String, Ty> =
                        std::collections::HashMap::new();
                    // Unify each (param ty, arg ty) pair.
                    for (i, param) in f.params.iter().enumerate() {
                        if let Some(pt) = &param.ty {
                            let arg_ty = args.get(i)
                                .and_then(|a| res.expr_types.get(&a.span))
                                .cloned()
                                .unwrap_or(Ty::Unknown);
                            unify_ty(pt, &arg_ty, &type_var_set, &mut subst);
                        }
                    }
                    // Build the concrete type-tuple in declaration order.
                    let type_args: Vec<Ty> = f.type_params.iter()
                        .map(|tp| subst.get(&tp.name).cloned().unwrap_or(Ty::I64))
                        .collect();
                    // Only register the instance if every type var resolved
                    // to a concrete primitive — otherwise we'd emit two
                    // instances mangled the same way.
                    if type_args.iter().all(|t| is_primitive_ty(t)) {
                        out.entry(name.clone()).or_default().insert(type_args);
                    }
                }
            }
        }
        collect_calls_expr(callee, generic_names, generic_fns, res, out);
        for a in args { collect_calls_expr(a, generic_names, generic_fns, res, out); }
        return;
    }
    match &e.kind {
        ExprKind::Index { container, index } => { collect_calls_expr(container, generic_names, generic_fns, res, out); collect_calls_expr(index, generic_names, generic_fns, res, out); }
        ExprKind::Field { container, .. } => collect_calls_expr(container, generic_names, generic_fns, res, out),
        ExprKind::Binary { lhs, rhs, .. } => { collect_calls_expr(lhs, generic_names, generic_fns, res, out); collect_calls_expr(rhs, generic_names, generic_fns, res, out); }
        ExprKind::Unary { operand, .. } => collect_calls_expr(operand, generic_names, generic_fns, res, out),
        ExprKind::Ternary { cond, then_, else_ } => { collect_calls_expr(cond, generic_names, generic_fns, res, out); collect_calls_expr(then_, generic_names, generic_fns, res, out); collect_calls_expr(else_, generic_names, generic_fns, res, out); }
        ExprKind::Pipe { lhs, rhs } => { collect_calls_expr(lhs, generic_names, generic_fns, res, out); collect_calls_expr(rhs, generic_names, generic_fns, res, out); }
        ExprKind::Array(xs) => for x in xs { collect_calls_expr(x, generic_names, generic_fns, res, out); },
        ExprKind::Map(es) => for (k, v) in es { collect_calls_expr(k, generic_names, generic_fns, res, out); collect_calls_expr(v, generic_names, generic_fns, res, out); },
        ExprKind::Tuple(xs) => for x in xs { collect_calls_expr(x, generic_names, generic_fns, res, out); },
        ExprKind::StructLit { fields, .. } => for (_, v) in fields { collect_calls_expr(v, generic_names, generic_fns, res, out); },
        ExprKind::Block(b) => collect_calls_block(b, generic_names, generic_fns, res, out),
        ExprKind::Try(inner) => collect_calls_expr(inner, generic_names, generic_fns, res, out),
        ExprKind::Lambda { body, .. } => match body {
            LambdaBody::Expr(inner) => collect_calls_expr(inner, generic_names, generic_fns, res, out),
            LambdaBody::Block(b) => collect_calls_block(b, generic_names, generic_fns, res, out),
        },
        _ => {}
    }
}

fn is_primitive_ty(t: &Ty) -> bool {
    matches!(t, Ty::I64 | Ty::F64 | Ty::Bool | Ty::Str)
}

/// One-shot unification: walks the param AST type alongside the arg Ty,
/// recording type-var → concrete-type bindings. Mismatched shapes are
/// ignored (caller decides what to do with an incomplete subst).
fn unify_ty(
    param_ty: &Type,
    arg_ty: &Ty,
    type_vars: &std::collections::HashSet<String>,
    subst: &mut std::collections::HashMap<String, Ty>,
) {
    match (&param_ty.kind, arg_ty) {
        (TypeKind::Path(name), _) if type_vars.contains(name) => {
            subst.entry(name.clone()).or_insert_with(|| arg_ty.clone());
        }
        (TypeKind::Array(inner), Ty::Array(arg_elem)) => {
            unify_ty(inner, arg_elem, type_vars, subst);
        }
        (TypeKind::Map(kp, vp), Ty::Map(ka, va)) => {
            unify_ty(kp, ka, type_vars, subst);
            unify_ty(vp, va, type_vars, subst);
        }
        _ => {}
    }
}

/// Substitute the type parameters in an FnDecl with concrete types.
/// Returns a fresh FnDecl with mangled name (e.g. `id_i64`, `id_str`).
fn substitute_fn_decl(f: &FnDecl, type_args: &[Ty]) -> FnDecl {
    let subst: std::collections::HashMap<String, Ty> = f.type_params
        .iter()
        .zip(type_args.iter().cloned())
        .map(|(p, t)| (p.name.clone(), t))
        .collect();
    let mangled = format!("{}_{}", f.name.name, mangle_ty_suffix(type_args));
    FnDecl {
        name: Ident { name: mangled, span: f.name.span },
        type_params: Vec::new(),
        params: f.params.iter().map(|p| Param {
            name: p.name.clone(),
            ty: p.ty.as_ref().map(|t| substitute_ty(t, &subst)),
            span: p.span,
        }).collect(),
        return_ty: f.return_ty.as_ref().map(|t| substitute_ty(t, &subst)),
        body: f.body.clone(),
        span: f.span,
    }
}

fn substitute_ty(t: &Type, subst: &std::collections::HashMap<String, Ty>) -> Type {
    let kind = match &t.kind {
        TypeKind::Path(name) => match subst.get(name) {
            Some(ty) => TypeKind::Path(ty_to_path_name(ty).to_string()),
            None => TypeKind::Path(name.clone()),
        },
        TypeKind::Array(inner) => TypeKind::Array(Box::new(substitute_ty(inner, subst))),
        TypeKind::Map(k, v) => TypeKind::Map(Box::new(substitute_ty(k, subst)), Box::new(substitute_ty(v, subst))),
        TypeKind::Ptr(inner) => TypeKind::Ptr(Box::new(substitute_ty(inner, subst))),
        TypeKind::Optional(inner) => TypeKind::Optional(Box::new(substitute_ty(inner, subst))),
        TypeKind::Result(inner) => TypeKind::Result(Box::new(substitute_ty(inner, subst))),
        TypeKind::Fn { params, ret } => TypeKind::Fn {
            params: params.iter().map(|p| substitute_ty(p, subst)).collect(),
            ret: ret.as_ref().map(|r| Box::new(substitute_ty(r, subst))),
        },
    };
    Type { kind, span: t.span }
}

fn ty_to_path_name(t: &Ty) -> &'static str {
    match t {
        Ty::I64 => "i64",
        Ty::F64 => "f64",
        Ty::Bool => "bool",
        Ty::Str => "str",
        _ => "i64",
    }
}

fn mangle_ty_suffix(types: &[Ty]) -> String {
    types.iter().map(|t| ty_to_path_name(t)).collect::<Vec<_>>().join("_")
}

/// Emit `#define name(x) _Generic((x), TY: name_TY, …)(x)` for one generic fn.
/// Dispatch is on the *first param*'s substituted C type — not on `T`
/// directly — so `first<T>(arr:[T])` works (dispatches on `ailang_arr_i64`
/// / `ailang_arr_str` instead of trying `int64_t` / `const char*`).
fn emit_generic_dispatch_macro(
    out: &mut String,
    f: &FnDecl,
    types_set: &std::collections::BTreeSet<Vec<Ty>>,
) {
    let name = &f.name.name;
    let _ = write!(out, "\n#define {name}(__x) _Generic((__x)");
    for type_args in types_set {
        let suffix = mangle_ty_suffix(type_args);
        // Substitute T → type_args into the first param's AST type, then
        // lower to a C type spelling.
        let subst: std::collections::HashMap<String, Ty> = f.type_params
            .iter()
            .zip(type_args.iter().cloned())
            .map(|(p, t)| (p.name.clone(), t))
            .collect();
        let first_param_c_ty = f.params.first()
            .and_then(|p| p.ty.as_ref())
            .map(|t| c_ty_from_ast(&substitute_ty(t, &subst)))
            .unwrap_or_else(|| "int64_t".to_string());
        let _ = write!(out, ", \\\n    {first_param_c_ty}: {name}_{suffix}");
        // String literals lex as `char*`; alias to the `const char*` arm.
        if first_param_c_ty == "const char*" {
            let _ = write!(out, ", \\\n    char*: {name}_{suffix}");
        }
    }
    let _ = write!(out, ")(__x)\n");
}

/// Per-lambda info: the unique C name, params/body (cloned from AST), and
/// the analyzed capture list — names from the enclosing scope that the
/// body references. Each capture lands in a generated env struct allocated
/// on the GC heap at lambda-construction time.
struct LambdaInfo {
    c_name: String,
    env_name: String,    // typedef name for the env struct (only emitted if non-empty)
    params: Vec<Param>,
    body: LambdaBody,
    captures: Vec<(String, String)>, // (var name, C type) pairs
    /// C type spelling for the lambda's return — derived from sema's
    /// expr_types on the body's tail expression. Defaults to int64_t.
    return_c_ty: String,
    #[allow(dead_code)]
    span: Span,
}

fn collect_lambdas(m: &Module, resolved: &ResolvedModule) -> Vec<(Span, LambdaInfo)> {
    let mut out: Vec<(Span, LambdaInfo)> = Vec::new();
    for item in &m.items {
        if let Item::Fn(f) = item {
            // Each fn starts with its params in scope.
            let mut bound: std::collections::HashSet<String> =
                f.params.iter().map(|p| p.name.name.clone()).collect();
            walk_block_for_lambdas(&f.body, &mut bound, &mut out, resolved);
        }
    }
    out
}

fn walk_block_for_lambdas(
    b: &Block,
    bound: &mut std::collections::HashSet<String>,
    out: &mut Vec<(Span, LambdaInfo)>,
    res: &ResolvedModule,
) {
    let snapshot = bound.clone();
    for s in &b.stmts {
        walk_stmt_for_lambdas(s, bound, out, res);
    }
    if let Some(t) = &b.tail_expr {
        walk_expr_for_lambdas(t, bound, out, res);
    }
    *bound = snapshot;
}

fn walk_stmt_for_lambdas(
    s: &Stmt,
    bound: &mut std::collections::HashSet<String>,
    out: &mut Vec<(Span, LambdaInfo)>,
    res: &ResolvedModule,
) {
    match s {
        Stmt::Decl { name, value, .. } => {
            // The RHS is evaluated *before* `name` is in scope.
            walk_expr_for_lambdas(value, bound, out, res);
            bound.insert(name.name.clone());
        }
        Stmt::Assign { target, value, .. } => {
            walk_expr_for_lambdas(target, bound, out, res);
            walk_expr_for_lambdas(value, bound, out, res);
        }
        Stmt::Expr(e) => walk_expr_for_lambdas(e, bound, out, res),
        Stmt::Return { value: Some(e), .. } => walk_expr_for_lambdas(e, bound, out, res),
        Stmt::If(i) => {
            walk_expr_for_lambdas(&i.cond, bound, out, res);
            walk_block_for_lambdas(&i.then_branch, bound, out, res);
            match &i.else_branch {
                Some(ElseBranch::Block(b)) => walk_block_for_lambdas(b, bound, out, res),
                Some(ElseBranch::If(inner)) => walk_stmt_for_lambdas(&Stmt::If((**inner).clone()), bound, out, res),
                None => {}
            }
        }
        Stmt::Loop(l) => {
            match &l.head {
                Some(LoopHead::ForIn { vars, iter }) => {
                    walk_expr_for_lambdas(iter, bound, out, res);
                    let saved = bound.clone();
                    for v in vars { bound.insert(v.name.clone()); }
                    walk_block_for_lambdas(&l.body, bound, out, res);
                    *bound = saved;
                    return;
                }
                Some(LoopHead::While(c)) => walk_expr_for_lambdas(c, bound, out, res),
                None => {}
            }
            walk_block_for_lambdas(&l.body, bound, out, res);
        }
        Stmt::Match(m) => {
            walk_expr_for_lambdas(&m.scrutinee, bound, out, res);
            for arm in &m.arms {
                walk_expr_for_lambdas(&arm.body, bound, out, res);
            }
        }
        _ => {}
    }
}

fn walk_expr_for_lambdas(
    e: &Expr,
    bound: &mut std::collections::HashSet<String>,
    out: &mut Vec<(Span, LambdaInfo)>,
    res: &ResolvedModule,
) {
    match &e.kind {
        ExprKind::Lambda { params, body } => {
            let c_name = format!("__ail_lam_{}", e.span.start);
            let env_name = format!("__ail_env_{}", e.span.start);

            // Captures = names referenced in the body that are bound in the
            // enclosing scope (i.e., `bound` at lambda-creation time) and
            // are NOT params of this lambda. Top-level fn names and builtins
            // are explicitly excluded.
            let outer = bound.clone();
            let param_names: std::collections::HashSet<String> =
                params.iter().map(|p| p.name.name.clone()).collect();
            let referenced = referenced_names_in_body(body);
            let mut captures_set = std::collections::BTreeSet::new();
            for n in &referenced {
                if param_names.contains(n) { continue; }
                if res.fn_table.contains_key(n) { continue; }
                if outer.contains(n) {
                    captures_set.insert(n.clone());
                }
            }
            // Resolve each capture's C type from sema's expr_types (find any
            // use of this name inside the body and read its type). Falls
            // back to int64_t if sema didn't record a type.
            let captures: Vec<(String, String)> = captures_set
                .into_iter()
                .map(|n| {
                    let ty = type_of_ident_in(&n, body, &res.expr_types)
                        .unwrap_or(Ty::I64);
                    (n, c_ty_for(&ty))
                })
                .collect();

            // Recurse into the body first so nested lambdas get lifted too.
            // Inside the lambda, only its params (plus anything decl'd in
            // the body) are in scope — the outer bindings are reachable
            // through the env but not "bound" from the perspective of
            // further nested lambdas' capture analysis.
            let mut inner_bound = param_names.clone();
            match body {
                LambdaBody::Expr(inner) => walk_expr_for_lambdas(inner, &mut inner_bound, out, res),
                LambdaBody::Block(b) => walk_block_for_lambdas(b, &mut inner_bound, out, res),
            }

            // Lambda's return type — sema already recorded the whole
            // Lambda expression's type (its body's tail-expr type), so we
            // just read it from the side table.
            let return_c_ty = res.expr_types.get(&e.span)
                .map(|t| c_ty_for(t))
                .unwrap_or_else(|| "int64_t".to_string());

            out.push((e.span, LambdaInfo {
                c_name,
                env_name,
                params: params.clone(),
                body: body.clone(),
                captures,
                return_c_ty,
                span: e.span,
            }));
        }
        ExprKind::Call { callee, args } => {
            walk_expr_for_lambdas(callee, bound, out, res);
            for a in args { walk_expr_for_lambdas(a, bound, out, res); }
        }
        ExprKind::Index { container, index } => {
            walk_expr_for_lambdas(container, bound, out, res);
            walk_expr_for_lambdas(index, bound, out, res);
        }
        ExprKind::Field { container, .. } => walk_expr_for_lambdas(container, bound, out, res),
        ExprKind::Binary { lhs, rhs, .. } => {
            walk_expr_for_lambdas(lhs, bound, out, res);
            walk_expr_for_lambdas(rhs, bound, out, res);
        }
        ExprKind::Unary { operand, .. } => walk_expr_for_lambdas(operand, bound, out, res),
        ExprKind::Ternary { cond, then_, else_ } => {
            walk_expr_for_lambdas(cond, bound, out, res);
            walk_expr_for_lambdas(then_, bound, out, res);
            walk_expr_for_lambdas(else_, bound, out, res);
        }
        ExprKind::Pipe { lhs, rhs } => {
            walk_expr_for_lambdas(lhs, bound, out, res);
            walk_expr_for_lambdas(rhs, bound, out, res);
        }
        ExprKind::Array(xs) => { for x in xs { walk_expr_for_lambdas(x, bound, out, res); } }
        ExprKind::Map(es) => { for (k, v) in es { walk_expr_for_lambdas(k, bound, out, res); walk_expr_for_lambdas(v, bound, out, res); } }
        ExprKind::Tuple(xs) => { for x in xs { walk_expr_for_lambdas(x, bound, out, res); } }
        ExprKind::StructLit { fields, .. } => { for (_, v) in fields { walk_expr_for_lambdas(v, bound, out, res); } }
        ExprKind::Block(b) => walk_block_for_lambdas(b, bound, out, res),
        ExprKind::Try(inner) => walk_expr_for_lambdas(inner, bound, out, res),
        _ => {}
    }
}

/// Collect every identifier name that appears as a value-position `Ident`
/// inside a lambda body. Used by capture analysis.
fn referenced_names_in_body(b: &LambdaBody) -> std::collections::HashSet<String> {
    let mut out = std::collections::HashSet::new();
    match b {
        LambdaBody::Expr(e) => collect_refs(e, &mut out),
        LambdaBody::Block(blk) => collect_refs_block(blk, &mut out),
    }
    out
}
fn collect_refs_block(b: &Block, out: &mut std::collections::HashSet<String>) {
    for s in &b.stmts {
        match s {
            Stmt::Decl { value, .. } => collect_refs(value, out),
            Stmt::Expr(e) => collect_refs(e, out),
            Stmt::Assign { target, value, .. } => { collect_refs(target, out); collect_refs(value, out); }
            Stmt::Return { value: Some(e), .. } => collect_refs(e, out),
            Stmt::If(i) => {
                collect_refs(&i.cond, out);
                collect_refs_block(&i.then_branch, out);
                match &i.else_branch {
                    Some(ElseBranch::Block(b)) => collect_refs_block(b, out),
                    Some(ElseBranch::If(inner)) => collect_refs_stmt(&Stmt::If((**inner).clone()), out),
                    None => {}
                }
            }
            Stmt::Loop(l) => {
                match &l.head {
                    Some(LoopHead::ForIn { iter, .. }) => collect_refs(iter, out),
                    Some(LoopHead::While(c)) => collect_refs(c, out),
                    None => {}
                }
                collect_refs_block(&l.body, out);
            }
            Stmt::Match(m) => {
                collect_refs(&m.scrutinee, out);
                for arm in &m.arms { collect_refs(&arm.body, out); }
            }
            _ => {}
        }
    }
    if let Some(t) = &b.tail_expr { collect_refs(t, out); }
}
fn collect_refs_stmt(s: &Stmt, out: &mut std::collections::HashSet<String>) {
    let mut tmp = Block { stmts: vec![s.clone()], tail_expr: None, span: Span::empty() };
    collect_refs_block(&mut tmp, out);
}
fn collect_refs(e: &Expr, out: &mut std::collections::HashSet<String>) {
    match &e.kind {
        ExprKind::Ident(n) => { out.insert(n.clone()); }
        ExprKind::Call { callee, args } => {
            collect_refs(callee, out);
            for a in args { collect_refs(a, out); }
        }
        ExprKind::Index { container, index } => { collect_refs(container, out); collect_refs(index, out); }
        ExprKind::Field { container, .. } => collect_refs(container, out),
        ExprKind::Binary { lhs, rhs, .. } => { collect_refs(lhs, out); collect_refs(rhs, out); }
        ExprKind::Unary { operand, .. } => collect_refs(operand, out),
        ExprKind::Ternary { cond, then_, else_ } => { collect_refs(cond, out); collect_refs(then_, out); collect_refs(else_, out); }
        ExprKind::Pipe { lhs, rhs } => { collect_refs(lhs, out); collect_refs(rhs, out); }
        ExprKind::Lambda { body, .. } => {
            // Nested lambdas: their body's references also reach up to us.
            match body {
                LambdaBody::Expr(inner) => collect_refs(inner, out),
                LambdaBody::Block(b) => collect_refs_block(b, out),
            }
        }
        ExprKind::Array(xs) => for x in xs { collect_refs(x, out); },
        ExprKind::Map(es) => for (k, v) in es { collect_refs(k, out); collect_refs(v, out); },
        ExprKind::Tuple(xs) => for x in xs { collect_refs(x, out); },
        ExprKind::StructLit { fields, .. } => for (_, v) in fields { collect_refs(v, out); },
        ExprKind::Block(b) => collect_refs_block(b, out),
        ExprKind::Try(inner) => collect_refs(inner, out),
        _ => {}
    }
}

/// Look up the type of an `Ident(name)` reference somewhere in the lambda
/// body, by consulting sema's expr_types side table.
fn type_of_ident_in(name: &str, body: &LambdaBody, et: &std::collections::HashMap<Span, Ty>) -> Option<Ty> {
    fn walk_expr(name: &str, e: &Expr, et: &std::collections::HashMap<Span, Ty>) -> Option<Ty> {
        if let ExprKind::Ident(n) = &e.kind {
            if n == name { return et.get(&e.span).cloned(); }
        }
        match &e.kind {
            ExprKind::Call { callee, args } => {
                walk_expr(name, callee, et).or_else(|| args.iter().find_map(|a| walk_expr(name, a, et)))
            }
            ExprKind::Index { container, index } => walk_expr(name, container, et).or_else(|| walk_expr(name, index, et)),
            ExprKind::Field { container, .. } => walk_expr(name, container, et),
            ExprKind::Binary { lhs, rhs, .. } => walk_expr(name, lhs, et).or_else(|| walk_expr(name, rhs, et)),
            ExprKind::Unary { operand, .. } => walk_expr(name, operand, et),
            ExprKind::Ternary { cond, then_, else_ } => {
                walk_expr(name, cond, et).or_else(|| walk_expr(name, then_, et)).or_else(|| walk_expr(name, else_, et))
            }
            ExprKind::Pipe { lhs, rhs } => walk_expr(name, lhs, et).or_else(|| walk_expr(name, rhs, et)),
            ExprKind::Array(xs) => xs.iter().find_map(|x| walk_expr(name, x, et)),
            ExprKind::Map(es) => es.iter().find_map(|(k, v)| walk_expr(name, k, et).or_else(|| walk_expr(name, v, et))),
            ExprKind::Tuple(xs) => xs.iter().find_map(|x| walk_expr(name, x, et)),
            ExprKind::StructLit { fields, .. } => fields.iter().find_map(|(_, v)| walk_expr(name, v, et)),
            ExprKind::Block(b) => walk_block(name, b, et),
            ExprKind::Try(inner) => walk_expr(name, inner, et),
            ExprKind::Lambda { body, .. } => match body {
                LambdaBody::Expr(inner) => walk_expr(name, inner, et),
                LambdaBody::Block(b) => walk_block(name, b, et),
            },
            _ => None,
        }
    }
    fn walk_block(name: &str, b: &Block, et: &std::collections::HashMap<Span, Ty>) -> Option<Ty> {
        for s in &b.stmts {
            if let Some(t) = walk_stmt(name, s, et) { return Some(t); }
        }
        b.tail_expr.as_ref().and_then(|e| walk_expr(name, e, et))
    }
    fn walk_stmt(name: &str, s: &Stmt, et: &std::collections::HashMap<Span, Ty>) -> Option<Ty> {
        match s {
            Stmt::Decl { value, .. } => walk_expr(name, value, et),
            Stmt::Assign { target, value, .. } => walk_expr(name, target, et).or_else(|| walk_expr(name, value, et)),
            Stmt::Expr(e) => walk_expr(name, e, et),
            Stmt::Return { value: Some(e), .. } => walk_expr(name, e, et),
            Stmt::If(i) => walk_expr(name, &i.cond, et).or_else(|| walk_block(name, &i.then_branch, et)),
            Stmt::Loop(l) => match &l.head {
                Some(LoopHead::ForIn { iter, .. }) => walk_expr(name, iter, et).or_else(|| walk_block(name, &l.body, et)),
                Some(LoopHead::While(c)) => walk_expr(name, c, et).or_else(|| walk_block(name, &l.body, et)),
                None => walk_block(name, &l.body, et),
            },
            Stmt::Match(m) => walk_expr(name, &m.scrutinee, et)
                .or_else(|| m.arms.iter().find_map(|a| walk_expr(name, &a.body, et))),
            _ => None,
        }
    }
    match body {
        LambdaBody::Expr(e) => walk_expr(name, e, et),
        LambdaBody::Block(b) => walk_block(name, b, et),
    }
}

fn emit_lambda_signature(out: &mut String, info: &LambdaInfo) {
    // Every lifted lambda takes `void* __env_` as its first parameter so
    // the caller's `ailang_closure_t.env` plugs in uniformly. The return
    // type comes from the body's tail expression (sema records it).
    let _ = write!(out, "static {} {}(void* __env_", info.return_c_ty, info.c_name);
    for p in info.params.iter() {
        out.push_str(", ");
        let ty = c_ty_for_param(p.ty.as_ref());
        out.push_str(&c_decl(&c_safe_name(&p.name.name), &ty));
    }
    out.push(')');
}

fn emit_lambda_body(out: &mut String, info: &LambdaInfo, ctx: &EmitCtx) {
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

/// `en Name { V1, V2(f:T), … }` →
/// ```c
/// typedef struct {
///   int __tag;
///   union { struct { } V1; struct { T f; } V2; ... } __data;
/// } Name;
/// ```
fn emit_enum_typedef(out: &mut String, e: &EnumDecl) {
    let name = c_safe_name(&e.name.name);
    let _ = write!(out, "typedef struct {{\n    int __tag;\n    union {{\n");
    for v in &e.variants {
        let vname = c_safe_name(&v.name.name);
        let _ = write!(out, "        struct {{");
        for f in &v.fields {
            let cty = c_ty_from_ast(&f.ty);
            let _ = write!(out, " {}; ", c_decl(&c_safe_name(&f.name.name), &cty));
        }
        let _ = write!(out, "}} {};\n", vname);
    }
    let _ = write!(out, "    }} __data;\n}} {};\n", name);
}

fn emit_enum_ctor(out: &mut String, e: &EnumDecl, v: &EnumVariant) {
    let en_name = c_safe_name(&e.name.name);
    let v_name = c_safe_name(&v.name.name);
    let tag = e.variants.iter().position(|x| x.name.name == v.name.name).unwrap_or(0);
    let _ = write!(out, "static inline {en_name} {en_name}_{v_name}(");
    if v.fields.is_empty() {
        out.push_str("void");
    } else {
        for (i, f) in v.fields.iter().enumerate() {
            if i > 0 { out.push_str(", "); }
            let cty = c_ty_from_ast(&f.ty);
            out.push_str(&c_decl(&c_safe_name(&f.name.name), &cty));
        }
    }
    let _ = write!(out, ") {{\n    {en_name} __v;\n    __v.__tag = {tag};\n");
    for f in &v.fields {
        let fn_ = c_safe_name(&f.name.name);
        let _ = write!(out, "    __v.__data.{v_name}.{fn_} = {fn_};\n");
    }
    out.push_str("    return __v;\n}\n");
}

fn emit_struct_typedef(out: &mut String, s: &StructDecl) {
    let name = c_safe_name(&s.name.name);
    let _ = write!(out, "typedef struct {{\n");
    for f in &s.fields {
        let cty = c_ty_from_ast(&f.ty);
        let _ = write!(out, "    {} {};\n", cty, c_safe_name(&f.name.name));
    }
    let _ = write!(out, "}} {};\n", name);
}

/// Emit `static void ailang_print_<Name>(<Name> v)` + `ailang_println_<Name>`.
/// Each known scalar field type maps to its existing print helper; unknown
/// field shapes fall back to `printf("?")` for v1.
fn emit_struct_printer(out: &mut String, s: &StructDecl) {
    let name = c_safe_name(&s.name.name);
    let _ = write!(out, "static void ailang_print_{}({} __v) {{\n", name, name);
    let _ = write!(out, "    printf(\"{}\" \"{{\");\n", name);
    for (i, f) in s.fields.iter().enumerate() {
        if i > 0 {
            out.push_str("    printf(\", \");\n");
        }
        let fname = c_safe_name(&f.name.name);
        let _ = write!(out, "    printf(\"{}: \");\n", fname);
        let printer = match &f.ty.kind {
            TypeKind::Path(n) => match n.as_str() {
                "i8" | "i16" | "i32" | "i64" | "u8" | "u16" | "u32" | "u64" => {
                    "ailang_print_i64"
                }
                "f32" | "f64" => "ailang_print_f64",
                "bool" => "ailang_print_bool",
                "str" => "ailang_print_str",
                _ => "", // user struct or unknown — best-effort handling below
            },
            _ => "",
        };
        if !printer.is_empty() {
            let _ = write!(out, "    {}(__v.{});\n", printer, fname);
        } else {
            // Fallback for fields whose type we can't pretty-print yet.
            out.push_str("    printf(\"?\");\n");
        }
    }
    out.push_str("    printf(\"}\");\n");
    out.push_str("}\n");
    let _ = write!(
        out,
        "static void ailang_println_{n}({n} __v) {{ ailang_print_{n}(__v); printf(\"\\n\"); }}\n",
        n = name
    );
}

/// Re-emit `print` / `println` macros after `#undef`-ing the prelude
/// defaults, adding one `_Generic` arm per user struct so `println(p)`
/// (where `p:Point`) dispatches to the per-struct printer.
fn emit_extended_print_macros(out: &mut String, structs: &[&StructDecl]) {
    out.push_str("\n#undef print\n#undef println\n");
    // Helper to emit the `_Generic` block parameterized by the per-type
    // printer suffix (`print` vs `println`).
    let mut emit_macro = |macro_name: &str, suffix: &str| {
        let _ = write!(
            out,
            "#define {m}(x) _Generic((x), \\\n\
             \tint64_t: ailang_{s}_i64, \\\n\
             \tint: ailang_{s}_int, \\\n\
             \tdouble: ailang_{s}_f64, \\\n\
             \tbool: ailang_{s}_bool, \\\n\
             \tchar*: ailang_{s}_str, \\\n\
             \tconst char*: ailang_{s}_str, \\\n\
             \tailang_arr_i64: ailang_{s}_arr_i64, \\\n\
             \tailang_arr_str: ailang_{s}_arr_str, \\\n\
             \tailang_map_ii: ailang_{s}_map_ii, \\\n\
             \tailang_map_si: ailang_{s}_map_si, \\\n\
             \tailang_map_ss: ailang_{s}_map_ss",
            m = macro_name,
            s = suffix
        );
        for sd in structs {
            let n = c_safe_name(&sd.name.name);
            let _ = write!(out, ", \\\n\t{n}: ailang_{s}_{n}", n = n, s = suffix);
        }
        out.push_str(")(x)\n");
    };
    emit_macro("print", "print");
    emit_macro("println", "println");
    out.push('\n');
}

fn emit_fn_signature(out: &mut String, f: &FnDecl, res: &ResolvedModule) {
    // Special-case `main` so it returns `int` (C entry point), keeps its
    // name un-mangled, and always accepts `(int argc, char** argv)` so the
    // `args()` builtin can hand argv[1..] back to user code.
    let is_main = f.name.name == "main";
    let ret = if is_main {
        "int".to_string()
    } else if f.return_ty.is_some() {
        c_ty_for_ret(f.return_ty.as_ref())
    } else {
        match res.fn_table.get(&f.name.name).map(|s| &s.return_ty) {
            Some(Ty::Unit) => "void".to_string(),
            Some(t) => c_ty_for(t),
            None => "void".to_string(),
        }
    };
    let mangled = if is_main { "main".to_string() } else { c_safe_name(&f.name.name) };
    let _ = write!(out, "{} {}(", ret, mangled);
    if is_main {
        // User-level `fn main()` always has zero params (the parser doesn't
        // surface argc/argv); we shape the C signature regardless.
        out.push_str("int __argc, char** __argv");
    } else if f.params.is_empty() {
        out.push_str("void");
    } else {
        for (i, p) in f.params.iter().enumerate() {
            if i > 0 {
                out.push_str(", ");
            }
            let ty = c_ty_for_param(p.ty.as_ref());
            out.push_str(&c_decl(&c_safe_name(&p.name.name), &ty));
        }
    }
    out.push(')');
}

fn emit_fn(out: &mut String, f: &FnDecl, res: &ResolvedModule, ctx: &EmitCtx) {
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
    let returns_value = !is_main && match f.return_ty.as_ref().map(ailang_sema::ast_ty_kind_to_ty) {
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
    /// Lifted lambdas (span → C name + sig). emit_expr substitutes the name
    /// when it encounters the original Lambda expression.
    lambdas: &'a [(Span, LambdaInfo)],
    /// Current function's C return type. Threaded so `expr?` knows what
    /// shape of err-struct to propagate to.
    current_ret_ty: std::cell::RefCell<String>,
}

fn indent(out: &mut String, level: usize) {
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
fn emit_block_body(out: &mut String, block: &Block, level: usize, ctx: &EmitCtx, returns_value: bool) {
    let last_idx = block.stmts.len().saturating_sub(1);
    for (i, stmt) in block.stmts.iter().enumerate() {
        let is_last = i == last_idx && block.tail_expr.is_none();
        if returns_value && is_last {
            match stmt {
                Stmt::If(if_) => { emit_if_stmt_returning(out, if_, level, ctx); continue; }
                Stmt::Match(m) => { emit_match_stmt_returning(out, m, level, ctx); continue; }
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
fn emit_branch_block_returning(out: &mut String, b: &Block, level: usize, ctx: &EmitCtx) {
    out.push_str("{\n");
    emit_block_body(out, b, level + 1, ctx, /*returns_value=*/ true);
    indent(out, level);
    out.push('}');
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
                        (Ty::Str, _)       => "ailang_map_si_set",
                        _                  => "ailang_map_ii_set",
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
    emit_match_stmt_inner(out, mt, level, ctx, /*returning=*/ false);
}

fn emit_match_stmt_inner(out: &mut String, mt: &MatchStmt, level: usize, ctx: &EmitCtx, returning: bool) {
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
        let cty = ctx.fns.expr_types.get(&expr.span)
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
                                    if b.name == "_" { continue; }
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
        if returning { out.push_str("return "); }
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
fn pattern_cond(pat: &Pattern, var: &str, ctx: &EmitCtx) -> Option<String> {
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
fn emit_if_stmt_returning(out: &mut String, if_: &IfStmt, level: usize, ctx: &EmitCtx) {
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

fn emit_if_stmt_returning_inline(out: &mut String, if_: &IfStmt, level: usize, ctx: &EmitCtx) {
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
fn emit_match_stmt_returning(out: &mut String, m: &MatchStmt, level: usize, ctx: &EmitCtx) {
    // Reuse the normal match emitter but make each arm body a return.
    // The existing emitter takes arm bodies as expressions; if the body
    // is a value expression we wrap it with `return ...;`. We synthesize
    // that by post-processing — easier to just emit a parallel impl.
    emit_match_stmt_inner(out, m, level, ctx, /*returning=*/ true);
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
                        (Ty::Str, _)       => ("ailang_map_si", "const char*", "int64_t"),
                        _                  => ("ailang_map_ii", "int64_t", "int64_t"),
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
                    let _ = write!(out,
                        "for (int64_t {i} = 0; {i} < {m}->cap; {i}++) {{ \
                         if (!{m}->occupied[{i}]) continue; \
                         {key_ty} {k} = {m}->keys[{i}]; \
                         {val_ty} {v} = {m}->values[{i}];\n");
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

fn emit_expr(out: &mut String, e: &Expr, ctx: &EmitCtx) {
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
                        if i > 0 { out.push_str(", "); }
                        emit_expr(out, a, ctx);
                    }
                    out.push(')');
                    return;
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
                    if i > 0 { out.push_str(", "); }
                    emit_expr(out, a, ctx);
                }
                out.push(')');
            } else {
                // Indirect call through a closure. We read the callee's
                // sema type for the return shape; args still default to
                // i64. Mixed-arg closures need an explicit fn-type
                // annotation upstream — out of scope for v1.
                let ret_c = ctx.fns.expr_types.get(&callee.span)
                    .map(|t| c_ty_for(t))
                    .unwrap_or_else(|| "int64_t".to_string());
                let mut sig_params = String::from("void*");
                for _ in args { sig_params.push_str(", int64_t"); }
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
            let macro_name = if is_str { "AILANG_ARR_STR" } else { "AILANG_ARR_I64" };
            let _ = write!(out, "{}({}", macro_name, elems.len());
            for e in elems {
                out.push_str(", ");
                emit_expr(out, e, ctx);
            }
            out.push(')');
        }
        ExprKind::Map(entries) => {
            // {k1:v1, ...} → statement-expression building a fresh map and
            // setting each entry. Pick (K,V)-specific helpers by inspecting
            // the first key+value pair's static shape.
            let (ty, make_fn, set_fn) = match entries.first().map(|(k, v)| (&k.kind, &v.kind)) {
                Some((ExprKind::Lit(lk), ExprKind::Lit(lv)))
                    if matches!(lk.kind, Lit::Str(_)) && matches!(lv.kind, Lit::Str(_)) =>
                {
                    ("ailang_map_ss", "ailang_map_ss_make", "ailang_map_ss_set")
                }
                Some((ExprKind::Lit(lk), _)) if matches!(lk.kind, Lit::Str(_)) => {
                    ("ailang_map_si", "ailang_map_si_make", "ailang_map_si_set")
                }
                _ => ("ailang_map_ii", "ailang_map_ii_make", "ailang_map_ii_set"),
            };
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
                if i > 0 { out.push_str(", "); }
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
    // Sema may have recorded a type for this exact expression — trust it
    // when it says Str. Catches e.g. `lambda_param + ...` where the param
    // was annotated `:str`.
    if matches!(ctx.fns.expr_types.get(&e.span), Some(Ty::Str)) {
        return true;
    }
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
            // Anything else is taken to name a user struct. Emit verbatim;
            // if the struct wasn't actually declared, C will surface a
            // friendly "incomplete type" error.
            other => c_safe_name(other),
        },
        TypeKind::Array(inner) => match &inner.kind {
            TypeKind::Path(name) if name == "str" => "ailang_arr_str".to_string(),
            _ => "ailang_arr_i64".to_string(),
        },
        TypeKind::Map(k, v) => match (&k.kind, &v.kind) {
            (TypeKind::Path(kn), TypeKind::Path(vn)) if kn == "str" && vn == "str" => {
                "ailang_map_ss".to_string()
            }
            (TypeKind::Path(kn), _) if kn == "str" => "ailang_map_si".to_string(),
            _ => "ailang_map_ii".to_string(),
        },
        TypeKind::Ptr(inner) => format!("{}*", c_ty_from_ast(inner)),
        TypeKind::Result(inner) => match &inner.kind {
            TypeKind::Path(n) if n == "str"  => "ailang_result_str".to_string(),
            TypeKind::Path(n) if n == "bool" => "ailang_result_bool".to_string(),
            TypeKind::Path(n) if n == "f64" || n == "f32" => "ailang_result_f64".to_string(),
            _ => "ailang_result_i64".to_string(),
        },
        TypeKind::Fn { .. } => {
            // Every fn-pointer-typed value in AiLang is an
            // `ailang_closure_t` (fat pointer with captured env). Calls
            // go through the trampoline in emit_expr::Call.
            "ailang_closure_t".to_string()
        }
        _ => "int64_t".to_string(),
    }
}

fn c_ty_for_decl(ty: Option<&Type>, init: &Expr, ctx: &EmitCtx) -> String {
    if let Some(t) = ty {
        return c_ty_for(&ailang_sema::ast_ty_kind_to_ty(t));
    }
    // Trust sema's recorded type when it's a struct/enum — the syntactic
    // fallback below can't tell `Some(42)` is a `Maybe`.
    if let Some(t) = ctx.fns.expr_types.get(&init.span) {
        if matches!(t, Ty::Struct(_)) {
            return c_ty_for(t);
        }
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
        ExprKind::Map(entries) => match entries.first().map(|(k, v)| (&k.kind, &v.kind)) {
            Some((ExprKind::Lit(lk), ExprKind::Lit(lv)))
                if matches!(lk.kind, Lit::Str(_)) && matches!(lv.kind, Lit::Str(_)) => {
                    "ailang_map_ss".to_string()
            }
            Some((ExprKind::Lit(lk), _)) if matches!(lk.kind, Lit::Str(_)) => {
                "ailang_map_si".to_string()
            }
            _ => "ailang_map_ii".to_string(),
        },
        ExprKind::StructLit { name, .. } => c_safe_name(&name.name),
        ExprKind::Lambda { .. } => {
            // A lambda value is always an `ailang_closure_t` (fat pointer).
            "ailang_closure_t".to_string()
        }
        ExprKind::Binary { op: BinOp::Concat, .. } => "const char*".to_string(),
        ExprKind::Binary { op: BinOp::Add, lhs, rhs } if is_str_expr(lhs, ctx) || is_str_expr(rhs, ctx) => {
            "const char*".to_string()
        }
        ExprKind::Binary { op, .. } => match op {
            BinOp::Eq | BinOp::Ne | BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge
            | BinOp::And | BinOp::Or => "bool".to_string(),
            _ => "int64_t".to_string(),
        },
        ExprKind::Call { callee, args } => {
            if let ExprKind::Ident(name) = &callee.kind {
                // `push` / `pop` / `map` / `filter` / `sort` / `reverse` /
                // `slice` return the same array type as the first arg.
                if matches!(name.as_str(),
                    "push" | "pop" | "map" | "filter" | "sort" | "reverse" | "slice"
                ) && !args.is_empty() {
                    if let Some(t) = ctx.fns.expr_types.get(&args[0].span) {
                        return c_ty_for(t);
                    }
                }
                // `keys(m)` / `values(m)` derive their element type from
                // the map's (K,V).
                if (name == "keys" || name == "values") && !args.is_empty() {
                    if let Some(Ty::Map(k, v)) = ctx.fns.expr_types.get(&args[0].span) {
                        let elem = if name == "keys" { &**k } else { &**v };
                        return match elem {
                            Ty::Str => "ailang_arr_str".to_string(),
                            _       => "ailang_arr_i64".to_string(),
                        };
                    }
                }
                if name == "reduce" { return "int64_t".to_string(); }
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

/// Splice a variable/param name into a C type spelling. Most types use
/// `TYPE NAME`, but function-pointer types written `RET (*)(P1, ...)` need
/// the name *inside* the parens: `RET (*NAME)(P1, ...)`.
fn c_decl(name: &str, ty: &str) -> String {
    if let Some(idx) = ty.find("(*)") {
        format!("{}(*{}){}", &ty[..idx], name, &ty[idx + 3..])
    } else {
        format!("{} {}", ty, name)
    }
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
        Ty::Map(k, v) => match (&**k, &**v) {
            (Ty::Str, Ty::Str) => "ailang_map_ss".to_string(),
            (Ty::Str, _)       => "ailang_map_si".to_string(),
            _                  => "ailang_map_ii".to_string(),
        },
        Ty::Struct(name) => name.clone(),
        Ty::Result(inner) => match **inner {
            Ty::Str  => "ailang_result_str".to_string(),
            Ty::Bool => "ailang_result_bool".to_string(),
            Ty::F64  => "ailang_result_f64".to_string(),
            _        => "ailang_result_i64".to_string(),
        },
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
#include <stdarg.h>
#include <regex.h>
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

/* -------- string operations (M6++) --------
 *
 * All return GC-allocated strings (or arrays/bools). Indices are byte
 * offsets — these aren't Unicode-aware, just like libc.
 *
 * `contains/starts_with/ends_with/index_of/to_upper/to_lower/replace/
 *  trim/substring/split` are all here. `split` lives further down with
 *  the array helpers it depends on.
 */
static bool ailang_str_contains(const char* haystack, const char* needle) {
    if (!haystack || !needle) return false;
    return strstr(haystack, needle) != NULL;
}
static bool starts_with(const char* s, const char* prefix) {
    if (!s || !prefix) return false;
    size_t lp = strlen(prefix);
    return strncmp(s, prefix, lp) == 0;
}
static bool ends_with(const char* s, const char* suffix) {
    if (!s || !suffix) return false;
    size_t ls = strlen(s), lsu = strlen(suffix);
    if (lsu > ls) return false;
    return memcmp(s + ls - lsu, suffix, lsu) == 0;
}
static int64_t ailang_str_index_of(const char* haystack, const char* needle) {
    if (!haystack || !needle) return -1;
    const char* p = strstr(haystack, needle);
    return p ? (int64_t)(p - haystack) : (int64_t)-1;
}
static const char* to_upper(const char* s) {
    if (!s) return "";
    size_t n = strlen(s);
    char* out = (char*) GC_malloc_atomic(n + 1);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) s[i];
        out[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : (char) c;
    }
    out[n] = '\0';
    return out;
}
static const char* to_lower(const char* s) {
    if (!s) return "";
    size_t n = strlen(s);
    char* out = (char*) GC_malloc_atomic(n + 1);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) s[i];
        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char) c;
    }
    out[n] = '\0';
    return out;
}
static const char* trim(const char* s) {
    if (!s) return "";
    const char* p = s;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    const char* e = s + strlen(s);
    while (e > p) {
        char c = e[-1];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
        e--;
    }
    size_t n = (size_t)(e - p);
    char* out = (char*) GC_malloc_atomic(n + 1);
    memcpy(out, p, n);
    out[n] = '\0';
    return out;
}
/* `substring(s, start, end)` — bytes [start, end). Clamps to bounds. */
static const char* substring(const char* s, int64_t start, int64_t end) {
    if (!s) return "";
    int64_t n = (int64_t) strlen(s);
    if (start < 0) start = 0;
    if (end > n) end = n;
    if (end < start) end = start;
    size_t len = (size_t)(end - start);
    char* out = (char*) GC_malloc_atomic(len + 1);
    if (len) memcpy(out, s + start, len);
    out[len] = '\0';
    return out;
}
/* `replace(s, old, new)` — replace every non-overlapping occurrence. */
static const char* replace(const char* s, const char* old_, const char* new_) {
    if (!s || !old_ || !*old_) return s ? s : "";
    if (!new_) new_ = "";
    size_t lo = strlen(old_), ln = strlen(new_);
    /* Count occurrences for a single pre-sized allocation. */
    size_t count = 0;
    const char* p = s;
    while ((p = strstr(p, old_)) != NULL) { count++; p += lo; }
    size_t ls = strlen(s);
    size_t out_len = ls + count * (ln >= lo ? ln - lo : 0) - count * (lo > ln ? lo - ln : 0);
    char* out = (char*) GC_malloc_atomic(out_len + 1);
    char* w = out;
    const char* r = s;
    while ((p = strstr(r, old_)) != NULL) {
        size_t before = (size_t)(p - r);
        memcpy(w, r, before); w += before;
        memcpy(w, new_, ln); w += ln;
        r = p + lo;
    }
    size_t tail = strlen(r);
    memcpy(w, r, tail); w += tail;
    *w = '\0';
    return out;
}

/* -------- I/O + string conversion builtins (M6++) --------
 *
 * These appear as plain function names from AiLang user code (`read_file`,
 * `read_line`, `write_file`, `int_to_str`, `str_to_int`, `exit`, `args`).
 * Sema registers them so calls type-check; codegen emits the name verbatim
 * so each call lands on the C function defined here.
 *
 * All string results are GC-allocated so the caller has no manual lifetime
 * to track. Process-level state (argc/argv) is captured in `main`. */

static int    ailang_argc_ = 0;
static char** ailang_argv_ = NULL;

/* `read_file(path)` — slurp the whole file into a GC string; "" on error. */
static const char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return "";
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return ""; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return ""; }
    rewind(f);
    char* buf = (char*) GC_malloc_atomic((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

/* `write_file(path, contents)` — overwrite the file; returns true on success. */
static bool write_file(const char* path, const char* contents) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t len = contents ? strlen(contents) : 0;
    size_t wrote = fwrite(contents, 1, len, f);
    fclose(f);
    return wrote == len;
}

/* `read_line()` — one stdin line, trailing '\n' stripped; "" on EOF. */
static const char* read_line(void) {
    /* Use a growing buffer instead of getline() so we don't pull in
     * platform-specific extensions (getline is POSIX-only). */
    size_t cap = 128, len = 0;
    char* buf = (char*) GC_malloc_atomic(cap);
    int c;
    while ((c = fgetc(stdin)) != EOF && c != '\n') {
        if (len + 1 >= cap) {
            size_t new_cap = cap * 2;
            char* nb = (char*) GC_malloc_atomic(new_cap);
            memcpy(nb, buf, len);
            buf = nb;
            cap = new_cap;
        }
        buf[len++] = (char) c;
    }
    buf[len] = '\0';
    if (len == 0 && c == EOF) return "";
    return buf;
}

/* `int_to_str(n)` — base-10 i64 → GC-allocated string. */
static const char* int_to_str(int64_t n) {
    char tmp[24];
    int len = snprintf(tmp, sizeof tmp, "%lld", (long long) n);
    char* out = (char*) GC_malloc_atomic((size_t)len + 1);
    memcpy(out, tmp, (size_t)len + 1);
    return out;
}

/* `str_to_int(s)` — parse leading integer; 0 if malformed (libc atoi). */
static int64_t str_to_int(const char* s) {
    return (int64_t) atoll(s ? s : "");
}

/* `float_to_str(x)` — base-10 f64 → GC-allocated string ("%g" format). */
static const char* float_to_str(double x) {
    char tmp[32];
    int len = snprintf(tmp, sizeof tmp, "%g", x);
    if (len < 0) len = 0;
    if (len >= (int)sizeof tmp) len = (int)sizeof tmp - 1;
    char* out = (char*) GC_malloc_atomic((size_t)len + 1);
    memcpy(out, tmp, (size_t)len + 1);
    return out;
}

/* `str_to_float(s)` — parse leading float; 0.0 if malformed (libc strtod). */
static double str_to_float(const char* s) {
    return s ? strtod(s, NULL) : 0.0;
}

/* `format(fmt, ...)` — printf-style formatting → GC-allocated string.
 * Caller is responsible for matching `%d`/`%lld`/`%s`/`%g` against the
 * actual arg types; sema marks this variadic so we don't type-check args. */
static const char* format(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt ? fmt : "", ap);
    va_end(ap);
    if (n < 0) n = 0;
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    char* out = (char*) GC_malloc_atomic((size_t)n + 1);
    memcpy(out, buf, (size_t)n + 1);
    return out;
}

/* `exit(code)` — terminate the process. */
static void ailang_exit(int code) { exit(code); }
#define exit(c) ailang_exit((int)(c))

/* `get_env(name)` — read an environment variable; "" if unset. */
static const char* get_env(const char* name) {
    const char* v = name ? getenv(name) : NULL;
    return v ? v : "";
}

/* `abs_i64(n)` — i64 absolute value (libc `abs` is `int abs(int)`). */
static int64_t abs_i64(int64_t n) { return n < 0 ? -n : n; }

/* -------- `!T` result type (first slice of ADT support) --------
 *
 * One concrete `ailang_result_<T>` struct per primitive T. `ok(v)`
 * dispatches via `_Generic` on the wrapped value's type; `err_<T>(msg)`
 * is explicit per-T because the return type can't be inferred from a
 * string-only argument. `expr?` (Try) propagates errors to the
 * enclosing fn's return.
 *
 * Recursive / nested ADTs (general enums, e.g. `JsonValue`) still need
 * sum-type machinery — that's the next milestone past this one.
 */
typedef struct { bool ok; int64_t value; const char* error; } ailang_result_i64;
typedef struct { bool ok; const char* value; const char* error; } ailang_result_str;
typedef struct { bool ok; bool value; const char* error; } ailang_result_bool;
typedef struct { bool ok; double value; const char* error; } ailang_result_f64;

static inline ailang_result_i64  ok_i64(int64_t v)     { return (ailang_result_i64){true, v, ""}; }
static inline ailang_result_str  ok_str(const char* v) { return (ailang_result_str){true, v ? v : "", ""}; }
static inline ailang_result_bool ok_bool(bool v)       { return (ailang_result_bool){true, v, ""}; }
static inline ailang_result_f64  ok_f64(double v)      { return (ailang_result_f64){true, v, ""}; }

static inline ailang_result_i64  err_i64(const char* m)  { return (ailang_result_i64){false, 0, m ? m : ""}; }
static inline ailang_result_str  err_str(const char* m)  { return (ailang_result_str){false, "", m ? m : ""}; }
static inline ailang_result_bool err_bool(const char* m) { return (ailang_result_bool){false, false, m ? m : ""}; }
static inline ailang_result_f64  err_f64(const char* m)  { return (ailang_result_f64){false, 0.0, m ? m : ""}; }

#define ok(v) _Generic((v), \
    int64_t: ok_i64, \
    int: ok_i64, \
    double: ok_f64, \
    bool: ok_bool, \
    char*: ok_str, \
    const char*: ok_str)(v)

/* unwrap — return the value if ok, else abort with the error message. */
static inline int64_t     ailang_unwrap_i64 (ailang_result_i64  r) { if (!r.ok) { fprintf(stderr, "unwrap: %s\n", r.error); exit(1); } return r.value; }
static inline const char* ailang_unwrap_str (ailang_result_str  r) { if (!r.ok) { fprintf(stderr, "unwrap: %s\n", r.error); exit(1); } return r.value; }
static inline bool        ailang_unwrap_bool(ailang_result_bool r) { if (!r.ok) { fprintf(stderr, "unwrap: %s\n", r.error); exit(1); } return r.value; }
static inline double      ailang_unwrap_f64 (ailang_result_f64  r) { if (!r.ok) { fprintf(stderr, "unwrap: %s\n", r.error); exit(1); } return r.value; }
#define unwrap(r) _Generic((r), \
    ailang_result_i64:  ailang_unwrap_i64, \
    ailang_result_str:  ailang_unwrap_str, \
    ailang_result_bool: ailang_unwrap_bool, \
    ailang_result_f64:  ailang_unwrap_f64)(r)

static inline bool ailang_is_ok_i64 (ailang_result_i64  r) { return r.ok; }
static inline bool ailang_is_ok_str (ailang_result_str  r) { return r.ok; }
static inline bool ailang_is_ok_bool(ailang_result_bool r) { return r.ok; }
static inline bool ailang_is_ok_f64 (ailang_result_f64  r) { return r.ok; }
#define is_ok(r) _Generic((r), \
    ailang_result_i64:  ailang_is_ok_i64, \
    ailang_result_str:  ailang_is_ok_str, \
    ailang_result_bool: ailang_is_ok_bool, \
    ailang_result_f64:  ailang_is_ok_f64)(r)
#define is_err(r) ((bool)!is_ok(r))

/* err_msg(r) — the error string (empty if r is ok). */
static inline const char* ailang_err_msg_i64 (ailang_result_i64  r) { return r.error ? r.error : ""; }
static inline const char* ailang_err_msg_str (ailang_result_str  r) { return r.error ? r.error : ""; }
static inline const char* ailang_err_msg_bool(ailang_result_bool r) { return r.error ? r.error : ""; }
static inline const char* ailang_err_msg_f64 (ailang_result_f64  r) { return r.error ? r.error : ""; }
#define err_msg(r) _Generic((r), \
    ailang_result_i64:  ailang_err_msg_i64, \
    ailang_result_str:  ailang_err_msg_str, \
    ailang_result_bool: ailang_err_msg_bool, \
    ailang_result_f64:  ailang_err_msg_f64)(r)

/* `regex_match(pat, text)` — true if POSIX extended regex `pat` matches
 * any substring of `text`. Invalid patterns silently return false. */
static bool regex_match(const char* pat, const char* text) {
    if (!pat || !text) return false;
    regex_t re;
    if (regcomp(&re, pat, REG_EXTENDED | REG_NOSUB) != 0) return false;
    int rc = regexec(&re, text, 0, NULL, 0);
    regfree(&re);
    return rc == 0;
}

/* `regex_find(pat, text)` — first matching substring, GC-allocated.
 * Returns "" if no match or invalid pattern. */
static const char* regex_find(const char* pat, const char* text) {
    if (!pat || !text) return "";
    regex_t re;
    if (regcomp(&re, pat, REG_EXTENDED) != 0) return "";
    regmatch_t m;
    int rc = regexec(&re, text, 1, &m, 0);
    regfree(&re);
    if (rc != 0 || m.rm_so < 0) return "";
    size_t len = (size_t)(m.rm_eo - m.rm_so);
    char* out = (char*) GC_malloc_atomic(len + 1);
    memcpy(out, text + m.rm_so, len);
    out[len] = '\0';
    return out;
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

/* -------- closures (M6++): fat function-pointers --------
 *
 * Every AiLang lambda compiles to a `static` C function whose first
 * parameter is a `void* __env` (so the lifted body can reach captures
 * even when the lambda has none — non-capturing lambdas pass `NULL`).
 *
 * The runtime value of a lambda is a `ailang_closure_t { fn, env }`
 * pair. Every fn-pointer-typed param/local in user code is also
 * `ailang_closure_t`. Calls dispatch by casting `fn` to the right
 * signature and threading `env` through:
 *
 *     ((RET (*)(void*, P1, P2))c.fn)(c.env, a1, a2)
 *
 * `env` is GC-allocated. Captures snapshot the outer value by copy;
 * mutation of an outer `mu` after a lambda is constructed doesn't
 * propagate (an explicit ref type would land later).
 */
typedef struct {
    void* fn;
    void* env;
} ailang_closure_t;

/* push/pop — value-semantics: produce a fresh array, leaving the input
 * alone. O(n) per call. Reference-semantics (in-place push) would need
 * the array type itself to become a pointer; deferred. */
static ailang_arr_i64 ailang_arr_i64_push(ailang_arr_i64 a, int64_t x) {
    ailang_arr_i64 b;
    b.len = a.len + 1;
    b.data = (int64_t*) GC_MALLOC((size_t)b.len * sizeof(int64_t));
    if (a.len) memcpy(b.data, a.data, (size_t)a.len * sizeof(int64_t));
    b.data[a.len] = x;
    return b;
}
static ailang_arr_str ailang_arr_str_push(ailang_arr_str a, const char* x) {
    ailang_arr_str b;
    b.len = a.len + 1;
    b.data = (const char**) GC_MALLOC((size_t)b.len * sizeof(const char*));
    if (a.len) memcpy(b.data, a.data, (size_t)a.len * sizeof(const char*));
    b.data[a.len] = x;
    return b;
}
static ailang_arr_i64 ailang_arr_i64_pop(ailang_arr_i64 a) {
    ailang_arr_i64 b;
    b.len = a.len > 0 ? a.len - 1 : 0;
    b.data = (int64_t*) GC_MALLOC((size_t)(b.len > 0 ? b.len : 1) * sizeof(int64_t));
    if (b.len) memcpy(b.data, a.data, (size_t)b.len * sizeof(int64_t));
    return b;
}
static ailang_arr_str ailang_arr_str_pop(ailang_arr_str a) {
    ailang_arr_str b;
    b.len = a.len > 0 ? a.len - 1 : 0;
    b.data = (const char**) GC_MALLOC((size_t)(b.len > 0 ? b.len : 1) * sizeof(const char*));
    if (b.len) memcpy(b.data, a.data, (size_t)b.len * sizeof(const char*));
    return b;
}
#define push(arr, x) _Generic((arr), \
    ailang_arr_i64: ailang_arr_i64_push, \
    ailang_arr_str: ailang_arr_str_push)((arr), (x))
#define pop(arr) _Generic((arr), \
    ailang_arr_i64: ailang_arr_i64_pop, \
    ailang_arr_str: ailang_arr_str_pop)((arr))

/* sort / reverse / contains / index_of for arrays.
 *
 * sort: ascending, qsort-based, returns a fresh array (the input is
 * left alone so callers don't have to think about aliasing).
 * reverse: also returns a fresh array.
 * contains / index_of: linear scan. `index_of` returns -1 when missing.
 */
static int ailang_cmp_i64(const void* a, const void* b) {
    int64_t x = *(const int64_t*)a, y = *(const int64_t*)b;
    return (x > y) - (x < y);
}
static int ailang_cmp_str(const void* a, const void* b) {
    const char* x = *(const char* const*)a;
    const char* y = *(const char* const*)b;
    return strcmp(x ? x : "", y ? y : "");
}

static ailang_arr_i64 ailang_arr_i64_sort(ailang_arr_i64 a) {
    ailang_arr_i64 b;
    b.len = a.len;
    b.data = (int64_t*) GC_MALLOC((size_t)(b.len > 0 ? b.len : 1) * sizeof(int64_t));
    if (b.len) memcpy(b.data, a.data, (size_t)b.len * sizeof(int64_t));
    if (b.len > 1) qsort(b.data, (size_t)b.len, sizeof(int64_t), ailang_cmp_i64);
    return b;
}
static ailang_arr_str ailang_arr_str_sort(ailang_arr_str a) {
    ailang_arr_str b;
    b.len = a.len;
    b.data = (const char**) GC_MALLOC((size_t)(b.len > 0 ? b.len : 1) * sizeof(const char*));
    if (b.len) memcpy((void*)b.data, a.data, (size_t)b.len * sizeof(const char*));
    if (b.len > 1) qsort((void*)b.data, (size_t)b.len, sizeof(const char*), ailang_cmp_str);
    return b;
}

static ailang_arr_i64 ailang_arr_i64_reverse(ailang_arr_i64 a) {
    ailang_arr_i64 b;
    b.len = a.len;
    b.data = (int64_t*) GC_MALLOC((size_t)(b.len > 0 ? b.len : 1) * sizeof(int64_t));
    for (int64_t i = 0; i < b.len; i++) b.data[i] = a.data[b.len - 1 - i];
    return b;
}
static ailang_arr_str ailang_arr_str_reverse(ailang_arr_str a) {
    ailang_arr_str b;
    b.len = a.len;
    b.data = (const char**) GC_MALLOC((size_t)(b.len > 0 ? b.len : 1) * sizeof(const char*));
    for (int64_t i = 0; i < b.len; i++) b.data[i] = a.data[b.len - 1 - i];
    return b;
}

static bool ailang_arr_i64_contains(ailang_arr_i64 a, int64_t x) {
    for (int64_t i = 0; i < a.len; i++) if (a.data[i] == x) return true;
    return false;
}
static bool ailang_arr_str_contains(ailang_arr_str a, const char* x) {
    for (int64_t i = 0; i < a.len; i++) {
        if (a.data[i] == x) return true;
        if (a.data[i] && x && strcmp(a.data[i], x) == 0) return true;
    }
    return false;
}

static int64_t ailang_arr_i64_index_of(ailang_arr_i64 a, int64_t x) {
    for (int64_t i = 0; i < a.len; i++) if (a.data[i] == x) return i;
    return -1;
}
static int64_t ailang_arr_str_index_of(ailang_arr_str a, const char* x) {
    for (int64_t i = 0; i < a.len; i++) {
        if (a.data[i] == x) return i;
        if (a.data[i] && x && strcmp(a.data[i], x) == 0) return i;
    }
    return -1;
}

#define sort(arr) _Generic((arr), \
    ailang_arr_i64: ailang_arr_i64_sort, \
    ailang_arr_str: ailang_arr_str_sort)((arr))

#define reverse(arr) _Generic((arr), \
    ailang_arr_i64: ailang_arr_i64_reverse, \
    ailang_arr_str: ailang_arr_str_reverse)((arr))

#define contains(coll, x) _Generic((coll), \
    char*: ailang_str_contains, \
    const char*: ailang_str_contains, \
    ailang_arr_i64: ailang_arr_i64_contains, \
    ailang_arr_str: ailang_arr_str_contains)((coll), (x))

#define index_of(coll, x) _Generic((coll), \
    char*: ailang_str_index_of, \
    const char*: ailang_str_index_of, \
    ailang_arr_i64: ailang_arr_i64_index_of, \
    ailang_arr_str: ailang_arr_str_index_of)((coll), (x))

/* Higher-order helpers. v1: [i64] and [str] element types. Each callback
 * is an `ailang_closure_t`, dispatched through (*f.fn)(f.env, …). */
static ailang_arr_i64 ailang_arr_i64_map(ailang_arr_i64 a, ailang_closure_t f) {
    typedef int64_t (*FN)(void*, int64_t);
    FN fp = (FN) f.fn;
    ailang_arr_i64 b;
    b.len = a.len;
    b.data = (int64_t*) GC_MALLOC((size_t)(b.len > 0 ? b.len : 1) * sizeof(int64_t));
    for (int64_t i = 0; i < a.len; i++) b.data[i] = fp(f.env, a.data[i]);
    return b;
}
static ailang_arr_i64 ailang_arr_i64_filter(ailang_arr_i64 a, ailang_closure_t pred) {
    typedef bool (*FN)(void*, int64_t);
    FN fp = (FN) pred.fn;
    ailang_arr_i64 b;
    b.data = (int64_t*) GC_MALLOC((size_t)(a.len > 0 ? a.len : 1) * sizeof(int64_t));
    int64_t n = 0;
    for (int64_t i = 0; i < a.len; i++) {
        if (fp(pred.env, a.data[i])) b.data[n++] = a.data[i];
    }
    b.len = n;
    return b;
}
static int64_t ailang_arr_i64_reduce(ailang_arr_i64 a, int64_t init, ailang_closure_t f) {
    typedef int64_t (*FN)(void*, int64_t, int64_t);
    FN fp = (FN) f.fn;
    int64_t acc = init;
    for (int64_t i = 0; i < a.len; i++) acc = fp(f.env, acc, a.data[i]);
    return acc;
}

static ailang_arr_str ailang_arr_str_map(ailang_arr_str a, ailang_closure_t f) {
    typedef const char* (*FN)(void*, const char*);
    FN fp = (FN) f.fn;
    ailang_arr_str b;
    b.len = a.len;
    b.data = (const char**) GC_MALLOC((size_t)(b.len > 0 ? b.len : 1) * sizeof(const char*));
    for (int64_t i = 0; i < a.len; i++) b.data[i] = fp(f.env, a.data[i]);
    return b;
}
static ailang_arr_str ailang_arr_str_filter(ailang_arr_str a, ailang_closure_t pred) {
    typedef bool (*FN)(void*, const char*);
    FN fp = (FN) pred.fn;
    ailang_arr_str b;
    b.data = (const char**) GC_MALLOC((size_t)(a.len > 0 ? a.len : 1) * sizeof(const char*));
    int64_t n = 0;
    for (int64_t i = 0; i < a.len; i++) {
        if (fp(pred.env, a.data[i])) b.data[n++] = a.data[i];
    }
    b.len = n;
    return b;
}
/* reduce on [str]: accumulator is i64. */
static int64_t ailang_arr_str_reduce(ailang_arr_str a, int64_t init, ailang_closure_t f) {
    typedef int64_t (*FN)(void*, int64_t, const char*);
    FN fp = (FN) f.fn;
    int64_t acc = init;
    for (int64_t i = 0; i < a.len; i++) acc = fp(f.env, acc, a.data[i]);
    return acc;
}

#define map(arr, f) _Generic((arr), \
    ailang_arr_i64: ailang_arr_i64_map, \
    ailang_arr_str: ailang_arr_str_map)((arr), (f))
#define filter(arr, p) _Generic((arr), \
    ailang_arr_i64: ailang_arr_i64_filter, \
    ailang_arr_str: ailang_arr_str_filter)((arr), (p))
#define reduce(arr, init, f) _Generic((arr), \
    ailang_arr_i64: ailang_arr_i64_reduce, \
    ailang_arr_str: ailang_arr_str_reduce)((arr), (init), (f))

/* slice(arr, start, end) — half-open `[start, end)`. Clamps to bounds. */
static ailang_arr_i64 ailang_arr_i64_slice(ailang_arr_i64 a, int64_t start, int64_t end) {
    if (start < 0) start = 0;
    if (end > a.len) end = a.len;
    if (end < start) end = start;
    ailang_arr_i64 b;
    b.len = end - start;
    b.data = (int64_t*) GC_MALLOC((size_t)(b.len > 0 ? b.len : 1) * sizeof(int64_t));
    if (b.len) memcpy(b.data, a.data + start, (size_t)b.len * sizeof(int64_t));
    return b;
}
static ailang_arr_str ailang_arr_str_slice(ailang_arr_str a, int64_t start, int64_t end) {
    if (start < 0) start = 0;
    if (end > a.len) end = a.len;
    if (end < start) end = start;
    ailang_arr_str b;
    b.len = end - start;
    b.data = (const char**) GC_MALLOC((size_t)(b.len > 0 ? b.len : 1) * sizeof(const char*));
    if (b.len) memcpy((void*)b.data, a.data + start, (size_t)b.len * sizeof(const char*));
    return b;
}
#define slice(arr, start, end) _Generic((arr), \
    ailang_arr_i64: ailang_arr_i64_slice, \
    ailang_arr_str: ailang_arr_str_slice)((arr), (start), (end))

/* join — sep-delimited concatenation of an array.
 * For [i64], each element is formatted via "%lld". For [str], the
 * elements concatenate as-is (NULL→""). Returns a fresh GC string. */
static const char* ailang_arr_str_join(ailang_arr_str a, const char* sep) {
    if (!sep) sep = "";
    size_t sep_len = strlen(sep);
    size_t total = 0;
    for (int64_t i = 0; i < a.len; i++) total += a.data[i] ? strlen(a.data[i]) : 0;
    if (a.len > 1) total += sep_len * (size_t)(a.len - 1);
    char* out = (char*) GC_malloc_atomic(total + 1);
    char* w = out;
    for (int64_t i = 0; i < a.len; i++) {
        if (i > 0 && sep_len) { memcpy(w, sep, sep_len); w += sep_len; }
        const char* s = a.data[i] ? a.data[i] : "";
        size_t n = strlen(s);
        if (n) { memcpy(w, s, n); w += n; }
    }
    *w = '\0';
    return out;
}
static const char* ailang_arr_i64_join(ailang_arr_i64 a, const char* sep) {
    if (!sep) sep = "";
    size_t sep_len = strlen(sep);
    /* Worst-case: each i64 needs ≤ 21 chars (sign + 20 digits + nul). */
    size_t cap = (size_t)a.len * 22 + (a.len > 1 ? sep_len * (size_t)(a.len - 1) : 0) + 1;
    if (cap < 16) cap = 16;
    char* out = (char*) GC_malloc_atomic(cap);
    char* w = out;
    for (int64_t i = 0; i < a.len; i++) {
        if (i > 0 && sep_len) { memcpy(w, sep, sep_len); w += sep_len; }
        int n = snprintf(w, cap - (size_t)(w - out), "%lld", (long long) a.data[i]);
        if (n > 0) w += n;
    }
    *w = '\0';
    return out;
}
#define join(arr, sep) _Generic((arr), \
    ailang_arr_i64: ailang_arr_i64_join, \
    ailang_arr_str: ailang_arr_str_join)((arr), (sep))

/* repeat / pad — GC-allocated string outputs. */
static const char* repeat(const char* s, int64_t n) {
    if (!s || n <= 0) return "";
    size_t one = strlen(s);
    size_t total = one * (size_t)n;
    char* out = (char*) GC_malloc_atomic(total + 1);
    for (int64_t i = 0; i < n; i++) memcpy(out + (size_t)i * one, s, one);
    out[total] = '\0';
    return out;
}
static const char* pad_left(const char* s, int64_t width, const char* pad) {
    if (!s) s = "";
    if (!pad || !*pad) pad = " ";
    size_t slen = strlen(s);
    if ((int64_t)slen >= width) {
        char* out = (char*) GC_malloc_atomic(slen + 1);
        memcpy(out, s, slen + 1);
        return out;
    }
    size_t plen = strlen(pad);
    size_t need = (size_t)width - slen;
    char* out = (char*) GC_malloc_atomic((size_t)width + 1);
    size_t i = 0;
    while (i < need) { out[i] = pad[i % plen]; i++; }
    memcpy(out + need, s, slen);
    out[width] = '\0';
    return out;
}
static const char* pad_right(const char* s, int64_t width, const char* pad) {
    if (!s) s = "";
    if (!pad || !*pad) pad = " ";
    size_t slen = strlen(s);
    if ((int64_t)slen >= width) {
        char* out = (char*) GC_malloc_atomic(slen + 1);
        memcpy(out, s, slen + 1);
        return out;
    }
    size_t plen = strlen(pad);
    char* out = (char*) GC_malloc_atomic((size_t)width + 1);
    memcpy(out, s, slen);
    size_t i = slen;
    while (i < (size_t)width) { out[i] = pad[(i - slen) % plen]; i++; }
    out[width] = '\0';
    return out;
}

/* chr(i) — single-byte string. ord(s) — first byte of s (0 if empty). */
static const char* chr(int64_t i) {
    char* out = (char*) GC_malloc_atomic(2);
    out[0] = (char)(i & 0xFF);
    out[1] = '\0';
    return out;
}
static int64_t ord(const char* s) {
    return (s && *s) ? (int64_t)(unsigned char) s[0] : 0;
}

/* str_to_bool — "true"/"True"/"1"/"yes" → true; everything else false. */
static bool str_to_bool(const char* s) {
    if (!s) return false;
    if (strcmp(s, "true") == 0) return true;
    if (strcmp(s, "True") == 0) return true;
    if (strcmp(s, "TRUE") == 0) return true;
    if (strcmp(s, "1") == 0) return true;
    if (strcmp(s, "yes") == 0) return true;
    return false;
}

/* Misc numeric helpers. */
static double abs_f64(double x) { return x < 0 ? -x : x; }
static int64_t sign(int64_t n) { return (n > 0) - (n < 0); }
static int64_t clamp(int64_t n, int64_t lo, int64_t hi) {
    if (n < lo) return lo;
    if (n > hi) return hi;
    return n;
}

/* `split(s, sep)` — split a string by a literal separator (not regex) into
 * a `[str]`. Empty separator returns `[s]` (no-op). Adjacent separators
 * produce empty strings, mirroring Python's `s.split(sep)`. */
static ailang_arr_str split(const char* s, const char* sep) {
    if (!s) s = "";
    if (!sep || !*sep) {
        ailang_arr_str a;
        a.len = 1;
        a.data = (const char**) GC_MALLOC(sizeof(const char*));
        a.data[0] = s;
        return a;
    }
    size_t lsep = strlen(sep);
    /* count pieces */
    int64_t pieces = 1;
    const char* p = s;
    while ((p = strstr(p, sep)) != NULL) { pieces++; p += lsep; }
    ailang_arr_str a;
    a.len = pieces;
    a.data = (const char**) GC_MALLOC((size_t)pieces * sizeof(const char*));
    const char* r = s;
    int64_t i = 0;
    while ((p = strstr(r, sep)) != NULL) {
        size_t n = (size_t)(p - r);
        char* piece = (char*) GC_malloc_atomic(n + 1);
        memcpy(piece, r, n);
        piece[n] = '\0';
        a.data[i++] = piece;
        r = p + lsep;
    }
    /* trailing piece */
    size_t n = strlen(r);
    char* piece = (char*) GC_malloc_atomic(n + 1);
    memcpy(piece, r, n + 1);
    a.data[i] = piece;
    return a;
}

/* `args()` — argv[1..] as `[str]`. argc/argv captured in main(). */
static ailang_arr_str args(void) {
    int64_t n = ailang_argc_ > 1 ? (int64_t)(ailang_argc_ - 1) : 0;
    ailang_arr_str a;
    a.len = n;
    a.data = (const char**) GC_MALLOC((size_t)(n > 0 ? n : 1) * sizeof(const char*));
    for (int64_t i = 0; i < n; i++) a.data[i] = ailang_argv_[i + 1];
    return a;
}

#define len(x) _Generic((x), \
    char*: ailang_str_len, \
    const char*: ailang_str_len, \
    ailang_arr_i64: ailang_arr_i64_len, \
    ailang_arr_str: ailang_arr_str_len, \
    ailang_map_ii: ailang_map_ii_len, \
    ailang_map_si: ailang_map_si_len, \
    ailang_map_ss: ailang_map_ss_len)(x)

/* `has(m, k)` — map-only membership check. */
#define has(m, k) _Generic((m), \
    ailang_map_ii: ailang_map_ii_has, \
    ailang_map_si: ailang_map_si_has, \
    ailang_map_ss: ailang_map_ss_has)((m), (k))

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

/* -------- {str:i64} map — same shape as {i64:i64}, FNV-1a key hashing.
 *
 * Useful instantiation for word counters / configuration tables. Mirrors
 * the ii layout so the runtime cost story is identical; only the key type
 * and hash differ. */
typedef struct {
    int64_t cap;
    int64_t len;
    const char** keys;   /* NULL when slot is empty */
    int64_t* values;
    uint8_t* occupied;
} ailang_map_si_storage;
typedef ailang_map_si_storage* ailang_map_si;

static inline uint64_t ailang_hash_str(const char* s) {
    /* FNV-1a 64-bit. */
    uint64_t h = 0xcbf29ce484222325ULL;
    if (!s) return h;
    while (*s) { h ^= (uint64_t)(unsigned char) *s++; h *= 0x100000001b3ULL; }
    return h;
}

static ailang_map_si ailang_map_si_make(int64_t initial_cap) {
    int64_t cap = 8;
    while (cap < initial_cap) cap *= 2;
    ailang_map_si m = (ailang_map_si) GC_MALLOC(sizeof(ailang_map_si_storage));
    m->cap = cap;
    m->len = 0;
    m->keys = (const char**) GC_MALLOC((size_t)cap * sizeof(const char*));
    m->values = (int64_t*) GC_MALLOC((size_t)cap * sizeof(int64_t));
    m->occupied = (uint8_t*) GC_MALLOC((size_t)cap);
    return m;
}

static void ailang_map_si_grow(ailang_map_si m) {
    int64_t old_cap = m->cap;
    const char** old_keys = m->keys;
    int64_t* old_values = m->values;
    uint8_t* old_occupied = m->occupied;
    int64_t new_cap = old_cap * 2;
    m->cap = new_cap;
    m->keys = (const char**) GC_MALLOC((size_t)new_cap * sizeof(const char*));
    m->values = (int64_t*) GC_MALLOC((size_t)new_cap * sizeof(int64_t));
    m->occupied = (uint8_t*) GC_MALLOC((size_t)new_cap);
    uint64_t mask = (uint64_t)(new_cap - 1);
    for (int64_t i = 0; i < old_cap; i++) {
        if (!old_occupied[i]) continue;
        const char* k = old_keys[i];
        uint64_t h = ailang_hash_str(k) & mask;
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

static void ailang_map_si_set(ailang_map_si m, const char* k, int64_t v) {
    if (m->len * 10 >= m->cap * 7) ailang_map_si_grow(m);
    uint64_t mask = (uint64_t)(m->cap - 1);
    uint64_t h = ailang_hash_str(k) & mask;
    for (int64_t p = 0; p < m->cap; p++) {
        int64_t i = (int64_t)((h + (uint64_t)p) & mask);
        if (!m->occupied[i]) {
            m->keys[i] = k;
            m->values[i] = v;
            m->occupied[i] = 1;
            m->len++;
            return;
        }
        if (strcmp(m->keys[i], k) == 0) { m->values[i] = v; return; }
    }
}

static int64_t ailang_map_si_get(ailang_map_si m, const char* k) {
    uint64_t mask = (uint64_t)(m->cap - 1);
    uint64_t h = ailang_hash_str(k) & mask;
    for (int64_t p = 0; p < m->cap; p++) {
        int64_t i = (int64_t)((h + (uint64_t)p) & mask);
        if (!m->occupied[i]) return 0;
        if (strcmp(m->keys[i], k) == 0) return m->values[i];
    }
    return 0;
}

static int64_t ailang_map_si_len(ailang_map_si m) { return m->len; }
static bool ailang_map_si_has(ailang_map_si m, const char* k) {
    uint64_t mask = (uint64_t)(m->cap - 1);
    uint64_t h = ailang_hash_str(k) & mask;
    for (int64_t p = 0; p < m->cap; p++) {
        int64_t i = (int64_t)((h + (uint64_t)p) & mask);
        if (!m->occupied[i]) return false;
        if (strcmp(m->keys[i], k) == 0) return true;
    }
    return false;
}

static void ailang_print_map_si(ailang_map_si m) {
    printf("{");
    bool first = true;
    for (int64_t i = 0; i < m->cap; i++) {
        if (!m->occupied[i]) continue;
        if (!first) printf(", ");
        first = false;
        printf("\"%s\": %lld", m->keys[i] ? m->keys[i] : "", (long long) m->values[i]);
    }
    printf("}");
}
static void ailang_println_map_si(ailang_map_si m) { ailang_print_map_si(m); printf("\n"); }

/* -------- {str:str} map — same shape, both keys and values are strings.
 *
 * Useful for parsing flat JSON, config files, environment-style data. */
typedef struct {
    int64_t cap;
    int64_t len;
    const char** keys;
    const char** values;
    uint8_t* occupied;
} ailang_map_ss_storage;
typedef ailang_map_ss_storage* ailang_map_ss;

static ailang_map_ss ailang_map_ss_make(int64_t initial_cap) {
    int64_t cap = 8;
    while (cap < initial_cap) cap *= 2;
    ailang_map_ss m = (ailang_map_ss) GC_MALLOC(sizeof(ailang_map_ss_storage));
    m->cap = cap;
    m->len = 0;
    m->keys = (const char**) GC_MALLOC((size_t)cap * sizeof(const char*));
    m->values = (const char**) GC_MALLOC((size_t)cap * sizeof(const char*));
    m->occupied = (uint8_t*) GC_MALLOC((size_t)cap);
    return m;
}

static void ailang_map_ss_grow(ailang_map_ss m) {
    int64_t old_cap = m->cap;
    const char** old_keys = m->keys;
    const char** old_values = m->values;
    uint8_t* old_occupied = m->occupied;
    int64_t new_cap = old_cap * 2;
    m->cap = new_cap;
    m->keys = (const char**) GC_MALLOC((size_t)new_cap * sizeof(const char*));
    m->values = (const char**) GC_MALLOC((size_t)new_cap * sizeof(const char*));
    m->occupied = (uint8_t*) GC_MALLOC((size_t)new_cap);
    uint64_t mask = (uint64_t)(new_cap - 1);
    for (int64_t i = 0; i < old_cap; i++) {
        if (!old_occupied[i]) continue;
        const char* k = old_keys[i];
        uint64_t h = ailang_hash_str(k) & mask;
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

static void ailang_map_ss_set(ailang_map_ss m, const char* k, const char* v) {
    if (m->len * 10 >= m->cap * 7) ailang_map_ss_grow(m);
    uint64_t mask = (uint64_t)(m->cap - 1);
    uint64_t h = ailang_hash_str(k) & mask;
    for (int64_t p = 0; p < m->cap; p++) {
        int64_t i = (int64_t)((h + (uint64_t)p) & mask);
        if (!m->occupied[i]) {
            m->keys[i] = k;
            m->values[i] = v;
            m->occupied[i] = 1;
            m->len++;
            return;
        }
        if (strcmp(m->keys[i], k) == 0) { m->values[i] = v; return; }
    }
}

static const char* ailang_map_ss_get(ailang_map_ss m, const char* k) {
    uint64_t mask = (uint64_t)(m->cap - 1);
    uint64_t h = ailang_hash_str(k) & mask;
    for (int64_t p = 0; p < m->cap; p++) {
        int64_t i = (int64_t)((h + (uint64_t)p) & mask);
        if (!m->occupied[i]) return "";
        if (strcmp(m->keys[i], k) == 0) return m->values[i];
    }
    return "";
}

static int64_t ailang_map_ss_len(ailang_map_ss m) { return m->len; }
static bool ailang_map_ss_has(ailang_map_ss m, const char* k) {
    uint64_t mask = (uint64_t)(m->cap - 1);
    uint64_t h = ailang_hash_str(k) & mask;
    for (int64_t p = 0; p < m->cap; p++) {
        int64_t i = (int64_t)((h + (uint64_t)p) & mask);
        if (!m->occupied[i]) return false;
        if (strcmp(m->keys[i], k) == 0) return true;
    }
    return false;
}

static void ailang_print_map_ss(ailang_map_ss m) {
    printf("{");
    bool first = true;
    for (int64_t i = 0; i < m->cap; i++) {
        if (!m->occupied[i]) continue;
        if (!first) printf(", ");
        first = false;
        printf("\"%s\": \"%s\"",
            m->keys[i] ? m->keys[i] : "",
            m->values[i] ? m->values[i] : "");
    }
    printf("}");
}
static void ailang_println_map_ss(ailang_map_ss m) { ailang_print_map_ss(m); printf("\n"); }

/* -------- keys / values for both map combos.
 * Return arrays of the appropriate element type. */
static ailang_arr_i64 ailang_map_ii_keys(ailang_map_ii m) {
    ailang_arr_i64 a;
    a.len = m->len;
    a.data = (int64_t*) GC_MALLOC((size_t)(a.len > 0 ? a.len : 1) * sizeof(int64_t));
    int64_t n = 0;
    for (int64_t i = 0; i < m->cap; i++) if (m->occupied[i]) a.data[n++] = m->keys[i];
    return a;
}
static ailang_arr_i64 ailang_map_ii_values(ailang_map_ii m) {
    ailang_arr_i64 a;
    a.len = m->len;
    a.data = (int64_t*) GC_MALLOC((size_t)(a.len > 0 ? a.len : 1) * sizeof(int64_t));
    int64_t n = 0;
    for (int64_t i = 0; i < m->cap; i++) if (m->occupied[i]) a.data[n++] = m->values[i];
    return a;
}
static ailang_arr_str ailang_map_si_keys(ailang_map_si m) {
    ailang_arr_str a;
    a.len = m->len;
    a.data = (const char**) GC_MALLOC((size_t)(a.len > 0 ? a.len : 1) * sizeof(const char*));
    int64_t n = 0;
    for (int64_t i = 0; i < m->cap; i++) if (m->occupied[i]) a.data[n++] = m->keys[i];
    return a;
}
static ailang_arr_i64 ailang_map_si_values(ailang_map_si m) {
    ailang_arr_i64 a;
    a.len = m->len;
    a.data = (int64_t*) GC_MALLOC((size_t)(a.len > 0 ? a.len : 1) * sizeof(int64_t));
    int64_t n = 0;
    for (int64_t i = 0; i < m->cap; i++) if (m->occupied[i]) a.data[n++] = m->values[i];
    return a;
}
static ailang_arr_str ailang_map_ss_keys(ailang_map_ss m) {
    ailang_arr_str a;
    a.len = m->len;
    a.data = (const char**) GC_MALLOC((size_t)(a.len > 0 ? a.len : 1) * sizeof(const char*));
    int64_t n = 0;
    for (int64_t i = 0; i < m->cap; i++) if (m->occupied[i]) a.data[n++] = m->keys[i];
    return a;
}
static ailang_arr_str ailang_map_ss_values(ailang_map_ss m) {
    ailang_arr_str a;
    a.len = m->len;
    a.data = (const char**) GC_MALLOC((size_t)(a.len > 0 ? a.len : 1) * sizeof(const char*));
    int64_t n = 0;
    for (int64_t i = 0; i < m->cap; i++) if (m->occupied[i]) a.data[n++] = m->values[i];
    return a;
}

/* Polymorphic indexing: `a[i]` codegens to `ailang_at(a, i)`. */
#define ailang_at(x, i) _Generic((x), \
    char*: ailang_str_at, \
    const char*: ailang_str_at, \
    ailang_arr_i64: ailang_arr_i64_at, \
    ailang_arr_str: ailang_arr_str_at, \
    ailang_map_ii: ailang_map_ii_get, \
    ailang_map_si: ailang_map_si_get, \
    ailang_map_ss: ailang_map_ss_get)((x), (i))

#define keys(m) _Generic((m), \
    ailang_map_ii: ailang_map_ii_keys, \
    ailang_map_si: ailang_map_si_keys, \
    ailang_map_ss: ailang_map_ss_keys)((m))

#define values(m) _Generic((m), \
    ailang_map_ii: ailang_map_ii_values, \
    ailang_map_si: ailang_map_si_values, \
    ailang_map_ss: ailang_map_ss_values)((m))

/* Aggregate pretty-printers. `print` returns void so we can compose:
 *   println_arr_i64 = print + '\n'. */
static void ailang_print_arr_i64(ailang_arr_i64 a) {
    printf("[");
    for (int64_t i = 0; i < a.len; i++) {
        if (i > 0) printf(", ");
        printf("%lld", (long long) a.data[i]);
    }
    printf("]");
}
static void ailang_println_arr_i64(ailang_arr_i64 a) { ailang_print_arr_i64(a); printf("\n"); }

static void ailang_print_arr_str(ailang_arr_str a) {
    printf("[");
    for (int64_t i = 0; i < a.len; i++) {
        if (i > 0) printf(", ");
        printf("\"%s\"", a.data[i] ? a.data[i] : "");
    }
    printf("]");
}
static void ailang_println_arr_str(ailang_arr_str a) { ailang_print_arr_str(a); printf("\n"); }

static void ailang_print_map_ii(ailang_map_ii m) {
    printf("{");
    bool first = true;
    for (int64_t i = 0; i < m->cap; i++) {
        if (!m->occupied[i]) continue;
        if (!first) printf(", ");
        first = false;
        printf("%lld: %lld", (long long) m->keys[i], (long long) m->values[i]);
    }
    printf("}");
}
static void ailang_println_map_ii(ailang_map_ii m) { ailang_print_map_ii(m); printf("\n"); }

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
    const char*: ailang_print_str, \
    ailang_arr_i64: ailang_print_arr_i64, \
    ailang_arr_str: ailang_print_arr_str, \
    ailang_map_ii: ailang_print_map_ii, \
    ailang_map_si: ailang_print_map_si, \
    ailang_map_ss: ailang_print_map_ss)(x)

#define println(x) _Generic((x), \
    int64_t: ailang_println_i64, \
    int: ailang_println_int, \
    double: ailang_println_f64, \
    bool: ailang_println_bool, \
    char*: ailang_println_str, \
    const char*: ailang_println_str, \
    ailang_arr_i64: ailang_println_arr_i64, \
    ailang_arr_str: ailang_println_arr_str, \
    ailang_map_ii: ailang_println_map_ii, \
    ailang_map_si: ailang_println_map_si, \
    ailang_map_ss: ailang_println_map_ss)(x)
"#;

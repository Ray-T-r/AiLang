//! Collection passes: generic-instance discovery and lambda lifting.
use crate::typegen::*;
use crate::visit::{walk_block, walk_expr};
use ailang_sema::{ResolvedModule, Ty};
use ailang_syntax::ast::*;
use ailang_syntax::token::Span;

/// (generic-fn name) → fn decl + set of concrete type-arg tuples seen.
/// Stores the FnDecl alongside so unification can use the param types.
pub(crate) fn collect_generic_instances<'a>(
    m: &'a Module,
    resolved: &ResolvedModule,
) -> std::collections::BTreeMap<String, std::collections::BTreeSet<Vec<Ty>>> {
    let mut out: std::collections::BTreeMap<String, std::collections::BTreeSet<Vec<Ty>>> =
        std::collections::BTreeMap::new();
    // Map of generic fn name → its FnDecl (for unification).
    let generic_fns: std::collections::HashMap<&String, &'a FnDecl> = m
        .items
        .iter()
        .filter_map(|i| match i {
            Item::Fn(f) if !f.type_params.is_empty() => Some((&f.name.name, f)),
            _ => None,
        })
        .collect();
    if generic_fns.is_empty() {
        return out;
    }
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
    walk_block(b, &mut |e| {
        let ExprKind::Call { callee, args } = &e.kind else {
            return;
        };
        let ExprKind::Ident(name) = &callee.kind else {
            return;
        };
        if !generic_names.contains(name) {
            return;
        }
        let Some(f) = generic_fns.get(name) else {
            return;
        };
        let type_var_set: std::collections::HashSet<String> =
            f.type_params.iter().map(|p| p.name.clone()).collect();
        let mut subst: std::collections::HashMap<String, Ty> = std::collections::HashMap::new();
        // Unify each (param ty, arg ty) pair.
        for (i, param) in f.params.iter().enumerate() {
            if let Some(pt) = &param.ty {
                let arg_ty = args
                    .get(i)
                    .and_then(|a| res.expr_types.get(&a.span))
                    .cloned()
                    .unwrap_or(Ty::Unknown);
                unify_ty(pt, &arg_ty, &type_var_set, &mut subst);
            }
        }
        // Build the concrete type-tuple in declaration order.
        let type_args: Vec<Ty> = f
            .type_params
            .iter()
            .map(|tp| subst.get(&tp.name).cloned().unwrap_or(Ty::I64))
            .collect();
        // Register the instance when every type var resolved to a concrete type
        // we can monomorphize and mangle distinctly: a primitive or a user
        // struct/enum (both carried as `Ty::Struct`). `[T]`/`{K:V}` of an
        // aggregate is follow-up work; an unresolved var already defaulted to
        // `i64` above.
        if type_args
            .iter()
            .all(|t| is_primitive_ty(t) || matches!(t, Ty::Struct(_)))
        {
            out.entry(name.clone()).or_default().insert(type_args);
        }
    });
}

/// Per-lambda info: the unique C name, params/body (cloned from AST), and
/// the analyzed capture list — names from the enclosing scope that the
/// body references. Each capture lands in a generated env struct allocated
/// on the GC heap at lambda-construction time.
pub(crate) struct LambdaInfo {
    pub(crate) c_name: String,
    pub(crate) env_name: String, // typedef name for the env struct (only emitted if non-empty)
    pub(crate) params: Vec<Param>,
    pub(crate) body: LambdaBody,
    pub(crate) captures: Vec<(String, String)>, // (var name, C type) pairs
    /// C type spelling for the lambda's return — derived from sema's
    /// expr_types on the body's tail expression. Defaults to int64_t.
    pub(crate) return_c_ty: String,
    #[allow(dead_code)]
    pub(crate) span: Span,
}

pub(crate) fn collect_lambdas(m: &Module, resolved: &ResolvedModule) -> Vec<(Span, LambdaInfo)> {
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

pub(crate) fn walk_block_for_lambdas(
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

pub(crate) fn walk_stmt_for_lambdas(
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
                Some(ElseBranch::If(inner)) => {
                    walk_stmt_for_lambdas(&Stmt::If((**inner).clone()), bound, out, res)
                }
                None => {}
            }
        }
        Stmt::Loop(l) => {
            match &l.head {
                Some(LoopHead::ForIn { vars, iter }) => {
                    walk_expr_for_lambdas(iter, bound, out, res);
                    let saved = bound.clone();
                    for v in vars {
                        bound.insert(v.name.clone());
                    }
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

pub(crate) fn walk_expr_for_lambdas(
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
                if param_names.contains(n) {
                    continue;
                }
                if res.fn_table.contains_key(n) {
                    continue;
                }
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
                    let ty = type_of_ident_in(&n, body, &res.expr_types).unwrap_or(Ty::I64);
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
            let return_c_ty = res
                .expr_types
                .get(&e.span)
                .map(|t| c_ty_for(t))
                .unwrap_or_else(|| "int64_t".to_string());

            out.push((
                e.span,
                LambdaInfo {
                    c_name,
                    env_name,
                    params: params.clone(),
                    body: body.clone(),
                    captures,
                    return_c_ty,
                    span: e.span,
                },
            ));
        }
        ExprKind::Call { callee, args } => {
            walk_expr_for_lambdas(callee, bound, out, res);
            for a in args {
                walk_expr_for_lambdas(a, bound, out, res);
            }
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
        ExprKind::Array(xs) => {
            for x in xs {
                walk_expr_for_lambdas(x, bound, out, res);
            }
        }
        ExprKind::Map(es) => {
            for (k, v) in es {
                walk_expr_for_lambdas(k, bound, out, res);
                walk_expr_for_lambdas(v, bound, out, res);
            }
        }
        ExprKind::Tuple(xs) => {
            for x in xs {
                walk_expr_for_lambdas(x, bound, out, res);
            }
        }
        ExprKind::StructLit { fields, .. } => {
            for (_, v) in fields {
                walk_expr_for_lambdas(v, bound, out, res);
            }
        }
        ExprKind::Block(b) => walk_block_for_lambdas(b, bound, out, res),
        ExprKind::Try(inner) => walk_expr_for_lambdas(inner, bound, out, res),
        _ => {}
    }
}

/// Collect every identifier name that appears as a value-position `Ident`
/// inside a lambda body. Used by capture analysis.
pub(crate) fn referenced_names_in_body(b: &LambdaBody) -> std::collections::HashSet<String> {
    let mut out = std::collections::HashSet::new();
    {
        let mut visit = |e: &Expr| {
            if let ExprKind::Ident(n) = &e.kind {
                out.insert(n.clone());
            }
        };
        match b {
            LambdaBody::Expr(e) => walk_expr(e, &mut visit),
            LambdaBody::Block(blk) => walk_block(blk, &mut visit),
        }
    }
    out
}

/// Look up the type of an `Ident(name)` reference somewhere in the lambda
/// body, by consulting sema's expr_types side table.
pub(crate) fn type_of_ident_in(
    name: &str,
    body: &LambdaBody,
    et: &std::collections::HashMap<Span, Ty>,
) -> Option<Ty> {
    fn walk_expr(name: &str, e: &Expr, et: &std::collections::HashMap<Span, Ty>) -> Option<Ty> {
        if let ExprKind::Ident(n) = &e.kind {
            if n == name {
                return et.get(&e.span).cloned();
            }
        }
        match &e.kind {
            ExprKind::Call { callee, args } => walk_expr(name, callee, et)
                .or_else(|| args.iter().find_map(|a| walk_expr(name, a, et))),
            ExprKind::Index { container, index } => {
                walk_expr(name, container, et).or_else(|| walk_expr(name, index, et))
            }
            ExprKind::Field { container, .. } => walk_expr(name, container, et),
            ExprKind::Binary { lhs, rhs, .. } => {
                walk_expr(name, lhs, et).or_else(|| walk_expr(name, rhs, et))
            }
            ExprKind::Unary { operand, .. } => walk_expr(name, operand, et),
            ExprKind::Ternary { cond, then_, else_ } => walk_expr(name, cond, et)
                .or_else(|| walk_expr(name, then_, et))
                .or_else(|| walk_expr(name, else_, et)),
            ExprKind::Pipe { lhs, rhs } => {
                walk_expr(name, lhs, et).or_else(|| walk_expr(name, rhs, et))
            }
            ExprKind::Array(xs) => xs.iter().find_map(|x| walk_expr(name, x, et)),
            ExprKind::Map(es) => es
                .iter()
                .find_map(|(k, v)| walk_expr(name, k, et).or_else(|| walk_expr(name, v, et))),
            ExprKind::Tuple(xs) => xs.iter().find_map(|x| walk_expr(name, x, et)),
            ExprKind::StructLit { fields, .. } => {
                fields.iter().find_map(|(_, v)| walk_expr(name, v, et))
            }
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
            if let Some(t) = walk_stmt(name, s, et) {
                return Some(t);
            }
        }
        b.tail_expr.as_ref().and_then(|e| walk_expr(name, e, et))
    }
    fn walk_stmt(name: &str, s: &Stmt, et: &std::collections::HashMap<Span, Ty>) -> Option<Ty> {
        match s {
            Stmt::Decl { value, .. } => walk_expr(name, value, et),
            Stmt::Assign { target, value, .. } => {
                walk_expr(name, target, et).or_else(|| walk_expr(name, value, et))
            }
            Stmt::Expr(e) => walk_expr(name, e, et),
            Stmt::Return { value: Some(e), .. } => walk_expr(name, e, et),
            Stmt::If(i) => {
                walk_expr(name, &i.cond, et).or_else(|| walk_block(name, &i.then_branch, et))
            }
            Stmt::Loop(l) => match &l.head {
                Some(LoopHead::ForIn { iter, .. }) => {
                    walk_expr(name, iter, et).or_else(|| walk_block(name, &l.body, et))
                }
                Some(LoopHead::While(c)) => {
                    walk_expr(name, c, et).or_else(|| walk_block(name, &l.body, et))
                }
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

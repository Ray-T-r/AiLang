//! Collection passes: generic-instance discovery and lambda lifting.
use crate::typegen::*;
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

pub(crate) fn collect_calls_block(
    b: &Block,
    generic_names: &std::collections::HashSet<String>,
    generic_fns: &std::collections::HashMap<&String, &FnDecl>,
    res: &ResolvedModule,
    out: &mut std::collections::BTreeMap<String, std::collections::BTreeSet<Vec<Ty>>>,
) {
    for s in &b.stmts {
        collect_calls_stmt(s, generic_names, generic_fns, res, out);
    }
    if let Some(t) = &b.tail_expr {
        collect_calls_expr(t, generic_names, generic_fns, res, out);
    }
}

pub(crate) fn collect_calls_stmt(
    s: &Stmt,
    generic_names: &std::collections::HashSet<String>,
    generic_fns: &std::collections::HashMap<&String, &FnDecl>,
    res: &ResolvedModule,
    out: &mut std::collections::BTreeMap<String, std::collections::BTreeSet<Vec<Ty>>>,
) {
    match s {
        Stmt::Decl { value, .. } => collect_calls_expr(value, generic_names, generic_fns, res, out),
        Stmt::Assign { target, value, .. } => {
            collect_calls_expr(target, generic_names, generic_fns, res, out);
            collect_calls_expr(value, generic_names, generic_fns, res, out);
        }
        Stmt::Expr(e) => collect_calls_expr(e, generic_names, generic_fns, res, out),
        Stmt::Return { value: Some(e), .. } => {
            collect_calls_expr(e, generic_names, generic_fns, res, out)
        }
        Stmt::If(i) => {
            collect_calls_expr(&i.cond, generic_names, generic_fns, res, out);
            collect_calls_block(&i.then_branch, generic_names, generic_fns, res, out);
            match &i.else_branch {
                Some(ElseBranch::Block(b)) => {
                    collect_calls_block(b, generic_names, generic_fns, res, out)
                }
                Some(ElseBranch::If(inner)) => collect_calls_stmt(
                    &Stmt::If((**inner).clone()),
                    generic_names,
                    generic_fns,
                    res,
                    out,
                ),
                None => {}
            }
        }
        Stmt::Loop(l) => {
            match &l.head {
                Some(LoopHead::ForIn { iter, .. }) => {
                    collect_calls_expr(iter, generic_names, generic_fns, res, out)
                }
                Some(LoopHead::While(c)) => {
                    collect_calls_expr(c, generic_names, generic_fns, res, out)
                }
                None => {}
            }
            collect_calls_block(&l.body, generic_names, generic_fns, res, out);
        }
        Stmt::Match(m) => {
            collect_calls_expr(&m.scrutinee, generic_names, generic_fns, res, out);
            for arm in &m.arms {
                collect_calls_expr(&arm.body, generic_names, generic_fns, res, out);
            }
        }
        _ => {}
    }
}

pub(crate) fn collect_calls_expr(
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
        for a in args {
            collect_calls_expr(a, generic_names, generic_fns, res, out);
        }
        return;
    }
    match &e.kind {
        ExprKind::Index { container, index } => {
            collect_calls_expr(container, generic_names, generic_fns, res, out);
            collect_calls_expr(index, generic_names, generic_fns, res, out);
        }
        ExprKind::Field { container, .. } => {
            collect_calls_expr(container, generic_names, generic_fns, res, out)
        }
        ExprKind::Binary { lhs, rhs, .. } => {
            collect_calls_expr(lhs, generic_names, generic_fns, res, out);
            collect_calls_expr(rhs, generic_names, generic_fns, res, out);
        }
        ExprKind::Unary { operand, .. } => {
            collect_calls_expr(operand, generic_names, generic_fns, res, out)
        }
        ExprKind::Ternary { cond, then_, else_ } => {
            collect_calls_expr(cond, generic_names, generic_fns, res, out);
            collect_calls_expr(then_, generic_names, generic_fns, res, out);
            collect_calls_expr(else_, generic_names, generic_fns, res, out);
        }
        ExprKind::Pipe { lhs, rhs } => {
            collect_calls_expr(lhs, generic_names, generic_fns, res, out);
            collect_calls_expr(rhs, generic_names, generic_fns, res, out);
        }
        ExprKind::Array(xs) => {
            for x in xs {
                collect_calls_expr(x, generic_names, generic_fns, res, out);
            }
        }
        ExprKind::Map(es) => {
            for (k, v) in es {
                collect_calls_expr(k, generic_names, generic_fns, res, out);
                collect_calls_expr(v, generic_names, generic_fns, res, out);
            }
        }
        ExprKind::Tuple(xs) => {
            for x in xs {
                collect_calls_expr(x, generic_names, generic_fns, res, out);
            }
        }
        ExprKind::StructLit { fields, .. } => {
            for (_, v) in fields {
                collect_calls_expr(v, generic_names, generic_fns, res, out);
            }
        }
        ExprKind::Block(b) => collect_calls_block(b, generic_names, generic_fns, res, out),
        ExprKind::Try(inner) => collect_calls_expr(inner, generic_names, generic_fns, res, out),
        ExprKind::Lambda { body, .. } => match body {
            LambdaBody::Expr(inner) => {
                collect_calls_expr(inner, generic_names, generic_fns, res, out)
            }
            LambdaBody::Block(b) => collect_calls_block(b, generic_names, generic_fns, res, out),
        },
        _ => {}
    }
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
    match b {
        LambdaBody::Expr(e) => collect_refs(e, &mut out),
        LambdaBody::Block(blk) => collect_refs_block(blk, &mut out),
    }
    out
}

pub(crate) fn collect_refs_block(b: &Block, out: &mut std::collections::HashSet<String>) {
    for s in &b.stmts {
        match s {
            Stmt::Decl { value, .. } => collect_refs(value, out),
            Stmt::Expr(e) => collect_refs(e, out),
            Stmt::Assign { target, value, .. } => {
                collect_refs(target, out);
                collect_refs(value, out);
            }
            Stmt::Return { value: Some(e), .. } => collect_refs(e, out),
            Stmt::If(i) => {
                collect_refs(&i.cond, out);
                collect_refs_block(&i.then_branch, out);
                match &i.else_branch {
                    Some(ElseBranch::Block(b)) => collect_refs_block(b, out),
                    Some(ElseBranch::If(inner)) => {
                        collect_refs_stmt(&Stmt::If((**inner).clone()), out)
                    }
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
                for arm in &m.arms {
                    collect_refs(&arm.body, out);
                }
            }
            _ => {}
        }
    }
    if let Some(t) = &b.tail_expr {
        collect_refs(t, out);
    }
}

pub(crate) fn collect_refs_stmt(s: &Stmt, out: &mut std::collections::HashSet<String>) {
    let mut tmp = Block {
        stmts: vec![s.clone()],
        tail_expr: None,
        span: Span::empty(),
    };
    collect_refs_block(&mut tmp, out);
}

pub(crate) fn collect_refs(e: &Expr, out: &mut std::collections::HashSet<String>) {
    match &e.kind {
        ExprKind::Ident(n) => {
            out.insert(n.clone());
        }
        ExprKind::Call { callee, args } => {
            collect_refs(callee, out);
            for a in args {
                collect_refs(a, out);
            }
        }
        ExprKind::Index { container, index } => {
            collect_refs(container, out);
            collect_refs(index, out);
        }
        ExprKind::Field { container, .. } => collect_refs(container, out),
        ExprKind::Binary { lhs, rhs, .. } => {
            collect_refs(lhs, out);
            collect_refs(rhs, out);
        }
        ExprKind::Unary { operand, .. } => collect_refs(operand, out),
        ExprKind::Ternary { cond, then_, else_ } => {
            collect_refs(cond, out);
            collect_refs(then_, out);
            collect_refs(else_, out);
        }
        ExprKind::Pipe { lhs, rhs } => {
            collect_refs(lhs, out);
            collect_refs(rhs, out);
        }
        ExprKind::Lambda { body, .. } => {
            // Nested lambdas: their body's references also reach up to us.
            match body {
                LambdaBody::Expr(inner) => collect_refs(inner, out),
                LambdaBody::Block(b) => collect_refs_block(b, out),
            }
        }
        ExprKind::Array(xs) => {
            for x in xs {
                collect_refs(x, out);
            }
        }
        ExprKind::Map(es) => {
            for (k, v) in es {
                collect_refs(k, out);
                collect_refs(v, out);
            }
        }
        ExprKind::Tuple(xs) => {
            for x in xs {
                collect_refs(x, out);
            }
        }
        ExprKind::StructLit { fields, .. } => {
            for (_, v) in fields {
                collect_refs(v, out);
            }
        }
        ExprKind::Block(b) => collect_refs_block(b, out),
        ExprKind::Try(inner) => collect_refs(inner, out),
        _ => {}
    }
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

//! Semantic analysis — minimal version for M2.
//!
//! Goal for this milestone: just enough name resolution and type tagging to
//! let codegen produce correct C. We:
//!
//! - Collect top-level `fn` and `ex fn` signatures into a symbol table.
//! - Resolve identifier references in each function body.
//! - Infer types for every expression using a bidirectional walk, but treat
//!   conflicts as warnings rather than hard errors (M2 is permissive — the
//!   stricter checker lands once HIR exists in M3+).
//! - Reject genuinely unrecoverable shape errors (undefined name, wrong arg
//!   count, etc).
//!
//! Output: [`ResolvedModule`] — the original AST plus side tables
//! (`fn_table`, `type_of_expr`).

use std::collections::HashMap;

use ailang_diag::Diagnostic;
use ailang_syntax::ast::*;
use ailang_syntax::token::Span;

#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub enum Ty {
    Unit,
    I64,
    F64,
    Bool,
    Str,
    /// Homogeneous array `[T]`. For M4+ we only generate code for the
    /// `[i64]` and `[str]` instantiations.
    Array(Box<Ty>),
    /// Map `{K:V}`. M4+ ships only `{i64:i64}`; other instantiations are
    /// follow-up work.
    Map(Box<Ty>, Box<Ty>),
    /// Catch-all when we can't determine more precisely. Codegen falls back
    /// to `int64_t` and prints a warning.
    Unknown,
}

#[derive(Clone, Debug)]
pub struct FnSigResolved {
    pub name: String,
    pub params: Vec<(String, Ty)>,
    pub variadic: bool,
    pub return_ty: Ty,
    /// `true` when this came from `ex` (no body, C calling convention).
    pub is_extern: bool,
    /// Library to link, only meaningful when `is_extern`.
    pub extern_lib: Option<String>,
    pub span: Span,
}

pub struct ResolvedModule {
    pub module: Module,
    pub fn_table: HashMap<String, FnSigResolved>,
    /// Side table: an expression's `Span` → its inferred `Ty`. Keyed by the
    /// full span (start + end) so an outer expression and its first child
    /// — which share `span.start` — don't collide.
    /// Populated by the body-checking pass. Codegen consults this to decide
    /// e.g. whether `lp x in arr` should treat `arr` as `[i64]` or `[str]`.
    pub expr_types: HashMap<Span, Ty>,
}

pub fn analyze(module: Module) -> (ResolvedModule, Vec<Diagnostic>) {
    let mut diags = Vec::new();
    let mut fn_table: HashMap<String, FnSigResolved> = HashMap::new();

    // Built-in functions — registered before user code so user can call them
    // and so codegen knows their signatures.
    register_builtins(&mut fn_table);

    // Pass 1: collect signatures.
    for item in &module.items {
        match item {
            Item::Fn(f) => {
                // Initial pass: use annotated return type or `Unit` placeholder.
                // The pass-1.5 below revisits unannotated returns once all
                // signatures are visible (so we can tell `println` is void).
                let initial_ret = f.return_ty.as_ref().map(ast_ty_kind_to_ty).unwrap_or(Ty::Unit);
                let sig = FnSigResolved {
                    name: f.name.name.clone(),
                    params: f.params.iter().map(|p| (p.name.name.clone(), ast_ty_to_ty(p.ty.as_ref()))).collect(),
                    variadic: false,
                    return_ty: initial_ret,
                    is_extern: false,
                    extern_lib: None,
                    span: f.span,
                };
                if let Some(prev) = fn_table.insert(f.name.name.clone(), sig) {
                    diags.push(
                        Diagnostic::error(
                            format!("duplicate definition of `{}`", f.name.name),
                            f.name.span,
                        )
                        .with_help(format!("first defined at {:?}", prev.span)),
                    );
                }
            }
            Item::Extern(e) => {
                let sig = FnSigResolved {
                    name: e.sig.name.name.clone(),
                    params: e.sig.params.iter()
                        .map(|p| (p.name.name.clone(), ast_ty_to_ty(p.ty.as_ref())))
                        .collect(),
                    variadic: e.sig.variadic,
                    return_ty: e.sig.return_ty.as_ref().map(ast_ty_kind_to_ty).unwrap_or(Ty::Unit),
                    is_extern: true,
                    extern_lib: e.lib.as_ref().map(|s| s.value.clone()),
                    span: e.span,
                };
                fn_table.insert(e.sig.name.name.clone(), sig);
            }
            Item::Struct(_) | Item::Import(_) => {
                // Structs/imports not wired through M2; ignore for now.
            }
        }
    }

    // Pass 1.5: re-infer return types for unannotated functions, now that
    // all signatures (including builtins) are visible. A function whose body
    // ends in a `void`-returning call (e.g. `println`) stays `Unit`; one
    // ending in a string-producing expression becomes `Str`; everything else
    // value-returning defaults to `I64`.
    let mut updates: Vec<(String, Ty)> = Vec::new();
    for item in &module.items {
        if let Item::Fn(f) = item {
            if f.return_ty.is_none() {
                let inferred = infer_fn_ret(f, &fn_table);
                updates.push((f.name.name.clone(), inferred));
            }
        }
    }
    for (name, ty) in updates {
        if let Some(sig) = fn_table.get_mut(&name) {
            sig.return_ty = ty;
        }
    }

    // Pass 2: name-resolve every function body. We also harvest per-expression
    // inferred types so codegen can ask "what's the type of this identifier?"
    // for cases the AST shape alone can't decide (e.g. `lp x in arr`).
    let mut expr_types: HashMap<Span, Ty> = HashMap::new();
    for item in &module.items {
        if let Item::Fn(f) = item {
            let mut env = LocalEnv::new();
            for p in &f.params {
                env.insert(p.name.name.clone(), ast_ty_to_ty(p.ty.as_ref()));
            }
            check_block(&f.body, &fn_table, &mut env, &mut diags);
            expr_types.extend(env.expr_types);
        }
    }

    // Require a `main` for executable output. (Libraries are post-MVP.)
    if !fn_table.contains_key("main") {
        diags.push(Diagnostic::error(
            "no `main` function defined",
            Span::empty(),
        ));
    }

    (ResolvedModule { module, fn_table, expr_types }, diags)
}

fn register_builtins(fn_table: &mut HashMap<String, FnSigResolved>) {
    // print / println accept any single arg; we type-check loosely.
    for name in ["print", "println"] {
        fn_table.insert(
            name.to_string(),
            FnSigResolved {
                name: name.to_string(),
                params: vec![("v".to_string(), Ty::Unknown)],
                variadic: false,
                return_ty: Ty::Unit,
                is_extern: false,
                extern_lib: None,
                span: Span::empty(),
            },
        );
    }
    // len(str) -> i64. Codegen lowers via a C11 _Generic so future array
    // overloads slot in without changes here.
    fn_table.insert(
        "len".to_string(),
        FnSigResolved {
            name: "len".to_string(),
            params: vec![("s".to_string(), Ty::Str)],
            variadic: false,
            return_ty: Ty::I64,
            is_extern: false,
            extern_lib: None,
            span: Span::empty(),
        },
    );
    // `has(map, key) -> bool` — map membership check.
    fn_table.insert(
        "has".to_string(),
        FnSigResolved {
            name: "has".to_string(),
            params: vec![("m".to_string(), Ty::Unknown), ("k".to_string(), Ty::Unknown)],
            variadic: false,
            return_ty: Ty::Bool,
            is_extern: false,
            extern_lib: None,
            span: Span::empty(),
        },
    );
}

struct LocalEnv {
    scopes: Vec<HashMap<String, Ty>>,
    /// Per-expression inferred type, collected as we walk the function body.
    /// Merged into `ResolvedModule.expr_types` after each function checks.
    expr_types: HashMap<Span, Ty>,
}

impl LocalEnv {
    fn new() -> Self {
        Self { scopes: vec![HashMap::new()], expr_types: HashMap::new() }
    }
    fn push(&mut self) {
        self.scopes.push(HashMap::new());
    }
    fn pop(&mut self) {
        self.scopes.pop();
    }
    fn insert(&mut self, name: String, ty: Ty) {
        self.scopes.last_mut().unwrap().insert(name, ty);
    }
    fn lookup(&self, name: &str) -> Option<&Ty> {
        self.scopes.iter().rev().find_map(|s| s.get(name))
    }
    fn record(&mut self, span: Span, ty: Ty) {
        self.expr_types.insert(span, ty);
    }
}

fn check_block(
    block: &Block,
    fns: &HashMap<String, FnSigResolved>,
    env: &mut LocalEnv,
    diags: &mut Vec<Diagnostic>,
) {
    env.push();
    for stmt in &block.stmts {
        check_stmt(stmt, fns, env, diags);
    }
    if let Some(e) = &block.tail_expr {
        check_expr(e, fns, env, diags);
    }
    env.pop();
}

fn check_stmt(
    stmt: &Stmt,
    fns: &HashMap<String, FnSigResolved>,
    env: &mut LocalEnv,
    diags: &mut Vec<Diagnostic>,
) {
    match stmt {
        Stmt::Decl { name, ty, value, .. } => {
            let inferred = check_expr(value, fns, env, diags);
            let final_ty = ty.as_ref().map(ast_ty_kind_to_ty).unwrap_or(inferred);
            env.insert(name.name.clone(), final_ty);
        }
        Stmt::Assign { target, value, .. } => {
            check_expr(target, fns, env, diags);
            check_expr(value, fns, env, diags);
        }
        Stmt::Expr(e) => {
            check_expr(e, fns, env, diags);
        }
        Stmt::Return { value, .. } => {
            if let Some(e) = value {
                check_expr(e, fns, env, diags);
            }
        }
        Stmt::Break(_) | Stmt::Continue(_) => {}
        Stmt::If(if_) => check_if(if_, fns, env, diags),
        Stmt::Loop(lp) => check_loop(lp, fns, env, diags),
        Stmt::Match(mt) => check_match(mt, fns, env, diags),
    }
}

fn check_if(
    if_: &IfStmt,
    fns: &HashMap<String, FnSigResolved>,
    env: &mut LocalEnv,
    diags: &mut Vec<Diagnostic>,
) {
    check_expr(&if_.cond, fns, env, diags);
    check_block(&if_.then_branch, fns, env, diags);
    match &if_.else_branch {
        Some(ElseBranch::Block(b)) => check_block(b, fns, env, diags),
        Some(ElseBranch::If(inner)) => check_if(inner, fns, env, diags),
        None => {}
    }
}

fn check_loop(
    lp: &LoopStmt,
    fns: &HashMap<String, FnSigResolved>,
    env: &mut LocalEnv,
    diags: &mut Vec<Diagnostic>,
) {
    env.push();
    match &lp.head {
        Some(LoopHead::ForIn { var, iter }) => {
            check_expr(iter, fns, env, diags);
            // Range elements are i64 (M2 supports only integer ranges).
            env.insert(var.name.clone(), Ty::I64);
        }
        Some(LoopHead::While(cond)) => {
            check_expr(cond, fns, env, diags);
        }
        None => {}
    }
    check_block(&lp.body, fns, env, diags);
    env.pop();
}

fn check_match(
    mt: &MatchStmt,
    fns: &HashMap<String, FnSigResolved>,
    env: &mut LocalEnv,
    diags: &mut Vec<Diagnostic>,
) {
    check_expr(&mt.scrutinee, fns, env, diags);
    for arm in &mt.arms {
        env.push();
        bind_pattern(&arm.pattern, env);
        check_expr(&arm.body, fns, env, diags);
        env.pop();
    }
}

fn bind_pattern(p: &Pattern, env: &mut LocalEnv) {
    match p {
        Pattern::Wildcard(_) | Pattern::Literal(_) => {}
        Pattern::Binding(id) => env.insert(id.name.clone(), Ty::Unknown),
        Pattern::Tuple { elems, .. } => {
            for e in elems {
                bind_pattern(e, env);
            }
        }
    }
}

fn check_expr(
    e: &Expr,
    fns: &HashMap<String, FnSigResolved>,
    env: &mut LocalEnv,
    diags: &mut Vec<Diagnostic>,
) -> Ty {
    let ty = check_expr_inner(e, fns, env, diags);
    env.record(e.span, ty.clone());
    ty
}

fn check_expr_inner(
    e: &Expr,
    fns: &HashMap<String, FnSigResolved>,
    env: &mut LocalEnv,
    diags: &mut Vec<Diagnostic>,
) -> Ty {
    match &e.kind {
        ExprKind::Lit(l) => match &l.kind {
            Lit::Int { .. } => Ty::I64,
            Lit::Float { .. } => Ty::F64,
            Lit::Str(_) => Ty::Str,
            Lit::Char(_) => Ty::I64,    // M2 simplification
            Lit::Bool(_) => Ty::Bool,
            Lit::Nil => Ty::Unknown,
        },
        ExprKind::Ident(name) => {
            if let Some(t) = env.lookup(name) {
                t.clone()
            } else if fns.contains_key(name) {
                Ty::Unknown // function references — value-level not used much
            } else {
                diags.push(Diagnostic::error(
                    format!("undefined name `{name}`"),
                    e.span,
                ));
                Ty::Unknown
            }
        }
        ExprKind::Underscore => Ty::Unknown,
        ExprKind::Call { callee, args } => {
            // Only direct calls by name supported in M2.
            let ExprKind::Ident(fname) = &callee.kind else {
                diags.push(Diagnostic::error(
                    "only direct calls `f(...)` are supported in this milestone",
                    callee.span,
                ));
                for a in args { check_expr(a, fns, env, diags); }
                return Ty::Unknown;
            };
            let sig = match fns.get(fname) {
                Some(s) => s.clone(),
                None => {
                    diags.push(Diagnostic::error(
                        format!("call to undefined function `{fname}`"),
                        callee.span,
                    ));
                    for a in args { check_expr(a, fns, env, diags); }
                    return Ty::Unknown;
                }
            };
            // Arg count: variadic allows >=, otherwise ==.
            let ok = if sig.variadic {
                args.len() >= sig.params.len()
            } else {
                args.len() == sig.params.len()
            };
            if !ok {
                diags.push(Diagnostic::error(
                    format!(
                        "wrong number of arguments to `{fname}`: expected {}{}, got {}",
                        sig.params.len(),
                        if sig.variadic { "+" } else { "" },
                        args.len(),
                    ),
                    callee.span,
                ));
            }
            for a in args { check_expr(a, fns, env, diags); }
            sig.return_ty.clone()
        }
        ExprKind::Binary { op, lhs, rhs } => {
            let lt = check_expr(lhs, fns, env, diags);
            let rt = check_expr(rhs, fns, env, diags);
            binary_result_ty(*op, &lt, &rt)
        }
        ExprKind::Unary { op, operand } => {
            let t = check_expr(operand, fns, env, diags);
            match op {
                UnOp::Neg => t,
                UnOp::Not => Ty::Bool,
                UnOp::Deref | UnOp::AddrOf => Ty::Unknown,
            }
        }
        ExprKind::Ternary { cond, then_, else_ } => {
            check_expr(cond, fns, env, diags);
            let t = check_expr(then_, fns, env, diags);
            let _ = check_expr(else_, fns, env, diags);
            t
        }
        ExprKind::Pipe { lhs, rhs } => {
            // `x |> f(a, b)` desugars to `f(x, a, b)` — the lhs counts as
            // the first argument for arity checks. So we don't recurse into
            // `rhs` via the normal Call path (which would see one fewer arg).
            check_expr(lhs, fns, env, diags);
            match &rhs.kind {
                ExprKind::Call { callee, args } => {
                    let ExprKind::Ident(fname) = &callee.kind else {
                        diags.push(Diagnostic::error(
                            "right side of `|>` must be a direct function call",
                            callee.span,
                        ));
                        for a in args { check_expr(a, fns, env, diags); }
                        return Ty::Unknown;
                    };
                    let sig = match fns.get(fname) {
                        Some(s) => s.clone(),
                        None => {
                            diags.push(Diagnostic::error(
                                format!("call to undefined function `{fname}`"),
                                callee.span,
                            ));
                            for a in args { check_expr(a, fns, env, diags); }
                            return Ty::Unknown;
                        }
                    };
                    // arity check counting the piped value as one arg.
                    let supplied = args.len() + 1;
                    let ok = if sig.variadic {
                        supplied >= sig.params.len()
                    } else {
                        supplied == sig.params.len()
                    };
                    if !ok {
                        diags.push(Diagnostic::error(
                            format!(
                                "wrong number of arguments to `{fname}`: expected {}{}, got {} (including the piped value)",
                                sig.params.len(),
                                if sig.variadic { "+" } else { "" },
                                supplied,
                            ),
                            callee.span,
                        ));
                    }
                    for a in args { check_expr(a, fns, env, diags); }
                    sig.return_ty.clone()
                }
                ExprKind::Ident(fname) => {
                    let sig = match fns.get(fname) {
                        Some(s) => s.clone(),
                        None => {
                            diags.push(Diagnostic::error(
                                format!("call to undefined function `{fname}`"),
                                rhs.span,
                            ));
                            return Ty::Unknown;
                        }
                    };
                    // 1 piped arg only.
                    let ok = if sig.variadic {
                        sig.params.len() <= 1
                    } else {
                        sig.params.len() == 1
                    };
                    if !ok {
                        diags.push(Diagnostic::error(
                            format!(
                                "`|> {fname}` passes 1 argument but `{fname}` expects {}",
                                sig.params.len(),
                            ),
                            rhs.span,
                        ));
                    }
                    sig.return_ty.clone()
                }
                _ => {
                    diags.push(Diagnostic::error(
                        "right side of `|>` must be a function name or call",
                        rhs.span,
                    ));
                    Ty::Unknown
                }
            }
        }
        ExprKind::Index { container, index } => {
            let ct = check_expr(container, fns, env, diags);
            check_expr(index, fns, env, diags);
            match ct {
                Ty::Array(elem) => *elem,
                Ty::Map(_, v) => *v,
                Ty::Str => Ty::I64,    // indexing a string yields a byte
                _ => Ty::Unknown,
            }
        }
        ExprKind::Field { container, .. } => {
            check_expr(container, fns, env, diags);
            Ty::Unknown
        }
        ExprKind::If(if_) => {
            check_if(if_, fns, env, diags);
            Ty::Unknown
        }
        ExprKind::Match(mt) => {
            check_match(mt, fns, env, diags);
            Ty::Unknown
        }
        ExprKind::Block(b) => {
            check_block(b, fns, env, diags);
            b.tail_expr
                .as_ref()
                .map(|t| check_expr(t, fns, env, diags))
                .unwrap_or(Ty::Unit)
        }
        ExprKind::Array(xs) => {
            // Take the first element's type as the array's element type. For
            // M4+ we assume homogeneous arrays; mixed-type arrays just fall
            // back to `[Unknown]` and the codegen will refuse to specialize.
            let elem = xs
                .first()
                .map(|x| check_expr(x, fns, env, diags))
                .unwrap_or(Ty::Unknown);
            for x in xs.iter().skip(1) {
                check_expr(x, fns, env, diags);
            }
            Ty::Array(Box::new(elem))
        }
        ExprKind::Map(entries) => {
            let (kt, vt) = match entries.first() {
                Some((k, v)) => (check_expr(k, fns, env, diags), check_expr(v, fns, env, diags)),
                None => (Ty::I64, Ty::I64), // empty `{}` defaults to {i64:i64}
            };
            for (k, v) in entries.iter().skip(1) {
                check_expr(k, fns, env, diags);
                check_expr(v, fns, env, diags);
            }
            Ty::Map(Box::new(kt), Box::new(vt))
        }
        ExprKind::Tuple(xs) => {
            for x in xs {
                check_expr(x, fns, env, diags);
            }
            Ty::Unknown
        }
        ExprKind::Lambda { .. } => {
            diags.push(Diagnostic::error(
                "lambdas are not supported in this milestone",
                e.span,
            ));
            Ty::Unknown
        }
        ExprKind::Try(inner) => {
            check_expr(inner, fns, env, diags);
            Ty::Unknown
        }
    }
}

fn binary_result_ty(op: BinOp, lt: &Ty, rt: &Ty) -> Ty {
    use BinOp::*;
    match op {
        // Unified `+`: string concat if either operand is a string, else numeric.
        Add if *lt == Ty::Str || *rt == Ty::Str => Ty::Str,
        Add | Sub | Mul | Div | Mod | BitAnd | BitOr | BitXor | Shl | Shr => {
            if *lt == Ty::F64 { Ty::F64 } else if *lt == Ty::I64 { Ty::I64 } else { Ty::Unknown }
        }
        Concat => Ty::Str,
        Eq | Ne | Lt | Le | Gt | Ge | And | Or => Ty::Bool,
        Range | RangeEq => Ty::Unknown, // iterator type
        Coalesce => lt.clone(),
    }
}

fn ast_ty_to_ty(t: Option<&Type>) -> Ty {
    // Unannotated params default to `i64` — the most common case by a wide
    // margin, and lets `fn fib(n)` parse and type-check without a type stutter.
    t.map(ast_ty_kind_to_ty).unwrap_or(Ty::I64)
}

/// True if the function body produces a value — i.e. has a tail expression
/// (whose computed type is non-unit) or any `rt expr` statement reachable
/// from the body. Used by sema and codegen to choose between `int64_t`
/// (default) and `void` for unannotated returns.
///
/// `fn_table` is consulted so calls to `void`-returning functions (e.g.
/// `println`) in the tail position don't get misinterpreted as a value.
pub fn fn_returns_value(b: &Block, fn_table: &HashMap<String, FnSigResolved>) -> bool {
    if let Some(t) = &b.tail_expr {
        if expr_yields_value(t, fn_table) {
            return true;
        }
    }
    b.stmts.iter().any(|s| stmt_yields_value(s, fn_table))
}

fn stmt_yields_value(s: &Stmt, fn_table: &HashMap<String, FnSigResolved>) -> bool {
    match s {
        Stmt::Return { value: Some(_), .. } => true,
        Stmt::If(if_) => {
            fn_returns_value(&if_.then_branch, fn_table)
                || match &if_.else_branch {
                    Some(ElseBranch::Block(b)) => fn_returns_value(b, fn_table),
                    Some(ElseBranch::If(inner)) => {
                        stmt_yields_value(&Stmt::If((**inner).clone()), fn_table)
                    }
                    None => false,
                }
        }
        Stmt::Loop(l) => fn_returns_value(&l.body, fn_table),
        _ => false,
    }
}

/// Infer the return type of a function with no annotation, using the fn body
/// and the surrounding `fn_table`. Returns `Ty::Unit` for void-returning bodies,
/// `Ty::Str` for string-producing tails, and `Ty::I64` for anything else that
/// computes a value.
fn infer_fn_ret(f: &FnDecl, fn_table: &HashMap<String, FnSigResolved>) -> Ty {
    // Look at the body's tail expression (or trailing `rt expr`) and ask sema
    // for its type using a param-aware local env.
    let mut env = LocalEnv::new();
    for p in &f.params {
        env.insert(p.name.name.clone(), ast_ty_to_ty(p.ty.as_ref()));
    }
    let mut diags = Vec::new(); // discarded — we only want the ty
    if let Some(t) = inferred_tail_ty(&f.body, fn_table, &mut env, &mut diags) {
        if matches!(t, Ty::Unit) { Ty::Unit } else { t }
    } else if fn_returns_value(&f.body, fn_table) {
        Ty::I64
    } else {
        Ty::Unit
    }
}

fn inferred_tail_ty(
    b: &Block,
    fn_table: &HashMap<String, FnSigResolved>,
    env: &mut LocalEnv,
    diags: &mut Vec<Diagnostic>,
) -> Option<Ty> {
    // We only look at the immediate tail; nested control flow stays I64.
    if let Some(t) = &b.tail_expr {
        let ty = check_expr(t, fn_table, env, diags);
        return Some(ty);
    }
    // `rt expr` at the end of the body also tells us.
    if let Some(last) = b.stmts.last() {
        if let Stmt::Return { value: Some(e), .. } = last {
            return Some(check_expr(e, fn_table, env, diags));
        }
    }
    None
}

/// Quick syntactic check: does this expression evaluate to a non-unit value?
/// Calls to known unit-returning functions are the only case treated as unit;
/// everything else is assumed to produce a value.
fn expr_yields_value(e: &Expr, fn_table: &HashMap<String, FnSigResolved>) -> bool {
    match &e.kind {
        ExprKind::Call { callee, .. } => {
            if let ExprKind::Ident(name) = &callee.kind {
                if let Some(sig) = fn_table.get(name) {
                    return !matches!(sig.return_ty, Ty::Unit);
                }
            }
            true
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
                if let Some(sig) = fn_table.get(name) {
                    return !matches!(sig.return_ty, Ty::Unit);
                }
            }
            true
        }
        _ => true,
    }
}

pub fn ast_ty_kind_to_ty(t: &Type) -> Ty {
    match &t.kind {
        TypeKind::Path(name) => match name.as_str() {
            "i8" | "i16" | "i32" | "i64"
            | "u8" | "u16" | "u32" | "u64" => Ty::I64,
            "f32" | "f64" => Ty::F64,
            "bool" => Ty::Bool,
            "str" => Ty::Str,
            _ => Ty::Unknown,
        },
        TypeKind::Array(inner) => Ty::Array(Box::new(ast_ty_kind_to_ty(inner))),
        TypeKind::Map(k, v) => Ty::Map(Box::new(ast_ty_kind_to_ty(k)), Box::new(ast_ty_kind_to_ty(v))),
        TypeKind::Ptr(_) => Ty::Unknown, // FFI pointers handled by codegen specially
        _ => Ty::Unknown,
    }
}

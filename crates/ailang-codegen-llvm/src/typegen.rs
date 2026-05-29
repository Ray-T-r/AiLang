//! Type lowering: AiLang types -> C type spellings, plus generic substitution.
use crate::emit::*;
use crate::names::*;
use ailang_sema::Ty;
use ailang_syntax::ast::*;

pub(crate) fn is_primitive_ty(t: &Ty) -> bool {
    matches!(t, Ty::I64 | Ty::F64 | Ty::Bool | Ty::Str)
}

/// One-shot unification: walks the param AST type alongside the arg Ty,
/// recording type-var → concrete-type bindings. Mismatched shapes are
/// ignored (caller decides what to do with an incomplete subst).
pub(crate) fn unify_ty(
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
pub(crate) fn substitute_fn_decl(f: &FnDecl, type_args: &[Ty]) -> FnDecl {
    let subst: std::collections::HashMap<String, Ty> = f
        .type_params
        .iter()
        .zip(type_args.iter().cloned())
        .map(|(p, t)| (p.name.clone(), t))
        .collect();
    let mangled = format!("{}_{}", f.name.name, mangle_ty_suffix(type_args));
    FnDecl {
        name: Ident {
            name: mangled,
            span: f.name.span,
        },
        type_params: Vec::new(),
        params: f
            .params
            .iter()
            .map(|p| Param {
                name: p.name.clone(),
                ty: p.ty.as_ref().map(|t| substitute_ty(t, &subst)),
                span: p.span,
            })
            .collect(),
        return_ty: f.return_ty.as_ref().map(|t| substitute_ty(t, &subst)),
        body: f.body.clone(),
        span: f.span,
    }
}

pub(crate) fn substitute_ty(t: &Type, subst: &std::collections::HashMap<String, Ty>) -> Type {
    let kind = match &t.kind {
        TypeKind::Path(name) => match subst.get(name) {
            Some(ty) => TypeKind::Path(ty_to_path_name(ty).to_string()),
            None => TypeKind::Path(name.clone()),
        },
        TypeKind::Array(inner) => TypeKind::Array(Box::new(substitute_ty(inner, subst))),
        TypeKind::Map(k, v) => TypeKind::Map(
            Box::new(substitute_ty(k, subst)),
            Box::new(substitute_ty(v, subst)),
        ),
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

pub(crate) fn ty_to_path_name(t: &Ty) -> &'static str {
    match t {
        Ty::I64 => "i64",
        Ty::F64 => "f64",
        Ty::Bool => "bool",
        Ty::Str => "str",
        _ => "i64",
    }
}

pub(crate) fn mangle_ty_suffix(types: &[Ty]) -> String {
    types
        .iter()
        .map(|t| ty_to_path_name(t))
        .collect::<Vec<_>>()
        .join("_")
}

pub(crate) fn c_ty_for_ret(ty: Option<&Type>) -> String {
    match ty {
        None => "void".to_string(),
        Some(t) => c_ty_from_ast(t),
    }
}

pub(crate) fn c_ty_for_param(ty: Option<&Type>) -> String {
    // Default unannotated lambda params to int64_t.
    match ty {
        None => "int64_t".to_string(),
        Some(t) => c_ty_from_ast(t),
    }
}

/// AST-level type → C type. Used for extern declarations and fn signatures
/// where the *exact* integer width matters (vs the sema `Ty` which collapses
/// every fixed-width integer to a single bucket for permissive checking).
pub(crate) fn c_ty_from_ast(t: &Type) -> String {
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
            "bytes" => "ailang_bytes".to_string(),
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
            TypeKind::Path(n) if n == "str" => "ailang_result_str".to_string(),
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

pub(crate) fn c_ty_for_decl(ty: Option<&Type>, init: &Expr, ctx: &EmitCtx) -> String {
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
    // For a plain identifier initializer (`mu x := y`), defer to sema's
    // recorded type for the right-hand side — the AST-shape fallback can't
    // recover the type from a name alone.
    if let ExprKind::Ident(_) = &init.kind {
        if let Some(t) = ctx.fns.expr_types.get(&init.span) {
            if !matches!(t, Ty::Unknown) {
                return c_ty_for(t);
            }
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
        ExprKind::Array(xs) => {
            // Sema's refined type wins (catches empty `[]` whose element
            // type was pinned by later usage).
            if let Some(Ty::Array(elem)) = ctx.fns.expr_types.get(&init.span) {
                if !matches!(**elem, Ty::Unknown) {
                    return c_ty_for(&Ty::Array(elem.clone()));
                }
            }
            match xs.first().map(|x| &x.kind) {
                Some(ExprKind::Lit(l)) if matches!(l.kind, Lit::Str(_)) => {
                    "ailang_arr_str".to_string()
                }
                _ => "ailang_arr_i64".to_string(),
            }
        }
        ExprKind::Map(entries) => {
            // Sema's refined type wins (catches empty `{}` whose K/V was
            // pinned by later `m[k] = v`).
            if let Some(Ty::Map(k, v)) = ctx.fns.expr_types.get(&init.span) {
                if !matches!(**k, Ty::Unknown) || !matches!(**v, Ty::Unknown) {
                    return c_ty_for(&Ty::Map(k.clone(), v.clone()));
                }
            }
            match entries.first().map(|(k, v)| (&k.kind, &v.kind)) {
                Some((ExprKind::Lit(lk), ExprKind::Lit(lv)))
                    if matches!(lk.kind, Lit::Str(_)) && matches!(lv.kind, Lit::Str(_)) =>
                {
                    "ailang_map_ss".to_string()
                }
                Some((ExprKind::Lit(lk), _)) if matches!(lk.kind, Lit::Str(_)) => {
                    "ailang_map_si".to_string()
                }
                _ => "ailang_map_ii".to_string(),
            }
        }
        ExprKind::StructLit { name, .. } => c_safe_name(&name.name),
        ExprKind::Lambda { .. } => {
            // A lambda value is always an `ailang_closure_t` (fat pointer).
            "ailang_closure_t".to_string()
        }
        ExprKind::Binary {
            op: BinOp::Concat, ..
        } => "const char*".to_string(),
        ExprKind::Binary {
            op: BinOp::Add,
            lhs,
            rhs,
        } if is_str_expr(lhs, ctx) || is_str_expr(rhs, ctx) => "const char*".to_string(),
        ExprKind::Binary { op, .. } => match op {
            BinOp::Eq
            | BinOp::Ne
            | BinOp::Lt
            | BinOp::Le
            | BinOp::Gt
            | BinOp::Ge
            | BinOp::And
            | BinOp::Or => "bool".to_string(),
            _ => "int64_t".to_string(),
        },
        ExprKind::Call { callee, args } => {
            if let ExprKind::Ident(name) = &callee.kind {
                // `push` / `pop` / `map` / `filter` / `sort` / `reverse` /
                // `slice` return the same array type as the first arg.
                if matches!(
                    name.as_str(),
                    "push" | "pop" | "map" | "filter" | "sort" | "reverse" | "slice"
                ) && !args.is_empty()
                {
                    // Prefer sema's recorded type; but sema gives `Unknown`
                    // for the polymorphic builtins (`keys`/`values`), so when
                    // we get nothing useful, recurse on the AST shape.
                    if let Some(t) = ctx.fns.expr_types.get(&args[0].span) {
                        if !matches!(t, Ty::Unknown) {
                            return c_ty_for(t);
                        }
                    }
                    return c_ty_for_decl(None, &args[0], ctx);
                }
                // `keys(m)` / `values(m)` derive their element type from
                // the map's (K,V).
                if (name == "keys" || name == "values") && !args.is_empty() {
                    if let Some(Ty::Map(k, v)) = ctx.fns.expr_types.get(&args[0].span) {
                        let elem = if name == "keys" { &**k } else { &**v };
                        return match elem {
                            Ty::Str => "ailang_arr_str".to_string(),
                            _ => "ailang_arr_i64".to_string(),
                        };
                    }
                }
                if name == "reduce" {
                    return "int64_t".to_string();
                }
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
                    return c_ty_for(&sig.return_ty);
                }
            }
            "int64_t".to_string()
        }
        _ => "int64_t".to_string(),
    }
}

pub(crate) fn c_ty_for(t: &Ty) -> String {
    match t {
        Ty::I64 => "int64_t".to_string(),
        Ty::F64 => "double".to_string(),
        Ty::Bool => "bool".to_string(),
        Ty::Str => "const char*".to_string(),
        Ty::Bytes => "ailang_bytes".to_string(),
        Ty::Unit => "void".to_string(),
        Ty::Array(elem) => match **elem {
            Ty::Str => "ailang_arr_str".to_string(),
            _ => "ailang_arr_i64".to_string(),
        },
        Ty::Map(k, v) => match (&**k, &**v) {
            (Ty::Str, Ty::Str) => "ailang_map_ss".to_string(),
            (Ty::Str, _) => "ailang_map_si".to_string(),
            _ => "ailang_map_ii".to_string(),
        },
        Ty::Struct(name) => name.clone(),
        Ty::Result(inner) => match **inner {
            Ty::Str => "ailang_result_str".to_string(),
            Ty::Bool => "ailang_result_bool".to_string(),
            Ty::F64 => "ailang_result_f64".to_string(),
            _ => "ailang_result_i64".to_string(),
        },
        Ty::Unknown => "int64_t".to_string(),
    }
}

// ============================================================================
// C prelude
// ============================================================================

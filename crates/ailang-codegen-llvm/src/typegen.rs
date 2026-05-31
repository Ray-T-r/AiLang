//! Type lowering: AiLang types -> C type spellings, plus generic substitution.
use crate::emit::*;
use crate::names::*;
use ailang_sema::Ty;
use ailang_syntax::ast::*;

pub(crate) fn is_primitive_ty(t: &Ty) -> bool {
    matches!(t, Ty::I64 | Ty::F64 | Ty::Bool | Ty::Str)
}

/// True when a syntactic type-name (a `TypeKind::Path`) is a built-in scalar
/// rather than a user struct/enum. Used to decide whether `[name]` is a
/// primitive-element array (handled by the prelude) or an aggregate-element
/// array (handled by an on-demand `ailang_arr_<name>` template).
pub(crate) fn is_prim_type_name(name: &str) -> bool {
    matches!(
        name,
        "i8" | "i16"
            | "i32"
            | "i64"
            | "u8"
            | "u16"
            | "u32"
            | "u64"
            | "f32"
            | "f64"
            | "bool"
            | "str"
            | "bytes"
    )
}

/// True when an enum variant field must be **boxed** (stored as a heap pointer)
/// to give the C tagged-union a finite size. A by-value aggregate field
/// (`TypeKind::Path` naming a struct/enum) needs boxing when it can
/// transitively reach the enclosing enum through other by-value aggregate
/// fields — i.e. it closes a value-type size cycle. Covers:
///   - direct self-reference: `Add(l:Expr, r:Expr)` in `en Expr` (Expr→Expr),
///   - mutual recursion: `en Expr {…Stmt…}` ↔ `en Stmt {…Expr…}`.
/// A field of array/map type is NOT boxed here — its own heap buffer already
/// breaks the cycle (so `Branch(kids:[Tree])` needs no boxing).
pub(crate) fn is_boxed_enum_field(
    f: &Field,
    enclosing: &str,
    enums: &std::collections::HashMap<String, EnumDecl>,
    structs: &std::collections::HashMap<String, StructDecl>,
) -> bool {
    let TypeKind::Path(n) = &f.ty.kind else {
        return false;
    };
    if !(enums.contains_key(n) || structs.contains_key(n)) {
        return false;
    }
    aggregate_reaches(n, enclosing, enums, structs)
}

/// Can aggregate type `start` reach `target` by following only *by-value*
/// (`TypeKind::Path`) aggregate fields? DFS over the value-type dependency
/// graph. Array/map fields are skipped — they hold elements behind a heap
/// pointer, so they don't contribute to a by-value size cycle.
fn aggregate_reaches(
    start: &str,
    target: &str,
    enums: &std::collections::HashMap<String, EnumDecl>,
    structs: &std::collections::HashMap<String, StructDecl>,
) -> bool {
    let mut seen: std::collections::HashSet<&str> = std::collections::HashSet::new();
    let mut stack = vec![start];
    while let Some(cur) = stack.pop() {
        if !seen.insert(cur) {
            continue;
        }
        let field_tys: Vec<&str> = if let Some(en) = enums.get(cur) {
            en.variants
                .iter()
                .flat_map(|v| v.fields.iter())
                .filter_map(|f| match &f.ty.kind {
                    TypeKind::Path(n) => Some(n.as_str()),
                    _ => None,
                })
                .collect()
        } else if let Some(st) = structs.get(cur) {
            st.fields
                .iter()
                .filter_map(|f| match &f.ty.kind {
                    TypeKind::Path(n) => Some(n.as_str()),
                    _ => None,
                })
                .collect()
        } else {
            Vec::new()
        };
        for ft in field_tys {
            if ft == target {
                return true;
            }
            if (enums.contains_key(ft) || structs.contains_key(ft)) && !seen.contains(ft) {
                stack.push(ft);
            }
        }
    }
    false
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
            Some(ty) => TypeKind::Path(ty_to_path_name(ty)),
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
        TypeKind::Tuple(elems) => {
            TypeKind::Tuple(elems.iter().map(|e| substitute_ty(e, subst)).collect())
        }
    };
    Type { kind, span: t.span }
}

/// Mangling/suffix name for a concrete type argument. Primitives get their
/// short spelling; a user struct/enum (carried as `Ty::Struct`) uses its own
/// name so `id<Point>` and `id<Expr>` mangle distinctly (`id_Point`,
/// `id_Expr`) instead of colliding on `i64`. Also used by `substitute_ty` to
/// turn a resolved type var back into a `TypeKind::Path` spelling.
pub(crate) fn ty_to_path_name(t: &Ty) -> String {
    match t {
        Ty::I64 => "i64".to_string(),
        Ty::F64 => "f64".to_string(),
        Ty::Bool => "bool".to_string(),
        Ty::Str => "str".to_string(),
        Ty::Bytes => "bytes".to_string(),
        Ty::Struct(name) => name.clone(),
        Ty::Tuple(es) => format!("tup_{}", mangle_ty_suffix(es)),
        _ => "i64".to_string(),
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
            // A non-primitive element names a user struct/enum → its
            // monomorphic array type (`[Point]` → `ailang_arr_Point`).
            TypeKind::Path(name) if !is_prim_type_name(name) => {
                format!("ailang_arr_{}", c_safe_name(name))
            }
            _ => "ailang_arr_i64".to_string(),
        },
        TypeKind::Map(k, v) => match (&k.kind, &v.kind) {
            (TypeKind::Path(kn), TypeKind::Path(vn)) if kn == "str" && vn == "str" => {
                "ailang_map_ss".to_string()
            }
            // `{str:Struct}` / `{str:Enum}` → its on-demand symbol-table type.
            (TypeKind::Path(kn), TypeKind::Path(vn)) if kn == "str" && !is_prim_type_name(vn) => {
                format!("ailang_smap_{}", c_safe_name(vn))
            }
            (TypeKind::Path(kn), _) if kn == "str" => "ailang_map_si".to_string(),
            _ => "ailang_map_ii".to_string(),
        },
        TypeKind::Ptr(inner) => format!("{}*", c_ty_from_ast(inner)),
        TypeKind::Result(inner) => match &inner.kind {
            TypeKind::Path(n) if n == "str" => "ailang_result_str".to_string(),
            TypeKind::Path(n) if n == "bool" => "ailang_result_bool".to_string(),
            TypeKind::Path(n) if n == "f64" || n == "f32" => "ailang_result_f64".to_string(),
            // A non-primitive `!Name` names a user struct/enum result.
            TypeKind::Path(n) if !is_prim_type_name(n) => {
                format!("ailang_result_{}", c_safe_name(n))
            }
            _ => "ailang_result_i64".to_string(),
        },
        TypeKind::Fn { .. } => {
            // Every fn-pointer-typed value in AiLang is an
            // `ailang_closure_t` (fat pointer with captured env). Calls
            // go through the trampoline in emit_expr::Call.
            "ailang_closure_t".to_string()
        }
        // `(T1, T2, ...)` → its `tup_<suffix>` struct. Routed through
        // `c_ty_for` so the suffix matches the on-demand typedef emission.
        TypeKind::Tuple(_) => c_ty_for(&ailang_sema::ast_ty_kind_to_ty(t)),
        _ => "int64_t".to_string(),
    }
}

pub(crate) fn c_ty_for_decl(ty: Option<&Type>, init: &Expr, ctx: &EmitCtx) -> String {
    if let Some(t) = ty {
        return c_ty_for(&ailang_sema::ast_ty_kind_to_ty(t));
    }
    // Trust sema's recorded type whenever it's concrete — the syntactic fallback
    // below only understands literal/array/map *shapes*, so a non-literal
    // initializer whose value is a struct, pointer, array, map, str, … (e.g.
    // `ys := s.items` where `items:[str]`) would otherwise wrongly default to
    // `int64_t`. Excluded: an empty aggregate still `Unknown` in its element/KV
    // (the syntactic path + later refinement do better), and a lambda (sema
    // records its *return* type, but the binding is the closure value).
    if !matches!(&init.kind, ExprKind::Lambda { .. }) {
        if let Some(t) = ctx.fns.expr_types.get(&init.span) {
            let concrete = match t {
                Ty::Unknown | Ty::Unit => false,
                Ty::Array(e) => !matches!(**e, Ty::Unknown),
                Ty::Map(k, v) => !matches!(**k, Ty::Unknown) && !matches!(**v, Ty::Unknown),
                _ => true,
            };
            if concrete {
                return c_ty_for(t);
            }
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
                // `unwrap(r)` yields the result's inner T. The builtin's sema
                // return type is Unknown (polymorphic), so derive it from the
                // argument's recorded `Result(T)` type.
                if name == "unwrap" && !args.is_empty() {
                    if let Some(Ty::Result(inner)) = ctx.fns.expr_types.get(&args[0].span) {
                        return c_ty_for(inner);
                    }
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
        Ty::Array(elem) => match &**elem {
            Ty::Str => "ailang_arr_str".to_string(),
            // `[Struct]` / `[Enum]` → its monomorphic array type (template
            // emitted on demand in emit_c). Both structs and enums are Ty::Struct.
            Ty::Struct(n) => format!("ailang_arr_{}", c_safe_name(n)),
            _ => "ailang_arr_i64".to_string(),
        },
        Ty::Map(k, v) => match (&**k, &**v) {
            (Ty::Str, Ty::Str) => "ailang_map_ss".to_string(),
            // `{str:Struct}` / `{str:Enum}` → its on-demand symbol-table type.
            (Ty::Str, Ty::Struct(n)) => format!("ailang_smap_{}", c_safe_name(n)),
            (Ty::Str, _) => "ailang_map_si".to_string(),
            _ => "ailang_map_ii".to_string(),
        },
        Ty::Struct(name) => name.clone(),
        Ty::Result(inner) => match &**inner {
            Ty::Str => "ailang_result_str".to_string(),
            Ty::Bool => "ailang_result_bool".to_string(),
            Ty::F64 => "ailang_result_f64".to_string(),
            // `!Struct` / `!Enum` → its on-demand result type.
            Ty::Struct(n) => format!("ailang_result_{}", c_safe_name(n)),
            _ => "ailang_result_i64".to_string(),
        },
        Ty::Ptr(inner) => format!("{}*", c_ty_for(inner)),
        // `(T1, T2, ...)` → its monomorphic tuple struct (template emitted on
        // demand in emit_c, keyed by the same `mangle_ty_suffix`).
        Ty::Tuple(elems) => format!("tup_{}", mangle_ty_suffix(elems)),
        Ty::Unknown => "int64_t".to_string(),
    }
}

// ============================================================================
// C prelude
// ============================================================================

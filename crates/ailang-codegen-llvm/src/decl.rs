//! Emission of typedefs, signatures, printers and dispatch macros.
use crate::collect::*;
use crate::names::*;
use crate::typegen::*;
use ailang_sema::{ResolvedModule, Ty};
use ailang_syntax::ast::*;
use std::fmt::Write;

/// Emit `#define name(x) _Generic((x), TY: name_TY, …)(x)` for one generic fn.
/// Dispatch is on the *first param*'s substituted C type — not on `T`
/// directly — so `first<T>(arr:[T])` works (dispatches on `ailang_arr_i64`
/// / `ailang_arr_str` instead of trying `int64_t` / `const char*`).
pub(crate) fn emit_generic_dispatch_macro(
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
        let subst: std::collections::HashMap<String, Ty> = f
            .type_params
            .iter()
            .zip(type_args.iter().cloned())
            .map(|(p, t)| (p.name.clone(), t))
            .collect();
        let first_param_c_ty = f
            .params
            .first()
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

pub(crate) fn emit_lambda_signature(out: &mut String, info: &LambdaInfo) {
    // Every lifted lambda takes `void* __env_` as its first parameter so
    // the caller's `ailang_closure_t.env` plugs in uniformly. The return
    // type comes from the body's tail expression (sema records it).
    let _ = write!(
        out,
        "static {} {}(void* __env_",
        info.return_c_ty, info.c_name
    );
    for p in info.params.iter() {
        out.push_str(", ");
        let ty = c_ty_for_param(p.ty.as_ref());
        out.push_str(&c_decl(&c_safe_name(&p.name.name), &ty));
    }
    out.push(')');
}

/// `en Name { V1, V2(f:T), … }` →
/// ```c
/// typedef struct {
///   int __tag;
///   union { struct { } V1; struct { T f; } V2; ... } __data;
/// } Name;
/// ```
pub(crate) fn emit_enum_typedef(out: &mut String, e: &EnumDecl) {
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

pub(crate) fn emit_enum_ctor(out: &mut String, e: &EnumDecl, v: &EnumVariant) {
    let en_name = c_safe_name(&e.name.name);
    let v_name = c_safe_name(&v.name.name);
    let tag = e
        .variants
        .iter()
        .position(|x| x.name.name == v.name.name)
        .unwrap_or(0);
    let _ = write!(out, "static inline {en_name} {en_name}_{v_name}(");
    if v.fields.is_empty() {
        out.push_str("void");
    } else {
        for (i, f) in v.fields.iter().enumerate() {
            if i > 0 {
                out.push_str(", ");
            }
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

pub(crate) fn emit_struct_typedef(out: &mut String, s: &StructDecl) {
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
pub(crate) fn emit_struct_printer(out: &mut String, s: &StructDecl) {
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
                "i8" | "i16" | "i32" | "i64" | "u8" | "u16" | "u32" | "u64" => "ailang_print_i64",
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
pub(crate) fn emit_extended_print_macros(out: &mut String, structs: &[&StructDecl]) {
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

pub(crate) fn emit_fn_signature(out: &mut String, f: &FnDecl, res: &ResolvedModule) {
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
    let mangled = if is_main {
        "main".to_string()
    } else {
        c_safe_name(&f.name.name)
    };
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

pub(crate) fn emit_extern(out: &mut String, e: &ExternDecl) {
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

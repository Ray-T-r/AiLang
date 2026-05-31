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
    // One macro parameter per fn parameter (`__a0, __a1, …`). Dispatch via
    // `_Generic` on the first argument's type, then forward *all* arguments to
    // the selected monomorphic instance — so multi-arg generics like
    // `fst<T>(a:T, b:T)` work, not just single-arg `id<T>(x:T)`. `.max(1)`
    // keeps a `__a0` selector even for the (unusual) zero-param case.
    let arity = f.params.len().max(1);
    let params: Vec<String> = (0..arity).map(|i| format!("__a{i}")).collect();
    let param_list = params.join(", ");
    let _ = write!(out, "\n#define {name}({param_list}) _Generic((__a0)");
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
    let _ = write!(out, ")({param_list})\n");
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
/// Forward-declare an enum as a named struct tag: `typedef struct Name Name;`.
/// Emitted before array templates and bodies so (a) recursive variant payloads
/// can hold `Name*`, and (b) an `ailang_arr_Name` template (for `[Name]` child
/// lists) can be generated before `Name`'s own body references it.
pub(crate) fn emit_enum_fwd(out: &mut String, e: &EnumDecl) {
    let name = c_safe_name(&e.name.name);
    let _ = write!(out, "typedef struct {name} {name};\n");
}

pub(crate) fn emit_enum_typedef(out: &mut String, e: &EnumDecl, res: &ResolvedModule) {
    let name = c_safe_name(&e.name.name);
    // Body only — the `typedef struct Name Name;` forward declaration is
    // emitted separately by `emit_enum_fwd` (see its doc for why the split
    // matters for recursive ADTs and `[Name]` child lists).
    let _ = write!(out, "struct {name} {{\n    int __tag;\n    union {{\n");
    for v in &e.variants {
        let vname = c_safe_name(&v.name.name);
        let _ = write!(out, "        struct {{");
        for f in &v.fields {
            // Box recursive payloads (`Expr l` → `Expr* l`), incl. mutual
            // recursion, so the struct's size is finite. Ctor heap-copies.
            let cty = if is_boxed_enum_field(f, &e.name.name, &res.enum_table, &res.struct_table) {
                format!("{}*", c_ty_from_ast(&f.ty))
            } else {
                c_ty_from_ast(&f.ty)
            };
            let _ = write!(out, " {}; ", c_decl(&c_safe_name(&f.name.name), &cty));
        }
        let _ = write!(out, "}} {};\n", vname);
    }
    let _ = write!(out, "    }} __data;\n}};\n");
}

pub(crate) fn emit_enum_ctor(
    out: &mut String,
    e: &EnumDecl,
    v: &EnumVariant,
    res: &ResolvedModule,
) {
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
        if is_boxed_enum_field(f, &e.name.name, &res.enum_table, &res.struct_table) {
            // Recursive field is stored boxed (`Name*`). The parameter arrives
            // by value; copy it onto the GC heap and store the pointer.
            // GC_MALLOC (not _atomic) — the payload may contain pointers/strings
            // the collector must scan.
            let cty = c_ty_from_ast(&f.ty);
            let _ = write!(
                out,
                "    {cty}* __box_{fn_} = ({cty}*) GC_MALLOC(sizeof({cty}));\n"
            );
            let _ = write!(out, "    *__box_{fn_} = {fn_};\n");
            let _ = write!(out, "    __v.__data.{v_name}.{fn_} = __box_{fn_};\n");
        } else {
            let _ = write!(out, "    __v.__data.{v_name}.{fn_} = {fn_};\n");
        }
    }
    out.push_str("    return __v;\n}\n");
}

/// Forward-declare a struct as a named tag: `typedef struct Name Name;`.
/// Paired with `emit_struct_typedef` (the body), the split lets a struct hold
/// pointer/array references to other aggregates declared anywhere in the module.
pub(crate) fn emit_struct_fwd(out: &mut String, s: &StructDecl) {
    let name = c_safe_name(&s.name.name);
    let _ = write!(out, "typedef struct {name} {name};\n");
}

pub(crate) fn emit_struct_typedef(out: &mut String, s: &StructDecl) {
    let name = c_safe_name(&s.name.name);
    // Body only; `emit_struct_fwd` emits the `typedef struct Name Name;` first.
    let _ = write!(out, "struct {name} {{\n");
    for f in &s.fields {
        let cty = c_ty_from_ast(&f.ty);
        let _ = write!(out, "    {} {};\n", cty, c_safe_name(&f.name.name));
    }
    let _ = write!(out, "}};\n");
}

/// Emit just the array *struct typedef* (`{ len; cap; T* data; }`) for one
/// aggregate element type. It holds `T*`, so it needs only a forward
/// declaration of `T` — and its own size is fixed. Emitting it *before* the
/// aggregate bodies lets a body embed `ailang_arr_T` by value, which is what
/// makes a self-recursive child list (`Branch(kids:[Tree])`) compile.
pub(crate) fn emit_arr_typedef(out: &mut String, suffix: &str, elem: &str) {
    let _ = write!(
        out,
        "typedef struct {{ int64_t len; int64_t cap; {elem}* data; }} ailang_arr_{suffix};\n"
    );
}

/// Emit one `tup_<suffix>` struct for a tuple shape (multi-return). Fields are
/// positional: `_0`, `_1`, … in the given C-type order. Like the array typedef,
/// emitted before fn signatures so a fn can return the tuple by value.
pub(crate) fn emit_tup_typedef(out: &mut String, suffix: &str, field_ctys: &[String]) {
    let _ = write!(out, "typedef struct {{ ");
    for (i, cty) in field_ctys.iter().enumerate() {
        let _ = write!(out, "{cty} _{i}; ");
    }
    let _ = write!(out, "}} tup_{suffix};\n");
}

/// Emit the array *helpers* (make/len/at/push/pop) + the `AILANG_ARR_<suffix>`
/// literal macro for one aggregate element type. These size and copy elements
/// by value (`sizeof(T)`, `data[i] = src[i]`, by-value `at`), so `T` must be
/// **complete** — emit *after* all aggregate bodies. Allocates with `GC_MALLOC`
/// (scanned), since struct/enum elements may embed pointers or strings.
/// Together with [`emit_arr_typedef`] this is the full `ailang_arr_i64` / `_str`
/// equivalent for `[Struct]` / `[Enum]` — enough to construct, index, iterate
/// (`.data`/`.len`), and grow, so token streams and AST child lists can exist.
pub(crate) fn emit_arr_helpers(out: &mut String, suffix: &str, elem: &str) {
    let _ = write!(
        out,
        "static inline ailang_arr_{suffix} ailang_arr_{suffix}_make(int64_t n, const {elem}* src) {{ \
ailang_arr_{suffix} a; a.len=n; a.cap=n; a.data=({elem}*) GC_MALLOC((size_t)(n>0?n:1)*sizeof({elem})); \
for(int64_t i=0;i<n;i++) a.data[i]=src[i]; return a; }}\n\
         static inline int64_t ailang_arr_{suffix}_len(ailang_arr_{suffix} a) {{ return a.len; }}\n\
         static inline {elem} ailang_arr_{suffix}_at(ailang_arr_{suffix} a, int64_t i) {{ return a.data[i]; }}\n\
         static ailang_arr_{suffix} ailang_arr_{suffix}_push(ailang_arr_{suffix} a, {elem} x) {{ \
if(a.cap>a.len){{a.data[a.len]=x;a.len+=1;return a;}} int64_t nc=a.cap>0?a.cap*2:8; \
{elem}* nd=({elem}*) GC_MALLOC((size_t)nc*sizeof({elem})); if(a.len) memcpy(nd,a.data,(size_t)a.len*sizeof({elem})); \
nd[a.len]=x; ailang_arr_{suffix} b; b.len=a.len+1; b.cap=nc; b.data=nd; return b; }}\n\
         static ailang_arr_{suffix} ailang_arr_{suffix}_pop(ailang_arr_{suffix} a) {{ if(a.len>0) a.len-=1; return a; }}\n\
         static ailang_arr_{suffix} ailang_arr_{suffix}_slice(ailang_arr_{suffix} a, int64_t lo, int64_t hi) {{ \
if(lo<0) lo=0; if(hi>a.len) hi=a.len; if(hi<lo) hi=lo; int64_t m=hi-lo; \
ailang_arr_{suffix} b; b.len=m; b.cap=m; b.data=({elem}*) GC_MALLOC((size_t)(m>0?m:1)*sizeof({elem})); \
for(int64_t i=0;i<m;i++) b.data[i]=a.data[lo+i]; return b; }}\n\
         static ailang_arr_{suffix} ailang_arr_{suffix}_reverse(ailang_arr_{suffix} a) {{ \
ailang_arr_{suffix} b; b.len=a.len; b.cap=a.len; b.data=({elem}*) GC_MALLOC((size_t)(a.len>0?a.len:1)*sizeof({elem})); \
for(int64_t i=0;i<a.len;i++) b.data[i]=a.data[a.len-1-i]; return b; }}\n\
         #define AILANG_ARR_{suffix}(n, ...) ailang_arr_{suffix}_make((n), ({elem}[]){{__VA_ARGS__}})\n"
    );
}

/// Emit `ailang_result_<T>` (a `{bool ok; T value; const char* error;}`) plus
/// its ok/err/unwrap/is_ok/is_err/err_msg helpers, for a `!T` whose `T` is a
/// user struct/enum. Mirrors the primitive `ailang_result_i64` set in the
/// prelude. Holds `T` by value, so emit after `T`'s body is complete. `?` needs
/// no per-type support — its codegen is generic over `.ok`/`.value`/`.error`.
pub(crate) fn emit_result_template(out: &mut String, suffix: &str, elem: &str) {
    let _ = write!(
        out,
        "typedef struct {{ bool ok; {elem} value; const char* error; }} ailang_result_{suffix};\n\
         static inline ailang_result_{suffix} ailang_ok_{suffix}({elem} v) {{ ailang_result_{suffix} r; r.ok=true; r.value=v; r.error=\"\"; return r; }}\n\
         static inline ailang_result_{suffix} ailang_err_{suffix}(const char* m) {{ ailang_result_{suffix} r; r.ok=false; r.error=m?m:\"\"; return r; }}\n\
         static inline {elem} ailang_unwrap_{suffix}(ailang_result_{suffix} r) {{ if(!r.ok){{ fprintf(stderr, \"unwrap: %s\\n\", r.error); exit(1); }} return r.value; }}\n\
         static inline bool ailang_is_ok_{suffix}(ailang_result_{suffix} r) {{ return r.ok; }}\n\
         static inline bool ailang_is_err_{suffix}(ailang_result_{suffix} r) {{ return !r.ok; }}\n\
         static inline const char* ailang_err_msg_{suffix}(ailang_result_{suffix} r) {{ return r.error?r.error:\"\"; }}\n"
    );
}

/// Re-emit the `ok` / `unwrap` / `is_ok` / `err_msg` `_Generic` dispatch macros
/// with an extra arm per aggregate result type, so they work for `!Struct` /
/// `!Enum` alongside the primitives. `is_err` stays the prelude's
/// `!is_ok(r)`, so it needs no per-type arm. `ok` dispatches on the *value*
/// type; the rest on the *result* type. (`err` is handled directly in codegen
/// via the enclosing fn's return type, so it needs no macro.)
pub(crate) fn emit_extended_result_macros(out: &mut String, suffixes: &[String]) {
    out.push_str("\n#undef ok\n#undef unwrap\n#undef is_ok\n#undef err_msg\n");
    out.push_str(
        "#define ok(v) _Generic((v), \\\n\
         \tint64_t: ok_i64, \\\n\
         \tint: ok_i64, \\\n\
         \tdouble: ok_f64, \\\n\
         \tbool: ok_bool, \\\n\
         \tchar*: ok_str, \\\n\
         \tconst char*: ok_str",
    );
    for s in suffixes {
        let _ = write!(out, ", \\\n\t{s}: ailang_ok_{s}");
    }
    out.push_str(")(v)\n");

    for (name, prim) in [
        ("unwrap", "ailang_unwrap"),
        ("is_ok", "ailang_is_ok"),
        ("err_msg", "ailang_err_msg"),
    ] {
        let _ = write!(
            out,
            "#define {name}(r) _Generic((r), \\\n\
             \tailang_result_i64: {prim}_i64, \\\n\
             \tailang_result_str: {prim}_str, \\\n\
             \tailang_result_bool: {prim}_bool, \\\n\
             \tailang_result_f64: {prim}_f64"
        );
        for s in suffixes {
            let _ = write!(out, ", \\\n\tailang_result_{s}: {prim}_{s}");
        }
        out.push_str(")(r)\n");
    }
    out.push('\n');
}

/// Emit the *storage struct + pointer typedef* for a string-keyed map whose
/// value type is an aggregate (`{str:Sym}` — a symbol table). The storage holds
/// only pointers (`const char** keys`, `T* values`, `uint8_t* occupied`), so it
/// needs just a forward declaration of `T`; emit it early like the array
/// typedef. `vsuffix`/`velem` are the value type's C name.
pub(crate) fn emit_smap_typedef(out: &mut String, vsuffix: &str, velem: &str) {
    let _ = write!(
        out,
        "typedef struct {{ int64_t cap; int64_t len; const char** keys; {velem}* values; uint8_t* occupied; }} ailang_smap_{vsuffix}_storage;\n\
         typedef ailang_smap_{vsuffix}_storage* ailang_smap_{vsuffix};\n"
    );
}

/// Emit the *helpers* (make/grow/set/get/len/has/keys/values) for a string-keyed
/// aggregate-value map. These store `T` by value (`sizeof(T)`, `values[i] = v`),
/// so `T` must be **complete** — emit *after* all aggregate bodies (and after
/// the `ailang_arr_<T>` helpers, since `values()` returns one). Reuses the
/// prelude's `ailang_hash_str`. A missing-key `get` returns a zero-initialized
/// `T` (mirrors primitive maps returning 0); call `has` first if that matters.
pub(crate) fn emit_smap_helpers(out: &mut String, vsuffix: &str, velem: &str) {
    let _ = write!(
        out,
        "static ailang_smap_{vsuffix} ailang_smap_{vsuffix}_make(int64_t icap) {{ \
int64_t cap=8; while(cap<icap) cap*=2; \
ailang_smap_{vsuffix} m=(ailang_smap_{vsuffix}) GC_MALLOC(sizeof(ailang_smap_{vsuffix}_storage)); \
m->cap=cap; m->len=0; m->keys=(const char**) GC_MALLOC((size_t)cap*sizeof(const char*)); \
m->values=({velem}*) GC_MALLOC((size_t)cap*sizeof({velem})); \
m->occupied=(uint8_t*) GC_malloc_atomic((size_t)cap); memset(m->occupied,0,(size_t)cap); return m; }}\n\
         static void ailang_smap_{vsuffix}_grow(ailang_smap_{vsuffix} m) {{ \
int64_t oc=m->cap; const char** ok=m->keys; {velem}* ov=m->values; uint8_t* oo=m->occupied; \
int64_t nc=oc*2; m->cap=nc; m->keys=(const char**) GC_MALLOC((size_t)nc*sizeof(const char*)); \
m->values=({velem}*) GC_MALLOC((size_t)nc*sizeof({velem})); m->occupied=(uint8_t*) GC_malloc_atomic((size_t)nc); \
memset(m->occupied,0,(size_t)nc); uint64_t mask=(uint64_t)(nc-1); \
for(int64_t i=0;i<oc;i++){{ if(!oo[i]) continue; const char* k=ok[i]; uint64_t h=ailang_hash_str(k)&mask; \
for(int64_t p=0;p<nc;p++){{ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){{ m->keys[j]=k; m->values[j]=ov[i]; m->occupied[j]=1; break; }} }} }} }}\n\
         static void ailang_smap_{vsuffix}_set(ailang_smap_{vsuffix} m, const char* k, {velem} v) {{ \
if(m->len*10>=m->cap*7) ailang_smap_{vsuffix}_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=ailang_hash_str(k)&mask; \
for(int64_t p=0;p<m->cap;p++){{ int64_t i=(int64_t)((h+(uint64_t)p)&mask); \
if(!m->occupied[i]){{ m->keys[i]=k; m->values[i]=v; m->occupied[i]=1; m->len++; return; }} \
if(strcmp(m->keys[i],k)==0){{ m->values[i]=v; return; }} }} }}\n\
         static {velem} ailang_smap_{vsuffix}_get(ailang_smap_{vsuffix} m, const char* k) {{ \
uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=ailang_hash_str(k)&mask; \
for(int64_t p=0;p<m->cap;p++){{ int64_t i=(int64_t)((h+(uint64_t)p)&mask); \
if(!m->occupied[i]) break; if(strcmp(m->keys[i],k)==0) return m->values[i]; }} {velem} __z; memset(&__z,0,sizeof(__z)); return __z; }}\n\
         static int64_t ailang_smap_{vsuffix}_len(ailang_smap_{vsuffix} m) {{ return m->len; }}\n\
         static bool ailang_smap_{vsuffix}_has(ailang_smap_{vsuffix} m, const char* k) {{ \
uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=ailang_hash_str(k)&mask; \
for(int64_t p=0;p<m->cap;p++){{ int64_t i=(int64_t)((h+(uint64_t)p)&mask); \
if(!m->occupied[i]) return false; if(strcmp(m->keys[i],k)==0) return true; }} return false; }}\n\
         static ailang_arr_str ailang_smap_{vsuffix}_keys(ailang_smap_{vsuffix} m) {{ \
ailang_arr_str r; r.len=m->len; r.cap=m->len; r.data=(const char**) GC_MALLOC((size_t)(m->len>0?m->len:1)*sizeof(const char*)); \
int64_t n=0; for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r.data[n++]=m->keys[i]; return r; }}\n\
         static ailang_arr_{vsuffix} ailang_smap_{vsuffix}_values(ailang_smap_{vsuffix} m) {{ \
ailang_arr_{vsuffix} r; r.len=m->len; r.cap=m->len; r.data=({velem}*) GC_MALLOC((size_t)(m->len>0?m->len:1)*sizeof({velem})); \
int64_t n=0; for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r.data[n++]=m->values[i]; return r; }}\n"
    );
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
    let name = &e.sig.name.name;
    // A `cinc`'d header may expose this symbol as a function-like macro — e.g.
    // glibc's <ctype.h> defines `toupper`/`tolower` as macros — which would
    // mangle the `extern` re-declaration below (the preprocessor expands
    // `toupper(int32_t c)` as a macro call, hence "expected identifier" on
    // Linux but not macOS). `#undef` drops any such macro so the declaration,
    // and later calls, bind to the real libc function — exactly what `ex fn`
    // asks for. Harmless no-op when the name isn't a macro.
    let _ = writeln!(out, "#undef {name}");
    let _ = write!(out, "extern {} {}(", ret, name);
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

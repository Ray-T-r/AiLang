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

#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum Ty {
    Unit,
    I64,
    F64,
    Bool,
    Str,
    /// Binary buffer — can contain `\0`. Codegen lowers to `ailang_bytes`
    /// (a `{ int64_t len; const uint8_t* data; }`). Distinct from `Str`
    /// (which is a nul-terminated `const char*`) so protocol/IO code can
    /// hold arbitrary bytes.
    Bytes,
    /// Homogeneous array `[T]`. For M4+ we only generate code for the
    /// `[i64]` and `[str]` instantiations.
    Array(Box<Ty>),
    /// Map `{K:V}`. M4+ ships only `{i64:i64}`; other instantiations are
    /// follow-up work.
    Map(Box<Ty>, Box<Ty>),
    /// User-defined struct, identified by name. Field types live in
    /// `ResolvedModule.struct_table`.
    Struct(String),
    /// `!T` — result-like wrapper. Codegen lowers to `ailang_result_<T>`
    /// (one C struct per primitive T). `?` postfix propagates.
    Result(Box<Ty>),
    /// `*T` — a raw C pointer (FFI only). Produced by `&x` (address-of) and
    /// by `ex fn`s declared to return `*T`. Codegen lowers to `<T>*` and,
    /// crucially, routes `.field` access through `->` instead of `.`.
    Ptr(Box<Ty>),
    /// `(T1, T2, ...)` — a tuple, used for multi-return. Codegen lowers to one
    /// `tup_<suffix>` C struct per distinct shape; produced by a tuple literal
    /// and consumed by a destructuring decl `a, b := f()`.
    Tuple(Vec<Ty>),
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
    pub expr_types: HashMap<Span, Ty>,
    /// User struct decls indexed by name.
    pub struct_table: HashMap<String, StructDecl>,
    /// User enum (ADT) decls indexed by name.
    pub enum_table: HashMap<String, EnumDecl>,
    /// Variant name → enum name. Lets codegen recognize bare `Some(x)` /
    /// `None` as constructor calls instead of plain idents/calls.
    pub variant_to_enum: HashMap<String, String>,
}

pub fn analyze(mut module: Module) -> (ResolvedModule, Vec<Diagnostic>) {
    let mut diags = Vec::new();
    let mut fn_table: HashMap<String, FnSigResolved> = HashMap::new();
    let mut struct_table: HashMap<String, StructDecl> = HashMap::new();
    let mut enum_table: HashMap<String, EnumDecl> = HashMap::new();

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
                let initial_ret = f
                    .return_ty
                    .as_ref()
                    .map(ast_ty_kind_to_ty)
                    .unwrap_or(Ty::Unit);
                let sig = FnSigResolved {
                    name: f.name.name.clone(),
                    params: f
                        .params
                        .iter()
                        .map(|p| (p.name.name.clone(), ast_ty_to_ty(p.ty.as_ref())))
                        .collect(),
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
                    params: e
                        .sig
                        .params
                        .iter()
                        .map(|p| (p.name.name.clone(), ast_ty_to_ty(p.ty.as_ref())))
                        .collect(),
                    variadic: e.sig.variadic,
                    return_ty: e
                        .sig
                        .return_ty
                        .as_ref()
                        .map(ast_ty_kind_to_ty)
                        .unwrap_or(Ty::Unit),
                    is_extern: true,
                    extern_lib: e.lib.as_ref().map(|s| s.value.clone()),
                    span: e.span,
                };
                fn_table.insert(e.sig.name.name.clone(), sig);
            }
            Item::Struct(s) => {
                struct_table.insert(s.name.name.clone(), s.clone());
            }
            Item::Enum(e) => {
                enum_table.insert(e.name.name.clone(), e.clone());
            }
            Item::Import(_) => {
                // Imports are resolved by the driver pre-sema; ignore here.
            }
            Item::CInclude(_) => {
                // C headers only affect codegen (#include) and linking; they
                // introduce no AiLang-visible names, so there's nothing to do.
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
    // Build the variant lookup early so the per-fn env can reference it.
    let mut variant_lookup: HashMap<String, String> = HashMap::new();
    for (en_name, en) in &enum_table {
        for v in &en.variants {
            variant_lookup.insert(v.name.name.clone(), en_name.clone());
        }
    }

    // Pre-pass: rewrite positional struct constructors. `Point(3, 4)` parses
    // as `Call(Ident("Point"), [3, 4])`; if `Point` is a struct (and not
    // shadowed by an enum variant of the same name), we splice the args into
    // the struct's declared field order and replace with `StructLit`. This
    // lets `Point(3,4)` save ~3 tokens vs `Point{x:3, y:4}` while reusing
    // every downstream code path that already handles named struct literals.
    if !struct_table.is_empty() {
        let struct_field_names: HashMap<String, Vec<Ident>> = struct_table
            .iter()
            .map(|(n, s)| (n.clone(), s.fields.iter().map(|f| f.name.clone()).collect()))
            .collect();
        for item in &mut module.items {
            if let Item::Fn(f) = item {
                rewrite_struct_ctors_block(&mut f.body, &struct_field_names, &variant_lookup);
            }
        }
    }

    let mut expr_types: HashMap<Span, Ty> = HashMap::new();
    for item in &module.items {
        if let Item::Fn(f) = item {
            let ret = fn_table.get(&f.name.name).map(|s| s.return_ty.clone());
            let mut env = LocalEnv::new(&struct_table, &variant_lookup, &enum_table, ret);
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

    // De-duplicate diagnostics by (severity, message, span). Some fn bodies are
    // walked twice (the pass-1.5 return-type revisit re-checks bodies), so a
    // body-level warning like non-exhaustive `mt` would otherwise report twice.
    // Order-preserving.
    {
        let mut seen = std::collections::HashSet::new();
        diags.retain(|d| {
            seen.insert((
                d.severity,
                d.message.clone(),
                d.primary.start,
                d.primary.end,
            ))
        });
    }
    (
        ResolvedModule {
            module,
            fn_table,
            expr_types,
            struct_table,
            enum_table,
            variant_to_enum: variant_lookup,
        },
        diags,
    )
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
            params: vec![
                ("m".to_string(), Ty::Unknown),
                ("k".to_string(), Ty::Unknown),
            ],
            variadic: false,
            return_ty: Ty::Bool,
            is_extern: false,
            extern_lib: None,
            span: Span::empty(),
        },
    );

    // I/O + string conversion builtins (resolved to C helpers baked into
    // the prelude). All string results are GC-allocated.
    let io_builtins: &[(&str, &[(&str, Ty)], Ty)] = &[
        ("read_file", &[("path", Ty::Str)], Ty::Str),
        (
            "write_file",
            &[("path", Ty::Str), ("contents", Ty::Str)],
            Ty::Bool,
        ),
        // Binary I/O — `bytes` keeps explicit length and may contain `\0`.
        ("read_file_bytes", &[("path", Ty::Str)], Ty::Bytes),
        (
            "write_file_bytes",
            &[("path", Ty::Str), ("b", Ty::Bytes)],
            Ty::Bool,
        ),
        ("str_to_bytes", &[("s", Ty::Str)], Ty::Bytes),
        ("bytes_to_str", &[("b", Ty::Bytes)], Ty::Str),
        // `bytes_at` is the long-form name for indexing into bytes; `b[i]`
        // also works via the polymorphic `ailang_at`.
        ("bytes_at", &[("b", Ty::Bytes), ("i", Ty::I64)], Ty::I64),
        (
            "bytes_slice",
            &[("b", Ty::Bytes), ("lo", Ty::I64), ("hi", Ty::I64)],
            Ty::Bytes,
        ),
        ("read_line", &[], Ty::Str),
        ("int_to_str", &[("n", Ty::I64)], Ty::Str),
        ("str_to_int", &[("s", Ty::Str)], Ty::I64),
        // Polymorphic stringify used by string interpolation. C11 `_Generic`
        // in PRELUDE picks int_to_str / float_to_str / bool_to_str / identity
        // based on the argument's C type, so sema takes Unknown and trusts
        // codegen.
        ("to_str", &[("v", Ty::Unknown)], Ty::Str),
        ("args", &[], Ty::Array(Box::new(Ty::Str))),
        ("exit", &[("code", Ty::I64)], Ty::Unit),
        // push/pop are polymorphic over array element type. Sema records
        // them as Unknown → codegen reads expr_types on the first arg to
        // decide the C call (ailang_arr_i64_push vs ailang_arr_str_push).
        (
            "push",
            &[("arr", Ty::Unknown), ("x", Ty::Unknown)],
            Ty::Unknown,
        ),
        ("pop", &[("arr", Ty::Unknown)], Ty::Unknown),
        // String operations — byte-oriented (not Unicode-aware).
        // `contains` and `index_of` are polymorphic (str / [i64] / [str]);
        // sema keeps loose Unknown params and codegen dispatches per-arg.
        (
            "contains",
            &[("haystack", Ty::Unknown), ("needle", Ty::Unknown)],
            Ty::Bool,
        ),
        (
            "starts_with",
            &[("s", Ty::Str), ("prefix", Ty::Str)],
            Ty::Bool,
        ),
        (
            "ends_with",
            &[("s", Ty::Str), ("suffix", Ty::Str)],
            Ty::Bool,
        ),
        (
            "index_of",
            &[("haystack", Ty::Unknown), ("needle", Ty::Unknown)],
            Ty::I64,
        ),
        ("to_upper", &[("s", Ty::Str)], Ty::Str),
        ("to_lower", &[("s", Ty::Str)], Ty::Str),
        ("trim", &[("s", Ty::Str)], Ty::Str),
        (
            "substring",
            &[("s", Ty::Str), ("start", Ty::I64), ("end", Ty::I64)],
            Ty::Str,
        ),
        (
            "replace",
            &[("s", Ty::Str), ("old", Ty::Str), ("new", Ty::Str)],
            Ty::Str,
        ),
        (
            "split",
            &[("s", Ty::Str), ("sep", Ty::Str)],
            Ty::Array(Box::new(Ty::Str)),
        ),
        // Float ↔ str conversions.
        ("float_to_str", &[("x", Ty::F64)], Ty::Str),
        ("str_to_float", &[("s", Ty::Str)], Ty::F64),
        ("get_env", &[("name", Ty::Str)], Ty::Str),
        // POSIX regex (extended), libc-backed.
        (
            "regex_match",
            &[("pat", Ty::Str), ("text", Ty::Str)],
            Ty::Bool,
        ),
        (
            "regex_find",
            &[("pat", Ty::Str), ("text", Ty::Str)],
            Ty::Str,
        ),
        // Higher-order: codegen reads first-arg type to dispatch; sema
        // returns Unknown and trusts user code on element shape.
        (
            "map",
            &[("arr", Ty::Unknown), ("f", Ty::Unknown)],
            Ty::Unknown,
        ),
        (
            "filter",
            &[("arr", Ty::Unknown), ("p", Ty::Unknown)],
            Ty::Unknown,
        ),
        (
            "reduce",
            &[
                ("arr", Ty::Unknown),
                ("init", Ty::Unknown),
                ("f", Ty::Unknown),
            ],
            Ty::Unknown,
        ),
        // Map introspection — return arrays of keys/values. Codegen reads
        // expr_types of the map arg to pick the right element shape.
        ("keys", &[("m", Ty::Unknown)], Ty::Unknown),
        ("values", &[("m", Ty::Unknown)], Ty::Unknown),
        // Array helpers — sort/reverse return same array type as arg.
        ("sort", &[("arr", Ty::Unknown)], Ty::Unknown),
        ("reverse", &[("arr", Ty::Unknown)], Ty::Unknown),
        // i64-only abs to complement libc abs (which is i32).
        ("abs_i64", &[("n", Ty::I64)], Ty::I64),
        // Polymorphic `abs(x)` — codegen prelude routes to abs_i64 or abs_f64
        // via _Generic depending on the C type of the argument. Replaces the
        // older `ex fn abs(n:i32) -> i32` extern that std/math.ail used to
        // declare. Sema returns Unknown so int + float arg types both pass.
        ("abs", &[("x", Ty::Unknown)], Ty::Unknown),
        // Time: wall-clock for timestamps; monotonic for durations.
        ("now_ms", &[], Ty::I64),
        ("now_us", &[], Ty::I64),
        ("mono_ms", &[], Ty::I64),
        ("sleep_ms", &[("ms", Ty::I64)], Ty::Unit),
        ("time_iso", &[("ms", Ty::I64)], Ty::Str),
        // TCP sockets (POSIX). All ops return -1 on error (sock_recv: empty bytes).
        (
            "tcp_listen",
            &[("host", Ty::Str), ("port", Ty::I64)],
            Ty::I64,
        ),
        ("tcp_accept", &[("fd", Ty::I64)], Ty::I64),
        (
            "tcp_connect",
            &[("host", Ty::Str), ("port", Ty::I64)],
            Ty::I64,
        ),
        ("sock_send", &[("fd", Ty::I64), ("b", Ty::Bytes)], Ty::I64),
        ("sock_send_str", &[("fd", Ty::I64), ("s", Ty::Str)], Ty::I64),
        ("sock_recv", &[("fd", Ty::I64), ("max", Ty::I64)], Ty::Bytes),
        ("sock_close", &[("fd", Ty::I64)], Ty::I64),
        // Process management (POSIX). Used for fork-per-request servers.
        ("proc_fork", &[], Ty::I64),
        ("proc_getpid", &[], Ty::I64),
        ("proc_no_zombies", &[], Ty::Unit),
        ("proc_reap", &[], Ty::I64),
        // Postgres (libpq). Handles are opaque i64 pointers; callers MUST
        // `pg_close(conn)` and `pg_clear(res)`. Always `pg_escape` user
        // input before concatenating into SQL.
        ("pg_connect", &[("conninfo", Ty::Str)], Ty::I64),
        ("pg_status", &[("conn", Ty::I64)], Ty::I64),
        ("pg_error", &[("conn", Ty::I64)], Ty::Str),
        ("pg_close", &[("conn", Ty::I64)], Ty::Unit),
        ("pg_exec", &[("conn", Ty::I64), ("sql", Ty::Str)], Ty::I64),
        ("pg_ok", &[("res", Ty::I64)], Ty::Bool),
        ("pg_result_error", &[("res", Ty::I64)], Ty::Str),
        ("pg_clear", &[("res", Ty::I64)], Ty::Unit),
        ("pg_nrows", &[("res", Ty::I64)], Ty::I64),
        ("pg_ncols", &[("res", Ty::I64)], Ty::I64),
        (
            "pg_value",
            &[("res", Ty::I64), ("row", Ty::I64), ("col", Ty::I64)],
            Ty::Str,
        ),
        (
            "pg_isnull",
            &[("res", Ty::I64), ("row", Ty::I64), ("col", Ty::I64)],
            Ty::Bool,
        ),
        (
            "pg_col_name",
            &[("res", Ty::I64), ("col", Ty::I64)],
            Ty::Str,
        ),
        ("pg_affected", &[("res", Ty::I64)], Ty::I64),
        ("pg_escape", &[("conn", Ty::I64), ("s", Ty::Str)], Ty::Str),
        // TLS (OpenSSL).  Handles are i64 (`SSL_CTX*` and `SSL*`).
        (
            "tls_server_ctx",
            &[("cert", Ty::Str), ("key", Ty::Str)],
            Ty::I64,
        ),
        ("tls_client_ctx", &[], Ty::I64),
        ("tls_free_ctx", &[("ctx", Ty::I64)], Ty::Unit),
        ("tls_accept", &[("ctx", Ty::I64), ("fd", Ty::I64)], Ty::I64),
        (
            "tls_connect_fd",
            &[("ctx", Ty::I64), ("fd", Ty::I64)],
            Ty::I64,
        ),
        ("tls_send", &[("ssl", Ty::I64), ("b", Ty::Bytes)], Ty::I64),
        ("tls_send_str", &[("ssl", Ty::I64), ("s", Ty::Str)], Ty::I64),
        ("tls_recv", &[("ssl", Ty::I64), ("max", Ty::I64)], Ty::Bytes),
        ("tls_close", &[("ssl", Ty::I64)], Ty::Unit),
        ("tls_error", &[], Ty::Str),
        ("sha1", &[("s", Ty::Str)], Ty::Bytes),
        // !T result-type builtins. `ok` is polymorphic via _Generic;
        // `err_T` are explicit per-T because the return type isn't
        // recoverable from a `str` argument alone.
        ("ok", &[("v", Ty::Unknown)], Ty::Unknown),
        (
            "err_i64",
            &[("msg", Ty::Str)],
            Ty::Result(Box::new(Ty::I64)),
        ),
        (
            "err_str",
            &[("msg", Ty::Str)],
            Ty::Result(Box::new(Ty::Str)),
        ),
        (
            "err_bool",
            &[("msg", Ty::Str)],
            Ty::Result(Box::new(Ty::Bool)),
        ),
        (
            "err_f64",
            &[("msg", Ty::Str)],
            Ty::Result(Box::new(Ty::F64)),
        ),
        // Polymorphic `err(msg)` — sema resolves T from the enclosing fn's
        // `-> !T` return type; codegen routes to err_i64/err_str/err_bool/
        // err_f64 accordingly. Outside a `-> !T` fn this is a sema error.
        ("err", &[("msg", Ty::Str)], Ty::Unknown),
        ("unwrap", &[("r", Ty::Unknown)], Ty::Unknown),
        ("is_ok", &[("r", Ty::Unknown)], Ty::Bool),
        ("is_err", &[("r", Ty::Unknown)], Ty::Bool),
        ("err_msg", &[("r", Ty::Unknown)], Ty::Str),
        // slice/join return same array type / str — codegen infers element.
        (
            "slice",
            &[("arr", Ty::Unknown), ("start", Ty::I64), ("end", Ty::I64)],
            Ty::Unknown,
        ),
        ("join", &[("arr", Ty::Unknown), ("sep", Ty::Str)], Ty::Str),
        // String builders.
        ("repeat", &[("s", Ty::Str), ("n", Ty::I64)], Ty::Str),
        (
            "pad_left",
            &[("s", Ty::Str), ("width", Ty::I64), ("pad", Ty::Str)],
            Ty::Str,
        ),
        (
            "pad_right",
            &[("s", Ty::Str), ("width", Ty::I64), ("pad", Ty::Str)],
            Ty::Str,
        ),
        // Char ↔ int.
        ("chr", &[("i", Ty::I64)], Ty::Str),
        ("ord", &[("s", Ty::Str)], Ty::I64),
        // More numeric helpers.
        ("str_to_bool", &[("s", Ty::Str)], Ty::Bool),
        ("abs_f64", &[("x", Ty::F64)], Ty::F64),
        ("sign", &[("n", Ty::I64)], Ty::I64),
        (
            "clamp",
            &[("n", Ty::I64), ("lo", Ty::I64), ("hi", Ty::I64)],
            Ty::I64,
        ),
    ];

    // `format(fmt, ...)` — variadic printf-style. Sema doesn't check arg
    // count/types; caller is responsible for matching format directives.
    fn_table.insert(
        "format".to_string(),
        FnSigResolved {
            name: "format".to_string(),
            params: vec![("fmt".to_string(), Ty::Str)],
            variadic: true,
            return_ty: Ty::Str,
            is_extern: false,
            extern_lib: None,
            span: Span::empty(),
        },
    );
    for (name, params, ret) in io_builtins {
        fn_table.insert(
            name.to_string(),
            FnSigResolved {
                name: name.to_string(),
                params: params
                    .iter()
                    .map(|(n, t)| (n.to_string(), t.clone()))
                    .collect(),
                variadic: false,
                return_ty: ret.clone(),
                is_extern: false,
                extern_lib: None,
                span: Span::empty(),
            },
        );
    }
}

struct LocalEnv<'a> {
    scopes: Vec<HashMap<String, Ty>>,
    expr_types: HashMap<Span, Ty>,
    /// Every Ident occurrence's span, grouped by name. Used to retroactively
    /// fix up expr_types when a binding's type is refined after-the-fact
    /// (e.g. `mu m := {}` discovers its real `(K,V)` from a later `m[k] = v`).
    ident_refs: HashMap<String, Vec<Span>>,
    /// Per-name span of the binding's initializer expression. Same purpose.
    init_spans: HashMap<String, Span>,
    structs: &'a HashMap<String, StructDecl>,
    /// Variant name → enum name (so sema lets `Some(42)` and `None`
    /// through without "undefined name" errors).
    variants: &'a HashMap<String, String>,
    /// Full enum decls, keyed by enum name. Lets `bind_pattern` resolve a
    /// variant field's declared type (so a match binding like `SIf(c, t, e)`
    /// gives `t` its real `[Stmt]` type instead of `Unknown`).
    enums: &'a HashMap<String, EnumDecl>,
    /// Return type of the function currently being checked. Used so
    /// polymorphic `err(msg)` can resolve to the right `Result(T)` and so
    /// we can diagnose `err(...)` calls outside a `-> !T` fn.
    fn_ret_ty: Option<Ty>,
}

impl<'a> LocalEnv<'a> {
    fn new(
        structs: &'a HashMap<String, StructDecl>,
        variants: &'a HashMap<String, String>,
        enums: &'a HashMap<String, EnumDecl>,
        fn_ret_ty: Option<Ty>,
    ) -> Self {
        Self {
            scopes: vec![HashMap::new()],
            expr_types: HashMap::new(),
            ident_refs: HashMap::new(),
            init_spans: HashMap::new(),
            structs,
            variants,
            enums,
            fn_ret_ty,
        }
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
    fn record_ident_ref(&mut self, name: &str, span: Span) {
        self.ident_refs
            .entry(name.to_string())
            .or_default()
            .push(span);
    }
    /// Update a binding's type and back-propagate to every prior Ident
    /// occurrence + its initializer expression. Used for empty-container
    /// inference (`{}` / `[]`) once usage reveals (K,V) or element type.
    fn refine_binding(&mut self, name: &str, new_ty: Ty) {
        for scope in self.scopes.iter_mut().rev() {
            if scope.contains_key(name) {
                scope.insert(name.to_string(), new_ty.clone());
                break;
            }
        }
        if let Some(init_span) = self.init_spans.get(name).copied() {
            self.expr_types.insert(init_span, new_ty.clone());
        }
        if let Some(spans) = self.ident_refs.get(name).cloned() {
            for span in spans {
                self.expr_types.insert(span, new_ty.clone());
            }
        }
    }
}

// Returns the value type of the block (tail expression, or last explicit
// `rt expr`, or Unit). The tail is type-checked *before* the block's scope
// is popped so that block-local bindings remain visible to it; this matters
// for closures and any other context where a Block is used as an expression.
fn check_block(
    block: &Block,
    fns: &HashMap<String, FnSigResolved>,
    env: &mut LocalEnv,
    diags: &mut Vec<Diagnostic>,
) -> Ty {
    env.push();
    for stmt in &block.stmts {
        check_stmt(stmt, fns, env, diags);
    }
    let ret_ty = if let Some(e) = &block.tail_expr {
        check_expr(e, fns, env, diags)
    } else {
        // No tail expression — if the block ends with `rt e`, e was already
        // type-checked inside check_stmt above; recover its type from the
        // side table so callers (ExprKind::Block, lambda bodies) can use it.
        match block.stmts.last() {
            Some(Stmt::Return { value: Some(e), .. }) => {
                env.expr_types.get(&e.span).cloned().unwrap_or(Ty::Unit)
            }
            _ => Ty::Unit,
        }
    };
    env.pop();
    ret_ty
}

fn check_stmt(
    stmt: &Stmt,
    fns: &HashMap<String, FnSigResolved>,
    env: &mut LocalEnv,
    diags: &mut Vec<Diagnostic>,
) {
    match stmt {
        Stmt::Decl {
            name, ty, value, ..
        } => {
            let inferred = check_expr(value, fns, env, diags);
            let final_ty = ty.as_ref().map(ast_ty_kind_to_ty).unwrap_or(inferred);
            // An explicit annotation pins the type the initializer must have —
            // override the value's recorded type so codegen sees e.g.
            // `[Token]` for `mu toks:[Token] := []` rather than the literal's
            // own loose `[Unknown]` (which would lower to `[i64]` and clash
            // with the declared `ailang_arr_Token`). Only do this for the empty
            // aggregate-literal case; a non-empty literal already self-types.
            if ty.is_some()
                && matches!(
                    &value.kind,
                    ExprKind::Array(xs) if xs.is_empty()
                )
                || ty.is_some()
                    && matches!(
                        &value.kind,
                        ExprKind::Map(es) if es.is_empty()
                    )
            {
                env.record(value.span, final_ty.clone());
            }
            // Remember the initializer's span so later usage can back-propagate
            // a refined type (relevant for empty `{}` / `[]` whose K/V/elem
            // can only be pinned by subsequent index assignments).
            if matches!(&final_ty, Ty::Map(k, v) if **k == Ty::Unknown || **v == Ty::Unknown)
                || matches!(&final_ty, Ty::Array(e) if **e == Ty::Unknown)
            {
                env.init_spans.insert(name.name.clone(), value.span);
            }
            env.insert(name.name.clone(), final_ty);
        }
        Stmt::DestructureDecl {
            names, value, span, ..
        } => {
            // `a, b := f()` — the RHS must be a tuple; bind each name to its
            // corresponding field type. `_` slots (None) are ignored. Permissive:
            // a non-tuple or length mismatch warns and falls back to Unknown.
            let inferred = check_expr(value, fns, env, diags);
            match &inferred {
                Ty::Tuple(fields) => {
                    if fields.len() != names.len() {
                        diags.push(Diagnostic::warning(
                            format!(
                                "destructuring {} bindings from a {}-tuple",
                                names.len(),
                                fields.len()
                            ),
                            *span,
                        ));
                    }
                    for (i, slot) in names.iter().enumerate() {
                        if let Some(id) = slot {
                            let fty = fields.get(i).cloned().unwrap_or(Ty::Unknown);
                            env.insert(id.name.clone(), fty);
                        }
                    }
                }
                _ => {
                    diags.push(Diagnostic::warning(
                        "destructuring a non-tuple value".to_string(),
                        *span,
                    ));
                    for slot in names.iter().flatten() {
                        env.insert(slot.name.clone(), Ty::Unknown);
                    }
                }
            }
        }
        Stmt::Assign { target, value, .. } => {
            check_expr(target, fns, env, diags);
            let value_ty = check_expr(value, fns, env, diags);
            // Refine an empty-`[]` binding from a `push`:
            //   `xs = push(xs, elem)` where `xs` was declared `[]` pins the
            //   element type from `elem`. Without this, `mu xs := []` infers
            //   `[Unknown]` → `[i64]`, and `push(xs, SomeStruct)` clashes.
            if let ExprKind::Ident(tname) = &target.kind {
                if let ExprKind::Call { callee, args } = &value.kind {
                    if let ExprKind::Ident(fname) = &callee.kind {
                        if fname == "push" && args.len() == 2 {
                            if let ExprKind::Ident(aname) = &args[0].kind {
                                if aname == tname {
                                    if let Some(Ty::Array(elem)) = env.lookup(tname).cloned() {
                                        if *elem == Ty::Unknown {
                                            let et = env
                                                .expr_types
                                                .get(&args[1].span)
                                                .cloned()
                                                .unwrap_or(Ty::Unknown);
                                            if et != Ty::Unknown {
                                                env.refine_binding(
                                                    tname,
                                                    Ty::Array(Box::new(et)),
                                                );
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // Refine empty-container bindings from index writes:
            //   `m[k] = v` where `m` was declared as `{}` pins `(K,V)`.
            //   `a[i] = v` where `a` was declared as `[]` pins the element.
            if let ExprKind::Index { container, index } = &target.kind {
                if let ExprKind::Ident(name) = &container.kind {
                    if let Some(existing) = env.lookup(name).cloned() {
                        let index_ty = env
                            .expr_types
                            .get(&index.span)
                            .cloned()
                            .unwrap_or(Ty::Unknown);
                        let refined = match &existing {
                            Ty::Map(k, v) => {
                                let new_k = if **k == Ty::Unknown {
                                    index_ty
                                } else {
                                    (**k).clone()
                                };
                                let new_v = if **v == Ty::Unknown {
                                    value_ty.clone()
                                } else {
                                    (**v).clone()
                                };
                                Some(Ty::Map(Box::new(new_k), Box::new(new_v)))
                            }
                            Ty::Array(elem) if **elem == Ty::Unknown => {
                                Some(Ty::Array(Box::new(value_ty.clone())))
                            }
                            _ => None,
                        };
                        if let Some(new_ty) = refined {
                            if new_ty != existing {
                                env.refine_binding(name, new_ty);
                            }
                        }
                    }
                }
            }
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
        Stmt::If(if_) => {
            check_if(if_, fns, env, diags);
        }
        Stmt::Loop(lp) => check_loop(lp, fns, env, diags),
        Stmt::Match(mt) => {
            check_match(mt, fns, env, diags);
        }
    }
}

/// Type-checks an `if`. Returns the value type when used as an expression
/// (`x := if c { a } el { b }`): the first concrete branch-tail type, so the
/// binding is typed and codegen can pick e.g. `strcmp` for a later `str ==`
/// instead of a raw pointer compare. Statement-position callers ignore it.
fn check_if(
    if_: &IfStmt,
    fns: &HashMap<String, FnSigResolved>,
    env: &mut LocalEnv,
    diags: &mut Vec<Diagnostic>,
) -> Ty {
    check_expr(&if_.cond, fns, env, diags);
    let then_ty = check_block(&if_.then_branch, fns, env, diags);
    let else_ty = match &if_.else_branch {
        Some(ElseBranch::Block(b)) => check_block(b, fns, env, diags),
        Some(ElseBranch::If(inner)) => check_if(inner, fns, env, diags),
        None => Ty::Unit,
    };
    // Prefer a concrete branch type; the arms are assumed homogeneous (sema is
    // permissive — a real then/else mismatch isn't an error here).
    if !matches!(then_ty, Ty::Unknown | Ty::Unit) {
        then_ty
    } else {
        else_ty
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
        Some(LoopHead::ForIn { vars, iter }) => {
            let iter_ty = check_expr(iter, fns, env, diags);
            match (vars.len(), &iter_ty) {
                // `lp (k, v) in m` — destructure map entries.
                (2, Ty::Map(k, v)) => {
                    env.insert(vars[0].name.clone(), (**k).clone());
                    env.insert(vars[1].name.clone(), (**v).clone());
                }
                // `lp x in [a, b, c]` — element binding.
                (1, Ty::Array(elem)) => {
                    env.insert(vars[0].name.clone(), (**elem).clone());
                }
                // Range / unknown / mismatched → fall back to i64.
                _ => {
                    for v in vars {
                        env.insert(v.name.clone(), Ty::I64);
                    }
                }
            }
        }
        Some(LoopHead::While(cond)) => {
            check_expr(cond, fns, env, diags);
        }
        None => {}
    }
    check_block(&lp.body, fns, env, diags);
    env.pop();
}

/// Type-checks a `mt`. Returns the value type when used as an expression: the
/// first concrete arm-body type, so a binding `x := mt … { … }` is typed (same
/// rationale as `check_if` — drives `strcmp` vs pointer `==` downstream).
fn check_match(
    mt: &MatchStmt,
    fns: &HashMap<String, FnSigResolved>,
    env: &mut LocalEnv,
    diags: &mut Vec<Diagnostic>,
) -> Ty {
    let scrut_ty = check_expr(&mt.scrutinee, fns, env, diags);
    let mut result_ty = Ty::Unknown;
    for arm in &mt.arms {
        env.push();
        bind_pattern(&arm.pattern, env);
        let body_ty = check_expr(&arm.body, fns, env, diags);
        env.pop();
        if matches!(result_ty, Ty::Unknown | Ty::Unit) {
            result_ty = body_ty;
        }
    }
    check_exhaustive(mt, &scrut_ty, env, diags);
    result_ty
}

/// Permissive exhaustiveness check: if the scrutinee is an enum and the arms
/// neither cover every variant nor include a catch-all (`_` or a bare binding),
/// emit a *warning* listing the missing variants. Catches the common "added a
/// variant, forgot an arm" bug without making `mt` hard-checked (M2 sema is
/// permissive). The enum's full variant set is reconstructed from `env.variants`
/// (variant-name → enum-name), so no enum-decl table is needed here.
fn check_exhaustive(mt: &MatchStmt, scrut_ty: &Ty, env: &LocalEnv, diags: &mut Vec<Diagnostic>) {
    let Ty::Struct(en_name) = scrut_ty else {
        return;
    };
    let mut all: Vec<&str> = env
        .variants
        .iter()
        .filter(|(_, en)| en.as_str() == en_name)
        .map(|(v, _)| v.as_str())
        .collect();
    if all.is_empty() {
        return; // a struct (or unknown), not an enum — nothing to check.
    }
    all.sort_unstable();
    let has_catch_all = mt
        .arms
        .iter()
        .any(|a| matches!(a.pattern, Pattern::Wildcard(_) | Pattern::Binding(_)));
    if has_catch_all {
        return;
    }
    let covered: std::collections::HashSet<&str> = mt
        .arms
        .iter()
        .filter_map(|a| match &a.pattern {
            Pattern::Variant { name, .. } => Some(name.name.as_str()),
            _ => None,
        })
        .collect();
    let missing: Vec<&str> = all.into_iter().filter(|v| !covered.contains(v)).collect();
    if !missing.is_empty() {
        diags.push(
            Diagnostic::warning(
                format!(
                    "non-exhaustive `mt` on enum `{}`: missing {}",
                    en_name,
                    missing.join(", ")
                ),
                mt.span,
            )
            .with_help("add the missing arms or a `_` catch-all"),
        );
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
        Pattern::Variant { name, bindings, .. } => {
            // Resolve each positional binding's type from the variant's declared
            // fields, so e.g. `SIf(c, t, e)` types `t` as its real `[Stmt]`
            // rather than `Unknown` (which would break `len(t)` dispatch). Fall
            // back to `Unknown` when the variant/enum isn't resolvable.
            let field_tys: Vec<Ty> = env
                .variants
                .get(&name.name)
                .and_then(|en_name| env.enums.get(en_name))
                .and_then(|en| en.variants.iter().find(|v| v.name.name == name.name))
                .map(|v| v.fields.iter().map(|f| ast_ty_kind_to_ty(&f.ty)).collect())
                .unwrap_or_default();
            for (i, b) in bindings.iter().enumerate() {
                if b.name != "_" {
                    let ty = field_tys.get(i).cloned().unwrap_or(Ty::Unknown);
                    env.insert(b.name.clone(), ty);
                }
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
            Lit::Char(_) => Ty::I64, // M2 simplification
            Lit::Bool(_) => Ty::Bool,
            Lit::Nil => Ty::Unknown,
        },
        ExprKind::Ident(name) => {
            // Record this Ident's span so later type refinements can fix it up.
            env.record_ident_ref(name, e.span);
            if let Some(t) = env.lookup(name) {
                t.clone()
            } else if fns.contains_key(name) {
                Ty::Unknown
            } else if let Some(en_name) = env.variants.get(name) {
                // Bare unit variant — e.g. `None`. The value type is the
                // owning enum.
                Ty::Struct(en_name.clone())
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
            // Direct calls `f(...)` look up `f` in the fn table. If `f` is
            // instead a local var (e.g. a lambda bound by `add := fn(a,b) ...`)
            // we fall through to a permissive indirect-call path: no arity
            // check, return type `Unknown` — codegen still emits `f(args)`.
            let fname = if let ExprKind::Ident(n) = &callee.kind {
                Some(n.as_str())
            } else {
                None
            };
            // Variant constructor call: `Some(42)` is not a fn but an
            // enum constructor. Just check the args; the result type is
            // the enclosing enum.
            if let Some(n) = fname {
                if let Some(en_name) = env.variants.get(n) {
                    for a in args {
                        check_expr(a, fns, env, diags);
                    }
                    return Ty::Struct(en_name.clone());
                }
            }
            // Polymorphic `err(msg)` — the T in the resulting `!T` is
            // dictated by the enclosing fn's return type. If we're not in
            // a `-> !T` fn, that's a real error: nothing else can decide T.
            if matches!(fname, Some("err")) {
                for a in args {
                    check_expr(a, fns, env, diags);
                }
                match &env.fn_ret_ty {
                    Some(Ty::Result(_)) => return env.fn_ret_ty.clone().unwrap(),
                    _ => {
                        diags.push(Diagnostic::error(
                            "`err(msg)` is only valid inside a function whose return type is `!T`",
                            callee.span,
                        ).with_help(
                            "either annotate the fn with `-> !T` (e.g. `-> !i64`) or call the type-specific form `err_i64`/`err_str`/`err_bool`/`err_f64`"
                        ));
                        return Ty::Result(Box::new(Ty::I64));
                    }
                }
            }
            let sig = match fname.and_then(|n| fns.get(n)) {
                Some(s) => s.clone(),
                None => {
                    let callee_ty = check_expr(callee, fns, env, diags);
                    if let Some(n) = fname {
                        if env.lookup(n).is_none() && callee_ty == Ty::Unknown {
                            // Struct names show up here when arity didn't match
                            // (matching arity is rewritten to StructLit pre-sema).
                            // Give the user a hint instead of a generic "undefined"
                            // so the miscounted-fields case isn't cryptic.
                            if let Some(sd) = env.structs.get(n) {
                                diags.push(Diagnostic::error(
                                    format!(
                                        "struct `{n}` has {} field(s) but {} argument(s) were given",
                                        sd.fields.len(),
                                        args.len(),
                                    ),
                                    callee.span,
                                ).with_help(format!(
                                    "use `{n}({})` or `{n} {{ {} }}`",
                                    sd.fields.iter().map(|f| f.name.name.as_str()).collect::<Vec<_>>().join(", "),
                                    sd.fields.iter().map(|f| format!("{}: ..", f.name.name)).collect::<Vec<_>>().join(", "),
                                )));
                            } else {
                                diags.push(Diagnostic::error(
                                    format!("call to undefined function `{n}`"),
                                    callee.span,
                                ));
                            }
                        }
                    }
                    for a in args {
                        check_expr(a, fns, env, diags);
                    }
                    return callee_ty;
                }
            };
            // Arg count: variadic allows >=, otherwise ==.
            let ok = if sig.variadic {
                args.len() >= sig.params.len()
            } else {
                args.len() == sig.params.len()
            };
            if !ok {
                let nm = fname.unwrap_or("?");
                diags.push(Diagnostic::error(
                    format!(
                        "wrong number of arguments to `{nm}`: expected {}{}, got {}",
                        sig.params.len(),
                        if sig.variadic { "+" } else { "" },
                        args.len(),
                    ),
                    callee.span,
                ));
            }
            let arg_tys: Vec<Ty> = args
                .iter()
                .map(|a| check_expr(a, fns, env, diags))
                .collect();
            // Polymorphic builtins are registered with `Unknown` return; refine
            // them here from actual arg types so downstream (codegen, loop
            // element-type inference) sees the right `Array(_)` or `Map(_,_)`.
            if let Some(name) = fname {
                match name {
                    // Same-shape array transforms: pass through arg[0]'s type.
                    "sort" | "reverse" | "slice" | "push" | "filter" | "map" => {
                        if let Some(t) = arg_tys.first() {
                            if matches!(t, Ty::Array(_)) {
                                return t.clone();
                            }
                        }
                    }
                    // `pop(arr)` returns the same array (value-semantics).
                    "pop" => {
                        if let Some(t) = arg_tys.first() {
                            if matches!(t, Ty::Array(_)) {
                                return t.clone();
                            }
                        }
                    }
                    // `keys(m)` / `values(m)` derive element type from map.
                    "keys" | "values" => {
                        if let Some(Ty::Map(k, v)) = arg_tys.first() {
                            let elem = if name == "keys" {
                                (**k).clone()
                            } else {
                                (**v).clone()
                            };
                            return Ty::Array(Box::new(elem));
                        }
                    }
                    _ => {}
                }
            }
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
                UnOp::AddrOf => Ty::Ptr(Box::new(t)),
                UnOp::Deref => match t {
                    Ty::Ptr(inner) => *inner,
                    _ => Ty::Unknown,
                },
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
                        for a in args {
                            check_expr(a, fns, env, diags);
                        }
                        return Ty::Unknown;
                    };
                    let sig = match fns.get(fname) {
                        Some(s) => s.clone(),
                        None => {
                            diags.push(Diagnostic::error(
                                format!("call to undefined function `{fname}`"),
                                callee.span,
                            ));
                            for a in args {
                                check_expr(a, fns, env, diags);
                            }
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
                    for a in args {
                        check_expr(a, fns, env, diags);
                    }
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
                Ty::Str => Ty::I64, // indexing a string yields a byte
                _ => Ty::Unknown,
            }
        }
        ExprKind::Field { container, name } => {
            let ct = check_expr(container, fns, env, diags);
            // See through a pointer so `p.field` on a `*Struct` resolves the
            // same field types as `s.field` on a by-value struct. Codegen
            // picks `->` vs `.` from the container's recorded pointer-ness.
            let ct = match ct {
                Ty::Ptr(inner) => *inner,
                other => other,
            };
            if let Ty::Struct(sname) = ct {
                if let Some(sd) = env.structs.get(&sname) {
                    if let Some(f) = sd.fields.iter().find(|f| f.name.name == name.name) {
                        return ast_ty_kind_to_ty(&f.ty);
                    }
                    diags.push(Diagnostic::error(
                        format!("struct `{sname}` has no field `{}`", name.name),
                        name.span,
                    ));
                }
            }
            Ty::Unknown
        }
        ExprKind::If(if_) => check_if(if_, fns, env, diags),
        ExprKind::Match(mt) => check_match(mt, fns, env, diags),
        ExprKind::Block(b) => check_block(b, fns, env, diags),
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
                Some((k, v)) => (
                    check_expr(k, fns, env, diags),
                    check_expr(v, fns, env, diags),
                ),
                // Empty `{}` — `(K,V)` left for back-propagation from
                // later usage (e.g. `m[k] = v`). If nothing refines it,
                // codegen falls back to {i64:i64}.
                None => (Ty::Unknown, Ty::Unknown),
            };
            for (k, v) in entries.iter().skip(1) {
                check_expr(k, fns, env, diags);
                check_expr(v, fns, env, diags);
            }
            Ty::Map(Box::new(kt), Box::new(vt))
        }
        ExprKind::Tuple(xs) => {
            // A tuple literal's type is the tuple of its element types. Used
            // for multi-return (`rt (a, b)`) and destructuring (`q, r := …`).
            let elems = xs
                .iter()
                .map(|x| check_expr(x, fns, env, diags))
                .collect();
            Ty::Tuple(elems)
        }
        ExprKind::Lambda { params, body } => {
            // Walk the body with the lambda's params in scope and return
            // its tail-expression type. This lets `f := fn(s:str) s + "!"`
            // record `f`'s value type as Str, which the call-site dispatch
            // uses to cast the closure_t.fn correctly.
            env.push();
            for p in params {
                env.insert(p.name.name.clone(), ast_ty_to_ty(p.ty.as_ref()));
            }
            let ret_ty = match body {
                LambdaBody::Expr(e) => check_expr(e, fns, env, diags),
                LambdaBody::Block(b) => {
                    let ty = check_block(b, fns, env, diags);
                    // Preserve the prior "value-returning closure defaults to
                    // i64 when sema can't see a value" behaviour, which the
                    // codegen relies on for closure_t.fn cast selection.
                    if matches!(ty, Ty::Unit) {
                        Ty::I64
                    } else {
                        ty
                    }
                }
            };
            env.pop();
            ret_ty
        }
        ExprKind::StructLit { name, fields } => {
            // Sanity-check the struct exists and each field name is real.
            if let Some(sd) = env.structs.get(&name.name) {
                let known: std::collections::HashSet<_> =
                    sd.fields.iter().map(|f| f.name.name.clone()).collect();
                // Declared field types, cloned so the `env.structs` borrow ends
                // before `check_expr` / `env.record` reborrow `env` below.
                let field_tys: std::collections::HashMap<String, Ty> = sd
                    .fields
                    .iter()
                    .map(|f| (f.name.name.clone(), ast_ty_kind_to_ty(&f.ty)))
                    .collect();
                for (fname, fval) in fields {
                    check_expr(fval, fns, env, diags);
                    if !known.contains(&fname.name) {
                        diags.push(Diagnostic::error(
                            format!("struct `{}` has no field `{}`", name.name, fname.name),
                            fname.span,
                        ));
                    }
                    // Pin an empty `{}` / `[]` constructor arg to the field's
                    // declared type. Without this it defaults to `{i64:i64}` /
                    // `[i64]`, so a `{str:str}` field built from `{}` would store
                    // string keys/values in an *atomic* (unscanned) map — Boehm
                    // then reclaims them mid-run, a use-after-free that only bites
                    // once allocation triggers a collection (and `clang -O1+`
                    // drops the stack temporaries that otherwise mask it).
                    let empty_agg = match &fval.kind {
                        ExprKind::Map(es) => es.is_empty(),
                        ExprKind::Array(es) => es.is_empty(),
                        _ => false,
                    };
                    if empty_agg {
                        if let Some(fty) = field_tys.get(&fname.name) {
                            env.record(fval.span, fty.clone());
                        }
                    }
                }
            } else {
                diags.push(Diagnostic::error(
                    format!("undefined struct `{}`", name.name),
                    name.span,
                ));
                for (_, fval) in fields {
                    check_expr(fval, fns, env, diags);
                }
            }
            Ty::Struct(name.name.clone())
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
            // Either side concrete pins the result type. Letting `Unknown +
            // 1` resolve to `i64` is what enables `m[k] += 1` to feed an
            // i64-shaped value back into empty-map (K,V) inference.
            if *lt == Ty::F64 || *rt == Ty::F64 {
                Ty::F64
            } else if *lt == Ty::I64 || *rt == Ty::I64 {
                Ty::I64
            } else {
                Ty::Unknown
            }
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
        // A `mt` with at least one arm whose body is a non-unit expression
        // is treated as a value producer.
        Stmt::Match(m) => m.arms.iter().any(|a| expr_yields_value(&a.body, fn_table)),
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
    // for its type using a param-aware local env. Pass-1.5 runs before
    // structs are needed for inference, so an empty table is safe here.
    let empty_structs: HashMap<String, StructDecl> = HashMap::new();
    let empty_variants: HashMap<String, String> = HashMap::new();
    let empty_enums: HashMap<String, EnumDecl> = HashMap::new();
    let mut env = LocalEnv::new(&empty_structs, &empty_variants, &empty_enums, None);
    for p in &f.params {
        env.insert(p.name.name.clone(), ast_ty_to_ty(p.ty.as_ref()));
    }
    let mut diags = Vec::new();
    if let Some(t) = inferred_tail_ty(&f.body, fn_table, &mut env, &mut diags) {
        if matches!(t, Ty::Unit) {
            Ty::Unit
        } else {
            t
        }
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
    if let Some(t) = &b.tail_expr {
        let ty = check_expr(t, fn_table, env, diags);
        return Some(ty);
    }
    // `rt expr` or a value-yielding `if`/`mt` at the end also tells us.
    if let Some(last) = b.stmts.last() {
        match last {
            Stmt::Return { value: Some(e), .. } => {
                return Some(check_expr(e, fn_table, env, diags));
            }
            Stmt::If(if_) => {
                if let Some(t) = inferred_tail_ty(&if_.then_branch, fn_table, env, diags) {
                    return Some(t);
                }
                if let Some(ElseBranch::Block(b)) = &if_.else_branch {
                    if let Some(t) = inferred_tail_ty(b, fn_table, env, diags) {
                        return Some(t);
                    }
                }
            }
            Stmt::Match(m) => {
                for arm in &m.arms {
                    let t = check_expr(&arm.body, fn_table, env, diags);
                    if !matches!(t, Ty::Unit | Ty::Unknown) {
                        return Some(t);
                    }
                }
            }
            _ => {}
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
                if let Some(sig) = fn_table.get(name) {
                    return !matches!(sig.return_ty, Ty::Unit);
                }
            }
            true
        }
        _ => true,
    }
}

// ============================================================================
// Positional struct constructor rewrite (pre-sema pass)
// ============================================================================
//
// `Point(3, 4)` parses as a generic Call node. If the callee matches a struct
// declaration (and isn't an enum variant of the same name), splice the args
// into the struct's declared field order and rewrite the node as a StructLit.
// The walker recurses into every Expr / Stmt / Block reachable from a fn body
// so nested ctors (`wrap(Point(3,4))`) are also rewritten.

fn rewrite_struct_ctors_block(
    block: &mut Block,
    structs: &HashMap<String, Vec<Ident>>,
    variants: &HashMap<String, String>,
) {
    for stmt in &mut block.stmts {
        rewrite_struct_ctors_stmt(stmt, structs, variants);
    }
    if let Some(tail) = block.tail_expr.as_mut() {
        rewrite_struct_ctors_expr(tail, structs, variants);
    }
}

fn rewrite_struct_ctors_stmt(
    stmt: &mut Stmt,
    structs: &HashMap<String, Vec<Ident>>,
    variants: &HashMap<String, String>,
) {
    match stmt {
        Stmt::Decl { value, .. } => rewrite_struct_ctors_expr(value, structs, variants),
        Stmt::DestructureDecl { value, .. } => {
            rewrite_struct_ctors_expr(value, structs, variants)
        }
        Stmt::Assign { target, value, .. } => {
            rewrite_struct_ctors_expr(target, structs, variants);
            rewrite_struct_ctors_expr(value, structs, variants);
        }
        Stmt::Expr(e) => rewrite_struct_ctors_expr(e, structs, variants),
        Stmt::Return { value: Some(e), .. } => rewrite_struct_ctors_expr(e, structs, variants),
        Stmt::Return { value: None, .. } | Stmt::Break(_) | Stmt::Continue(_) => {}
        Stmt::If(i) => rewrite_struct_ctors_if(i, structs, variants),
        Stmt::Loop(lp) => {
            if let Some(head) = lp.head.as_mut() {
                match head {
                    LoopHead::ForIn { iter, .. } => {
                        rewrite_struct_ctors_expr(iter, structs, variants)
                    }
                    LoopHead::While(c) => rewrite_struct_ctors_expr(c, structs, variants),
                }
            }
            rewrite_struct_ctors_block(&mut lp.body, structs, variants);
        }
        Stmt::Match(m) => {
            rewrite_struct_ctors_expr(&mut m.scrutinee, structs, variants);
            for arm in &mut m.arms {
                rewrite_struct_ctors_expr(&mut arm.body, structs, variants);
            }
        }
    }
}

fn rewrite_struct_ctors_if(
    i: &mut IfStmt,
    structs: &HashMap<String, Vec<Ident>>,
    variants: &HashMap<String, String>,
) {
    rewrite_struct_ctors_expr(&mut i.cond, structs, variants);
    rewrite_struct_ctors_block(&mut i.then_branch, structs, variants);
    match i.else_branch.as_mut() {
        Some(ElseBranch::Block(b)) => rewrite_struct_ctors_block(b, structs, variants),
        Some(ElseBranch::If(inner)) => rewrite_struct_ctors_if(inner, structs, variants),
        None => {}
    }
}

fn rewrite_struct_ctors_expr(
    e: &mut Expr,
    structs: &HashMap<String, Vec<Ident>>,
    variants: &HashMap<String, String>,
) {
    // Walk children first so nested ctors get rewritten regardless of whether
    // the outer node is itself a Call.
    match &mut e.kind {
        ExprKind::Call { callee, args } => {
            rewrite_struct_ctors_expr(callee, structs, variants);
            for a in args.iter_mut() {
                rewrite_struct_ctors_expr(a, structs, variants);
            }
        }
        ExprKind::Index { container, index } => {
            rewrite_struct_ctors_expr(container, structs, variants);
            rewrite_struct_ctors_expr(index, structs, variants);
        }
        ExprKind::Field { container, .. } => {
            rewrite_struct_ctors_expr(container, structs, variants);
        }
        ExprKind::Binary { lhs, rhs, .. } => {
            rewrite_struct_ctors_expr(lhs, structs, variants);
            rewrite_struct_ctors_expr(rhs, structs, variants);
        }
        ExprKind::Unary { operand, .. } => rewrite_struct_ctors_expr(operand, structs, variants),
        ExprKind::Ternary { cond, then_, else_ } => {
            rewrite_struct_ctors_expr(cond, structs, variants);
            rewrite_struct_ctors_expr(then_, structs, variants);
            rewrite_struct_ctors_expr(else_, structs, variants);
        }
        ExprKind::Pipe { lhs, rhs } => {
            rewrite_struct_ctors_expr(lhs, structs, variants);
            rewrite_struct_ctors_expr(rhs, structs, variants);
        }
        ExprKind::Lambda { body, .. } => match body {
            LambdaBody::Expr(inner) => rewrite_struct_ctors_expr(inner, structs, variants),
            LambdaBody::Block(b) => rewrite_struct_ctors_block(b, structs, variants),
        },
        ExprKind::Array(items) => {
            for it in items {
                rewrite_struct_ctors_expr(it, structs, variants);
            }
        }
        ExprKind::Map(entries) => {
            for (k, v) in entries {
                rewrite_struct_ctors_expr(k, structs, variants);
                rewrite_struct_ctors_expr(v, structs, variants);
            }
        }
        ExprKind::Tuple(items) => {
            for it in items {
                rewrite_struct_ctors_expr(it, structs, variants);
            }
        }
        ExprKind::StructLit { fields, .. } => {
            for (_, v) in fields {
                rewrite_struct_ctors_expr(v, structs, variants);
            }
        }
        ExprKind::Block(b) => rewrite_struct_ctors_block(b, structs, variants),
        ExprKind::If(i) => rewrite_struct_ctors_if(i, structs, variants),
        ExprKind::Match(m) => {
            rewrite_struct_ctors_expr(&mut m.scrutinee, structs, variants);
            for arm in &mut m.arms {
                rewrite_struct_ctors_expr(&mut arm.body, structs, variants);
            }
        }
        ExprKind::Try(inner) => rewrite_struct_ctors_expr(inner, structs, variants),
        ExprKind::Lit(_) | ExprKind::Ident(_) | ExprKind::Underscore => {}
    }

    // Now, if THIS node is a Call to a known struct name with matching arity,
    // replace it in-place with a StructLit. Variants win when the name is in
    // both tables (preserves the existing `Some(42)` behavior).
    if let ExprKind::Call { callee, args } = &mut e.kind {
        if let ExprKind::Ident(name) = &callee.kind {
            if !variants.contains_key(name) {
                if let Some(field_names) = structs.get(name) {
                    if field_names.len() == args.len() {
                        let ident = Ident {
                            name: name.clone(),
                            span: callee.span,
                        };
                        let taken_args = std::mem::take(args);
                        let fields: Vec<(Ident, Expr)> = field_names
                            .iter()
                            .cloned()
                            .zip(taken_args.into_iter())
                            .collect();
                        e.kind = ExprKind::StructLit {
                            name: ident,
                            fields,
                        };
                    }
                }
            }
        }
    }
}

pub fn ast_ty_kind_to_ty(t: &Type) -> Ty {
    match &t.kind {
        TypeKind::Path(name) => match name.as_str() {
            "i8" | "i16" | "i32" | "i64" | "u8" | "u16" | "u32" | "u64" => Ty::I64,
            "f32" | "f64" => Ty::F64,
            "bool" => Ty::Bool,
            "str" => Ty::Str,
            "bytes" => Ty::Bytes,
            // Any other path is assumed to name a user struct. Sema's
            // struct_table is the authoritative source; codegen uses the
            // name verbatim as the C type.
            other => Ty::Struct(other.to_string()),
        },
        TypeKind::Array(inner) => Ty::Array(Box::new(ast_ty_kind_to_ty(inner))),
        TypeKind::Map(k, v) => Ty::Map(
            Box::new(ast_ty_kind_to_ty(k)),
            Box::new(ast_ty_kind_to_ty(v)),
        ),
        TypeKind::Result(inner) => Ty::Result(Box::new(ast_ty_kind_to_ty(inner))),
        TypeKind::Ptr(inner) => Ty::Ptr(Box::new(ast_ty_kind_to_ty(inner))),
        TypeKind::Tuple(elems) => Ty::Tuple(elems.iter().map(ast_ty_kind_to_ty).collect()),
        _ => Ty::Unknown,
    }
}

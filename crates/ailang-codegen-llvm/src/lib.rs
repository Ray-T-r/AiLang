//! Code generation.
//!
//! **Current backend: transpile to portable C99/C11 source.** The plan calls
//! for LLVM IR via `inkwell`, but the C transpiler gets the whole pipeline
//! working end-to-end with zero LLVM environment setup, and `clang -O2` on
//! the output produces machine code competitive with anything we'd emit by
//! hand. Switching to LLVM IR is an isolated codegen-only change for a future
//! milestone — the rest of the compiler (lexer/parser/sema/driver) is backend-
//! agnostic.

mod collect;
mod decl;
mod emit;
mod names;
mod typegen;

use crate::collect::*;
use crate::decl::*;
use crate::emit::*;
use crate::names::*;
use crate::typegen::*;
use ailang_sema::ResolvedModule;
use ailang_syntax::ast::*;
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
            if !f.type_params.is_empty() {
                continue;
            }
            emit_fn_signature(&mut out, f, resolved);
            out.push_str(";\n");
        }
    }
    // Forward-declare every generic instance.
    for (g_name, types_set) in &generic_instances {
        if let Some(Item::Fn(f)) = module
            .items
            .iter()
            .find(|i| matches!(i, Item::Fn(f) if f.name.name == *g_name))
        {
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
        if let Some(Item::Fn(f)) = module
            .items
            .iter()
            .find(|i| matches!(i, Item::Fn(f) if f.name.name == *g_name))
        {
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
            if !f.type_params.is_empty() {
                continue;
            }
            emit_fn(&mut out, f, resolved, &ctx);
            out.push('\n');
        }
    }
    // Emit a specialized body per (generic fn, type-tuple).
    for (g_name, types_set) in &generic_instances {
        if let Some(Item::Fn(f)) = module
            .items
            .iter()
            .find(|i| matches!(i, Item::Fn(f) if f.name.name == *g_name))
        {
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

const PRELUDE: &str = include_str!("prelude.c");

//! C identifier/operator escaping.
use ailang_syntax::ast::*;

pub(crate) fn c_binop(op: BinOp) -> &'static str {
    use BinOp::*;
    match op {
        Add => "+",
        Sub => "-",
        Mul => "*",
        Div => "/",
        Mod => "%",
        Eq => "==",
        Ne => "!=",
        Lt => "<",
        Le => "<=",
        Gt => ">",
        Ge => ">=",
        And => "&&",
        Or => "||",
        BitAnd => "&",
        BitOr => "|",
        BitXor => "^",
        Shl => "<<",
        Shr => ">>",
        // The remaining ops produce non-trivial C output; sema warns when
        // they appear in an M2 program.
        Concat => "/* ++ */ +",
        Range | RangeEq => "/* range outside for-in */ ,",
        Coalesce => "/* ?? */ ,",
    }
}

pub(crate) fn c_unop(op: UnOp) -> &'static str {
    match op {
        UnOp::Neg => "-",
        UnOp::Not => "!",
        UnOp::Deref => "*",
        UnOp::AddrOf => "&",
    }
}

/// Return a C-safe spelling of an AiLang identifier.
/// Identifiers that collide with a C keyword, common stdlib type, or our own
/// runtime helpers get an `__ail_` prefix; everything else is returned
/// unchanged so generated C stays readable.
pub(crate) fn c_safe_name(name: &str) -> String {
    if is_c_reserved(name) {
        format!("__ail_{name}")
    } else {
        name.to_string()
    }
}

pub(crate) fn is_c_reserved(name: &str) -> bool {
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
pub(crate) fn c_decl(name: &str, ty: &str) -> String {
    if let Some(idx) = ty.find("(*)") {
        format!("{}(*{}){}", &ty[..idx], name, &ty[idx + 3..])
    } else {
        format!("{} {}", ty, name)
    }
}

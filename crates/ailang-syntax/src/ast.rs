//! Abstract syntax tree.
//!
//! Mirrors the grammar in `spec/grammar.ebnf`. Every node carries a [`Span`]
//! so diagnostics can point back at the source. Heap-allocated children use
//! `Box` (recursive types) or `Vec` (sequences).

use crate::token::Span;
use serde::{Deserialize, Serialize};

// ============================================================================
// Top-level
// ============================================================================

/// A parsed source file.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Module {
    pub items: Vec<Item>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum Item {
    Fn(FnDecl),
    Struct(StructDecl),
    Import(ImportDecl),
    Extern(ExternDecl),
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct FnDecl {
    pub name: Ident,
    /// Type parameters (e.g. `<T, U>`). Empty for monomorphic fns. M6+ keeps
    /// inference loose: occurrences of these names in `params`/`return_ty`
    /// become `Ty::Unknown` in sema, which codegen lowers to `int64_t` —
    /// enough for i64-only generics. Real per-call monomorphization arrives
    /// alongside the LLVM IR backend.
    pub type_params: Vec<Ident>,
    pub params: Vec<Param>,
    pub return_ty: Option<Type>,
    pub body: Block,
    pub span: Span,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct StructDecl {
    pub name: Ident,
    pub fields: Vec<Field>,
    pub span: Span,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Field {
    pub name: Ident,
    pub ty: Type,
    pub span: Span,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ImportDecl {
    pub path: StrLit,
    pub alias: Option<Ident>,
    pub span: Span,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ExternDecl {
    /// `None` ⇒ defaults to libc (per spec §1.5).
    pub lib: Option<StrLit>,
    pub sig: FnSig,
    pub span: Span,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct FnSig {
    pub name: Ident,
    pub params: Vec<Param>,
    /// Trailing `...` (C variadic), only valid inside `extern`.
    pub variadic: bool,
    pub return_ty: Option<Type>,
    pub span: Span,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Param {
    pub name: Ident,
    /// Type annotation. Optional in lambdas; required in fn signatures
    /// (the parser enforces context).
    pub ty: Option<Type>,
    pub span: Span,
}

// ============================================================================
// Statements & control flow
// ============================================================================

/// A `{ ... }` block. If `tail_expr` is `Some`, the block evaluates to it
/// (implicit return rule, spec §1.3).
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Block {
    pub stmts: Vec<Stmt>,
    pub tail_expr: Option<Box<Expr>>,
    pub span: Span,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum Stmt {
    /// `mu? name (: ty)? := expr`
    Decl {
        mutable: bool,
        name: Ident,
        ty: Option<Type>,
        value: Expr,
        span: Span,
    },
    /// `lvalue = expr`
    Assign {
        target: Expr,
        value: Expr,
        span: Span,
    },
    /// Expression evaluated for side effects only.
    Expr(Expr),
    /// `rt expr?`
    Return {
        value: Option<Expr>,
        span: Span,
    },
    Break(Span),
    Continue(Span),
    If(IfStmt),
    Loop(LoopStmt),
    Match(MatchStmt),
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct IfStmt {
    pub cond: Expr,
    pub then_branch: Block,
    pub else_branch: Option<ElseBranch>,
    pub span: Span,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum ElseBranch {
    Block(Block),
    If(Box<IfStmt>),
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct LoopStmt {
    pub head: Option<LoopHead>,
    pub body: Block,
    pub span: Span,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum LoopHead {
    /// `ident in expr`
    ForIn { var: Ident, iter: Expr },
    /// `expr` — while-style.
    While(Expr),
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct MatchStmt {
    pub scrutinee: Expr,
    pub arms: Vec<MatchArm>,
    pub span: Span,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct MatchArm {
    pub pattern: Pattern,
    pub body: Expr,
    pub span: Span,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum Pattern {
    Wildcard(Span),
    Literal(LitExpr),
    Binding(Ident),
    Tuple { elems: Vec<Pattern>, span: Span },
}

// ============================================================================
// Expressions
// ============================================================================

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Expr {
    pub kind: ExprKind,
    pub span: Span,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum ExprKind {
    Lit(LitExpr),
    Ident(String),
    Underscore,
    Call {
        callee: Box<Expr>,
        args: Vec<Expr>,
    },
    Index {
        container: Box<Expr>,
        index: Box<Expr>,
    },
    Field {
        container: Box<Expr>,
        name: Ident,
    },
    Binary {
        op: BinOp,
        lhs: Box<Expr>,
        rhs: Box<Expr>,
    },
    Unary {
        op: UnOp,
        operand: Box<Expr>,
    },
    Ternary {
        cond: Box<Expr>,
        then_: Box<Expr>,
        else_: Box<Expr>,
    },
    Pipe {
        lhs: Box<Expr>,
        rhs: Box<Expr>,
    },
    Lambda {
        params: Vec<Param>,
        body: LambdaBody,
    },
    Array(Vec<Expr>),
    Map(Vec<(Expr, Expr)>),
    Tuple(Vec<Expr>),
    Block(Block),
    If(Box<IfStmt>),
    Match(Box<MatchStmt>),
    /// Postfix `?` for error propagation.
    Try(Box<Expr>),
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum LambdaBody {
    Expr(Box<Expr>),
    Block(Block),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum BinOp {
    Add, Sub, Mul, Div, Mod,
    Concat,
    Eq, Ne, Lt, Le, Gt, Ge,
    And, Or,
    BitAnd, BitOr, BitXor, Shl, Shr,
    Range, RangeEq,
    Coalesce,
}

impl BinOp {
    pub fn symbol(self) -> &'static str {
        use BinOp::*;
        match self {
            Add => "+",  Sub => "-",  Mul => "*",  Div => "/",  Mod => "%",
            Concat => "++",
            Eq => "==",  Ne => "!=",
            Lt => "<",   Le => "<=",  Gt => ">",   Ge => ">=",
            And => "&&", Or => "||",
            BitAnd => "&", BitOr => "|", BitXor => "^",
            Shl => "<<", Shr => ">>",
            Range => "..", RangeEq => "..=",
            Coalesce => "??",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum UnOp {
    Neg,    // -x
    Not,    // !x
    Deref,  // *x   (extern-only contexts)
    AddrOf, // &x   (extern-only contexts)
}

impl UnOp {
    pub fn symbol(self) -> &'static str {
        match self {
            UnOp::Neg => "-",
            UnOp::Not => "!",
            UnOp::Deref => "*",
            UnOp::AddrOf => "&",
        }
    }
}

// ============================================================================
// Literals
// ============================================================================

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct LitExpr {
    pub kind: Lit,
    pub span: Span,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum Lit {
    /// 64-bit signed by default; `i32`/`u8`/... suffix narrows it.
    Int { value: i64, suffix: Option<NumSuffix> },
    Float { value: f64, suffix: Option<FloatSuffix> },
    /// Already-unescaped string contents (no surrounding quotes).
    Str(String),
    Char(char),
    Bool(bool),
    Nil,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum NumSuffix {
    I8, I16, I32, I64,
    U8, U16, U32, U64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum FloatSuffix { F32, F64 }

// ============================================================================
// Types
// ============================================================================

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Type {
    pub kind: TypeKind,
    pub span: Span,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum TypeKind {
    /// Primitive name (`i64`, `str`, `bool`, ...) or user struct name.
    Path(String),
    Array(Box<Type>),                // [T]
    Map(Box<Type>, Box<Type>),       // {K:V}
    Fn { params: Vec<Type>, ret: Option<Box<Type>> },
    Ptr(Box<Type>),                  // *T  (FFI only)
    Optional(Box<Type>),             // ?T
    Result(Box<Type>),               // !T
}

// ============================================================================
// Shared atoms
// ============================================================================

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Ident {
    pub name: String,
    pub span: Span,
}

/// String literal carrying the un-escaped contents.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct StrLit {
    pub value: String,
    pub span: Span,
}

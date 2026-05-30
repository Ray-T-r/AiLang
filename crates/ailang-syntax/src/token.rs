//! Token kinds, spans, and AST nodes for AiLang.
//!
//! This crate is intentionally data-only: no parsing, no lexing, no logic.
//! Anything that needs to inspect or transform syntax depends on this crate
//! for the shape of the data and lives elsewhere.

use serde::{Deserialize, Serialize};

/// Byte-offset span into the source file.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct Span {
    pub start: u32,
    pub end: u32,
}

impl Span {
    pub const fn new(start: u32, end: u32) -> Self {
        Self { start, end }
    }

    pub const fn empty() -> Self {
        Self { start: 0, end: 0 }
    }

    pub fn len(&self) -> u32 {
        self.end - self.start
    }

    pub fn is_empty(&self) -> bool {
        self.start == self.end
    }

    /// Slice the source text covered by this span.
    pub fn slice<'s>(&self, source: &'s str) -> &'s str {
        &source[self.start as usize..self.end as usize]
    }

    pub fn join(self, other: Span) -> Span {
        Span {
            start: self.start.min(other.start),
            end: self.end.max(other.end),
        }
    }
}

impl std::fmt::Debug for Span {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}..{}", self.start, self.end)
    }
}

impl From<std::ops::Range<usize>> for Span {
    fn from(r: std::ops::Range<usize>) -> Self {
        Span::new(r.start as u32, r.end as u32)
    }
}

/// A lexer token: kind + source span. The actual text is recovered via
/// `span.slice(source)` when needed (avoids owning strings in the token stream).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Token {
    pub kind: TokenKind,
    pub span: Span,
}

/// All token kinds AiLang recognizes. Mirrors the keywords and punctuation
/// fixed in `spec/grammar.ebnf`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum TokenKind {
    // ----- Keywords (control flow + declarations) -----
    KwFn,
    KwIf,
    KwEl,
    KwLp,
    KwRt,
    KwMt,
    KwSt,
    KwTr,
    KwIm,
    KwEx,
    KwMu,
    KwBr,
    KwCt,
    KwAs,
    KwIn,
    KwEn,   // `en` — enum / ADT declaration
    KwCinc, // `cinc` — C `#include` directive

    // ----- Keywords (literals) -----
    KwTrue,
    KwFalse,
    KwNil,

    // ----- Primitive type names (reserved keywords) -----
    TyI8,
    TyI16,
    TyI32,
    TyI64,
    TyU8,
    TyU16,
    TyU32,
    TyU64,
    TyF32,
    TyF64,
    TyBool,
    TyStr,

    // ----- Identifiers and literals -----
    Ident,
    /// Integer literal (with optional `_` separators and optional `_i32`/`_u64`/... suffix).
    IntLit,
    /// Float literal (with optional `_` separators and optional `_f32`/`_f64` suffix).
    FloatLit,
    /// String literal, double-quoted. Span includes the quotes.
    StrLit,
    /// Char literal, single-quoted. Span includes the quotes.
    CharLit,
    /// Underscore (used as wildcard pattern; not a regular identifier).
    Underscore,

    // ----- Punctuation -----
    LParen,   // (
    RParen,   // )
    LBrace,   // {
    RBrace,   // }
    LBracket, // [
    RBracket, // ]
    Comma,    // ,
    Semi,     // ;
    Dot,      // .
    At,       // @
    Hash,     // #
    Dollar,   // $

    // ----- Operators -----
    Eq,       // =
    Walrus,   // :=
    Colon,    // :
    Arrow,    // ->
    FatArrow, // =>
    Pipeline, // |>
    Question, // ?
    Coalesce, // ??

    // arithmetic
    Plus,    // +
    Minus,   // -
    Star,    // *
    Slash,   // /
    Percent, // %
    Concat,  // ++

    // compound assignment (desugared to `target = target op rhs` by parser)
    PlusEq,    // +=
    MinusEq,   // -=
    StarEq,    // *=
    SlashEq,   // /=
    PercentEq, // %=

    // comparison
    EqEq, // ==
    Neq,  // !=
    Lt,   // <
    Le,   // <=
    Gt,   // >
    Ge,   // >=

    // logical
    AndAnd, // &&
    OrOr,   // ||
    Bang,   // !

    // bitwise
    Amp,   // &
    Pipe,  // |
    Caret, // ^
    Tilde, // ~
    Shl,   // <<
    Shr,   // >>

    // ranges + variadic
    DotDot,   // ..
    DotDotEq, // ..=
    Ellipsis, // ...  (C-style variadic; used in `ex` declarations)

    // ----- Meta -----
    /// Synthetic end-of-file marker, emitted by the lexer adapter.
    Eof,
    /// Unrecognized input. The parser should report this as a diagnostic.
    Error,
}

impl TokenKind {
    /// Stable human-readable name. Used by diagnostics and snapshot tests.
    pub fn name(self) -> &'static str {
        use TokenKind::*;
        match self {
            KwFn => "fn",
            KwIf => "if",
            KwEl => "el",
            KwLp => "lp",
            KwRt => "rt",
            KwMt => "mt",
            KwSt => "st",
            KwTr => "tr",
            KwIm => "im",
            KwEn => "en",
            KwCinc => "cinc",
            KwEx => "ex",
            KwMu => "mu",
            KwBr => "br",
            KwCt => "ct",
            KwAs => "as",
            KwIn => "in",
            KwTrue => "true",
            KwFalse => "false",
            KwNil => "nil",
            TyI8 => "i8",
            TyI16 => "i16",
            TyI32 => "i32",
            TyI64 => "i64",
            TyU8 => "u8",
            TyU16 => "u16",
            TyU32 => "u32",
            TyU64 => "u64",
            TyF32 => "f32",
            TyF64 => "f64",
            TyBool => "bool",
            TyStr => "str",
            Ident => "ident",
            IntLit => "int",
            FloatLit => "float",
            StrLit => "string",
            CharLit => "char",
            Underscore => "_",
            LParen => "(",
            RParen => ")",
            LBrace => "{",
            RBrace => "}",
            LBracket => "[",
            RBracket => "]",
            Comma => ",",
            Semi => ";",
            Dot => ".",
            At => "@",
            Hash => "#",
            Dollar => "$",
            Eq => "=",
            Walrus => ":=",
            Colon => ":",
            Arrow => "->",
            FatArrow => "=>",
            Pipeline => "|>",
            Question => "?",
            Coalesce => "??",
            Plus => "+",
            Minus => "-",
            Star => "*",
            Slash => "/",
            Percent => "%",
            Concat => "++",
            PlusEq => "+=",
            MinusEq => "-=",
            StarEq => "*=",
            SlashEq => "/=",
            PercentEq => "%=",
            EqEq => "==",
            Neq => "!=",
            Lt => "<",
            Le => "<=",
            Gt => ">",
            Ge => ">=",
            AndAnd => "&&",
            OrOr => "||",
            Bang => "!",
            Amp => "&",
            Pipe => "|",
            Caret => "^",
            Tilde => "~",
            Shl => "<<",
            Shr => ">>",
            DotDot => "..",
            DotDotEq => "..=",
            Ellipsis => "...",
            Eof => "<eof>",
            Error => "<error>",
        }
    }
}

impl std::fmt::Display for TokenKind {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.name())
    }
}

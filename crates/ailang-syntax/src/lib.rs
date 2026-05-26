//! Syntax data for AiLang: tokens, spans, and AST nodes.
//!
//! Pure data — no parsing, lexing, or transformations live here. Two modules:
//!
//! - [`token`] — lexer output: [`Token`], [`TokenKind`], [`Span`].
//! - [`ast`]   — parser output: [`Module`], [`Item`], [`Expr`], etc.

pub mod token;
pub mod ast;

pub use token::{Span, Token, TokenKind};

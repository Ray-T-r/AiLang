//! Literal parsing: decodes the textual slice of int/float/string/char tokens
//! into [`Lit`] values.

use ailang_syntax::ast::*;
use ailang_syntax::token::{Span, TokenKind};

use crate::Parser;

impl Parser<'_> {
    pub(crate) fn parse_literal(&mut self) -> Option<LitExpr> {
        let tok = self.peek();
        match tok.kind {
            TokenKind::IntLit => {
                let raw = tok.span.slice(self.source).to_string();
                self.bump();
                let (digits, suffix) = strip_int_suffix(&raw);
                let value = parse_int(&digits).unwrap_or_else(|e| {
                    self.error(format!("invalid integer literal `{raw}`: {e}"), tok.span);
                    0
                });
                Some(LitExpr {
                    kind: Lit::Int { value, suffix },
                    span: tok.span,
                })
            }
            TokenKind::FloatLit => {
                let raw = tok.span.slice(self.source).to_string();
                self.bump();
                let (digits, suffix) = strip_float_suffix(&raw);
                let value = digits.replace('_', "").parse::<f64>().unwrap_or_else(|_| {
                    self.error(format!("invalid float literal `{raw}`"), tok.span);
                    0.0
                });
                Some(LitExpr {
                    kind: Lit::Float { value, suffix },
                    span: tok.span,
                })
            }
            TokenKind::StrLit => {
                let raw = tok.span.slice(self.source);
                self.bump();
                let unescaped = unescape_str(raw, tok.span, &mut self.errors);
                Some(LitExpr {
                    kind: Lit::Str(unescaped),
                    span: tok.span,
                })
            }
            TokenKind::CharLit => {
                let raw = tok.span.slice(self.source);
                self.bump();
                let ch = unescape_char(raw, tok.span, &mut self.errors);
                Some(LitExpr {
                    kind: Lit::Char(ch),
                    span: tok.span,
                })
            }
            TokenKind::KwTrue => {
                let span = self.bump().span;
                Some(LitExpr {
                    kind: Lit::Bool(true),
                    span,
                })
            }
            TokenKind::KwFalse => {
                let span = self.bump().span;
                Some(LitExpr {
                    kind: Lit::Bool(false),
                    span,
                })
            }
            TokenKind::KwNil => {
                let span = self.bump().span;
                Some(LitExpr {
                    kind: Lit::Nil,
                    span,
                })
            }
            _ => {
                self.error(format!("expected literal, got `{}`", tok.kind), tok.span);
                None
            }
        }
    }

    /// Parse a bare string literal (used for import paths and extern lib names).
    pub(crate) fn parse_str_literal(&mut self) -> Option<StrLit> {
        let tok = self.peek();
        if tok.kind != TokenKind::StrLit {
            self.error(format!("expected string literal, got `{}`", tok.kind), tok.span);
            return None;
        }
        self.bump();
        let raw = tok.span.slice(self.source);
        Some(StrLit {
            value: unescape_str(raw, tok.span, &mut self.errors),
            span: tok.span,
        })
    }
}

// ============================================================================
// helpers
// ============================================================================

fn strip_int_suffix(raw: &str) -> (String, Option<NumSuffix>) {
    for (suf, kind) in [
        ("_i8", NumSuffix::I8),
        ("_i16", NumSuffix::I16),
        ("_i32", NumSuffix::I32),
        ("_i64", NumSuffix::I64),
        ("_u8", NumSuffix::U8),
        ("_u16", NumSuffix::U16),
        ("_u32", NumSuffix::U32),
        ("_u64", NumSuffix::U64),
    ] {
        if let Some(prefix) = raw.strip_suffix(suf) {
            return (prefix.to_string(), Some(kind));
        }
    }
    (raw.to_string(), None)
}

fn strip_float_suffix(raw: &str) -> (String, Option<FloatSuffix>) {
    for (suf, kind) in [("_f32", FloatSuffix::F32), ("_f64", FloatSuffix::F64)] {
        if let Some(prefix) = raw.strip_suffix(suf) {
            return (prefix.to_string(), Some(kind));
        }
    }
    (raw.to_string(), None)
}

fn parse_int(digits: &str) -> Result<i64, std::num::ParseIntError> {
    let clean: String = digits.chars().filter(|c| *c != '_').collect();
    clean.parse::<i64>()
}

/// Strip surrounding `"`s and decode escapes.
fn unescape_str(raw: &str, span: Span, errors: &mut Vec<crate::ParseError>) -> String {
    let inner = raw.strip_prefix('"').and_then(|s| s.strip_suffix('"')).unwrap_or(raw);
    decode_escapes(inner, span, errors)
}

/// Decode a `'x'` or `'\n'` literal into a single `char`.
fn unescape_char(raw: &str, span: Span, errors: &mut Vec<crate::ParseError>) -> char {
    let inner = raw.strip_prefix('\'').and_then(|s| s.strip_suffix('\'')).unwrap_or(raw);
    let decoded = decode_escapes(inner, span, errors);
    let mut iter = decoded.chars();
    let ch = iter.next().unwrap_or('\0');
    if iter.next().is_some() {
        errors.push(crate::ParseError::new(
            "char literal must contain exactly one character",
            span,
        ));
    }
    ch
}

fn decode_escapes(s: &str, span: Span, errors: &mut Vec<crate::ParseError>) -> String {
    let mut out = String::with_capacity(s.len());
    let mut chars = s.chars();
    while let Some(c) = chars.next() {
        if c != '\\' {
            out.push(c);
            continue;
        }
        match chars.next() {
            Some('n') => out.push('\n'),
            Some('t') => out.push('\t'),
            Some('r') => out.push('\r'),
            Some('0') => out.push('\0'),
            Some('\\') => out.push('\\'),
            Some('\'') => out.push('\''),
            Some('"') => out.push('"'),
            Some(other) => {
                errors.push(crate::ParseError::new(
                    format!("unknown escape `\\{other}`"),
                    span,
                ));
                out.push(other);
            }
            None => {
                errors.push(crate::ParseError::new(
                    "trailing backslash in literal",
                    span,
                ));
            }
        }
    }
    out
}

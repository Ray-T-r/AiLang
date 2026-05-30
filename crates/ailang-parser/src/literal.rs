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
            self.error(
                format!("expected string literal, got `{}`", tok.kind),
                tok.span,
            );
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
    let inner = raw
        .strip_prefix('"')
        .and_then(|s| s.strip_suffix('"'))
        .unwrap_or(raw);
    decode_escapes(inner, span, errors)
}

/// Decode a `'x'` or `'\n'` literal into a single `char`.
fn unescape_char(raw: &str, span: Span, errors: &mut Vec<crate::ParseError>) -> char {
    let inner = raw
        .strip_prefix('\'')
        .and_then(|s| s.strip_suffix('\''))
        .unwrap_or(raw);
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

/// Desugar `${expr}` interpolations inside a string literal into a `+`-concat
/// chain of literal pieces and `to_str(expr)` calls. Returns `None` if the
/// string contains no interpolation, in which case the caller falls back to
/// emitting a plain `Lit::Str` expression.
///
/// The text inside `${...}` is re-parsed as a full expression, so field
/// access (`${p.x}`), calls (`${f(a)}`), indexing (`${m[k]}`) and arithmetic
/// (`${a + b}`) all work — not just bare idents. Anything that doesn't parse
/// cleanly into a single expression (e.g. `${not an ident here}`) is kept
/// verbatim, which is also what leaves JSON-ish brace runs untouched.
///
/// Brace matching is naive: the first `}` closes the interpolation, so a `}`
/// *inside* the expression (a map/block literal) would truncate it. Those are
/// vanishingly rare inside interpolations — store them in a local first.
///
/// Escape rule: a `$` not immediately followed by `{` is always literal
/// (Kotlin/Scala style). This avoids breaking JSON-shaped string literals
/// that contain `$` or stray `{`.
pub(crate) fn desugar_interp(s: &str, span: Span) -> Option<Expr> {
    if !s.contains("${") {
        return None;
    }

    enum Part {
        Lit(String),
        Expr(Expr),
    }
    let mut parts: Vec<Part> = Vec::new();
    let mut buf = String::new();
    let bytes = s.as_bytes();
    let n = bytes.len();
    let mut i = 0;

    while i < n {
        if bytes[i] == b'$' && i + 1 < n && bytes[i + 1] == b'{' {
            let start = i + 2;
            let end = (start..n).find(|&j| bytes[j] == b'}');
            let Some(end) = end else {
                buf.push('$');
                i += 1;
                continue;
            };
            let inner = s[start..end].trim();
            match parse_embedded_expr(inner) {
                Some(mut expr) => {
                    if !buf.is_empty() {
                        parts.push(Part::Lit(std::mem::take(&mut buf)));
                    }
                    // Re-parsing `inner` standalone gave its AST nodes spans
                    // relative to `inner` (starting at 0), so the SAME embedded
                    // text in two places would collide on identical spans — and
                    // sema keys `expr_types` by span, so the second occurrence's
                    // types would clobber the first's. Rebase every span into
                    // this string literal's byte range so each interpolation
                    // site is globally unique. `start` is the byte offset of
                    // `inner` within `s`; `span.start` is `s`'s offset in the
                    // source (plus 1 for the opening quote the literal span
                    // includes).
                    let base = span.start + 1 + start as u32;
                    shift_spans(&mut expr, base);
                    parts.push(Part::Expr(expr));
                }
                // Doesn't parse as one expression — keep `${...}` verbatim
                // (e.g. `${not an ident here}`, or a stray JSON-ish brace run).
                None => buf.push_str(&s[i..=end]),
            }
            i = end + 1;
        } else {
            let ch_end = next_char_boundary(bytes, i);
            buf.push_str(&s[i..ch_end]);
            i = ch_end;
        }
    }

    if parts.is_empty() {
        return None;
    }
    if !buf.is_empty() {
        parts.push(Part::Lit(buf));
    }

    let lit_expr = |content: String| Expr {
        span,
        kind: ExprKind::Lit(LitExpr {
            kind: Lit::Str(content),
            span,
        }),
    };
    let to_str_call = |inner: Expr| Expr {
        span,
        kind: ExprKind::Call {
            callee: Box::new(Expr {
                span,
                kind: ExprKind::Ident("to_str".into()),
            }),
            args: vec![inner],
        },
    };
    let mut iter = parts.into_iter().map(|p| match p {
        Part::Lit(s) => lit_expr(s),
        Part::Expr(e) => to_str_call(e),
    });
    let first = iter.next()?;
    Some(iter.fold(first, |lhs, rhs| Expr {
        span,
        kind: ExprKind::Binary {
            op: BinOp::Add,
            lhs: Box::new(lhs),
            rhs: Box::new(rhs),
        },
    }))
}

fn next_char_boundary(bytes: &[u8], i: usize) -> usize {
    let mut j = i + 1;
    while j < bytes.len() && (bytes[j] & 0xC0) == 0x80 {
        j += 1;
    }
    j
}

/// Add `base` to every span in `e` (and its children). An embedded
/// interpolation expression is parsed from a tiny standalone slice, so its
/// spans start at 0; rebasing them into the enclosing string literal's byte
/// range keeps every interpolation site's spans globally unique (sema keys
/// side tables by span, so collisions cause cross-site type contamination).
fn shift_spans(e: &mut Expr, base: u32) {
    fn shift(sp: &mut Span, base: u32) {
        sp.start = sp.start.saturating_add(base);
        sp.end = sp.end.saturating_add(base);
    }
    shift(&mut e.span, base);
    match &mut e.kind {
        ExprKind::Lit(l) => shift(&mut l.span, base),
        ExprKind::Ident(_) | ExprKind::Underscore => {}
        ExprKind::Call { callee, args } => {
            shift_spans(callee, base);
            for a in args {
                shift_spans(a, base);
            }
        }
        ExprKind::Index { container, index } => {
            shift_spans(container, base);
            shift_spans(index, base);
        }
        ExprKind::Field { container, name } => {
            shift_spans(container, base);
            shift(&mut name.span, base);
        }
        ExprKind::Binary { lhs, rhs, .. } => {
            shift_spans(lhs, base);
            shift_spans(rhs, base);
        }
        ExprKind::Unary { operand, .. } => shift_spans(operand, base),
        ExprKind::Ternary { cond, then_, else_ } => {
            shift_spans(cond, base);
            shift_spans(then_, base);
            shift_spans(else_, base);
        }
        ExprKind::Pipe { lhs, rhs } => {
            shift_spans(lhs, base);
            shift_spans(rhs, base);
        }
        // Other expression forms can't appear inside a `${...}` (the embedded
        // parser only accepts a single expression and rejects blocks, lambdas,
        // etc. that would carry statements); nothing to rebase.
        _ => {}
    }
}

/// Re-parse the text inside a `${...}` as a standalone expression. Returns
/// `None` when it doesn't consume the whole slice cleanly into one expression
/// (so the caller keeps the braces verbatim — e.g. `${not an ident here}`).
fn parse_embedded_expr(src: &str) -> Option<Expr> {
    if src.is_empty() {
        return None;
    }
    let tokens = ailang_lexer::lex(src);
    let mut p = Parser {
        tokens: &tokens,
        source: src,
        cursor: 0,
        errors: Vec::new(),
    };
    let expr = p.parse_expr()?;
    if !p.at(TokenKind::Eof) || !p.errors.is_empty() {
        return None;
    }
    Some(expr)
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

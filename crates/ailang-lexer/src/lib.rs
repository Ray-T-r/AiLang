//! AiLang lexer — source text → `Vec<ailang_syntax::Token>`.
//!
//! Driven by `logos`. Each `LogosTok` variant maps 1:1 to a
//! `TokenKind` in `ailang-syntax`. The lexer is whitespace- and
//! comment-stripping; the parser sees only meaningful tokens
//! plus a synthetic trailing `Eof`.

use ailang_syntax::{Span, Token, TokenKind};
use logos::Logos;

#[derive(Logos, Clone, Copy, Debug, PartialEq)]
enum LogosTok {
    // ----- Trivia (callback skip; these variants never reach the parser) -----
    #[regex(r"[ \t\r\n\f]+", logos::skip)]
    Whitespace,

    #[regex(r"//[^\n]*", logos::skip)]
    LineComment,

    // Non-nested block comments. Classic GNU C lexer pattern:
    //   /*  [^*]*    \*+        ([^/*]  [^*]*    \*+)*  /
    // i.e. content of non-stars, then one-or-more stars, optionally followed
    // by a non-slash/non-star char that resets the cycle, finally `/`.
    // The earlier `([^*]|\*+[^*/])*\*+/` form mis-handled `* /` at the end
    // and bailed before consuming the closing slash.
    #[regex(r"/\*[^*]*\*+([^/*][^*]*\*+)*/", logos::skip)]
    BlockComment,

    // ----- Keywords -----
    #[token("fn")]
    Fn,
    #[token("if")]
    If,
    #[token("el")]
    El,
    #[token("lp")]
    Lp,
    #[token("rt")]
    Rt,
    #[token("mt")]
    Mt,
    #[token("st")]
    St,
    #[token("tr")]
    Tr,
    #[token("im")]
    Im,
    #[token("ex")]
    Ex,
    #[token("mu")]
    Mu,
    #[token("br")]
    Br,
    #[token("ct")]
    Ct,
    #[token("as")]
    As,
    #[token("in")]
    In,
    #[token("en")]
    En,
    #[token("cinc")]
    Cinc,
    #[token("true")]
    True,
    #[token("false")]
    False,
    #[token("nil")]
    Nil,

    // ----- Primitive type keywords -----
    #[token("i8")]
    I8,
    #[token("i16")]
    I16,
    #[token("i32")]
    I32,
    #[token("i64")]
    I64,
    #[token("u8")]
    U8,
    #[token("u16")]
    U16,
    #[token("u32")]
    U32,
    #[token("u64")]
    U64,
    #[token("f32")]
    F32,
    #[token("f64")]
    F64,
    #[token("bool")]
    Bool,
    #[token("str")]
    Str,

    // ----- Identifiers (and the wildcard `_`) -----
    #[token("_")]
    Underscore,

    #[regex(r"[a-zA-Z][a-zA-Z0-9_]*")]
    #[regex(r"_[a-zA-Z0-9_]+")] // `_foo` is an ident; bare `_` is matched above
    Ident,

    // ----- Numeric literals -----
    // Plain integer with optional type suffix. Underscore separators
    // (`1_000`) are deferred to post-M0 once we've found a regex pattern
    // logos's engine handles correctly; the original `(_?[0-9])*` form
    // mis-extended the match into the trailing `.` of `1..5`.
    #[regex(r"[0-9]+(_[iu](8|16|32|64))?")]
    IntLit,

    // Float: strictly `D.D` so `1..5` stays as `1`, `..`, `5`.
    #[regex(r"[0-9]+\.[0-9]+(_f(32|64))?")]
    FloatLit,

    // ----- String / char literals (raw spans; un-escaping is the parser's job) -----
    #[regex(r#""([^"\\]|\\.)*""#)]
    StrLit,

    #[regex(r"'([^'\\]|\\.)*'")]
    CharLit,

    // ----- Multi-character operators -----
    // Explicit priorities guarantee longest match wins over `.` / `+` / etc.
    // logos's default priority can let a shorter `Dot` token win when the
    // surrounding regex (e.g. the numeric literal) shares a starting state.
    #[token("...", priority = 40)]
    Ellipsis,
    #[token("..=", priority = 30)]
    DotDotEq,
    #[token("..", priority = 20)]
    DotDot,
    #[token("++", priority = 20)]
    Concat,
    #[token("&&")]
    AndAnd,
    #[token("||")]
    OrOr,
    #[token("==")]
    EqEq,
    #[token("!=")]
    Neq,
    #[token("<=")]
    Le,
    #[token(">=")]
    Ge,
    #[token("<<")]
    Shl,
    #[token(">>")]
    Shr,
    #[token(":=")]
    Walrus,
    #[token("->")]
    Arrow,
    #[token("=>")]
    FatArrow,
    #[token("|>")]
    Pipeline,
    #[token("??")]
    Coalesce,
    // Compound assignment — longer-match wins over the single-char operator.
    #[token("+=")]
    PlusEq,
    #[token("-=")]
    MinusEq,
    #[token("*=")]
    StarEq,
    #[token("/=")]
    SlashEq,
    #[token("%=")]
    PercentEq,

    // ----- Punctuation -----
    #[token("(")]
    LParen,
    #[token(")")]
    RParen,
    #[token("{")]
    LBrace,
    #[token("}")]
    RBrace,
    #[token("[")]
    LBracket,
    #[token("]")]
    RBracket,
    #[token(",")]
    Comma,
    #[token(";")]
    Semi,
    #[token(".")]
    Dot,
    #[token("@")]
    At,
    #[token("#")]
    Hash,
    #[token("$")]
    Dollar,

    // ----- Single-char operators -----
    #[token("=")]
    Eq,
    #[token("+")]
    Plus,
    #[token("-")]
    Minus,
    #[token("*")]
    Star,
    #[token("/")]
    Slash,
    #[token("%")]
    Percent,
    #[token("<")]
    Lt,
    #[token(">")]
    Gt,
    #[token("!")]
    Bang,
    #[token("&")]
    Amp,
    #[token("|")]
    Pipe,
    #[token("^")]
    Caret,
    #[token("~")]
    Tilde,
    #[token(":")]
    Colon,
    #[token("?")]
    Question,
}

fn map(t: LogosTok) -> TokenKind {
    use LogosTok as L;
    use TokenKind as K;
    match t {
        // Trivia variants use `logos::skip` and are filtered before reaching here;
        // including them keeps the match exhaustive without a wildcard arm.
        L::Whitespace | L::LineComment | L::BlockComment => unreachable!("skip variant leaked"),

        L::Fn => K::KwFn,
        L::If => K::KwIf,
        L::El => K::KwEl,
        L::Lp => K::KwLp,
        L::Rt => K::KwRt,
        L::Mt => K::KwMt,
        L::St => K::KwSt,
        L::Tr => K::KwTr,
        L::Im => K::KwIm,
        L::En => K::KwEn,
        L::Cinc => K::KwCinc,
        L::Ex => K::KwEx,
        L::Mu => K::KwMu,
        L::Br => K::KwBr,
        L::Ct => K::KwCt,
        L::As => K::KwAs,
        L::In => K::KwIn,
        L::True => K::KwTrue,
        L::False => K::KwFalse,
        L::Nil => K::KwNil,

        L::I8 => K::TyI8,
        L::I16 => K::TyI16,
        L::I32 => K::TyI32,
        L::I64 => K::TyI64,
        L::U8 => K::TyU8,
        L::U16 => K::TyU16,
        L::U32 => K::TyU32,
        L::U64 => K::TyU64,
        L::F32 => K::TyF32,
        L::F64 => K::TyF64,
        L::Bool => K::TyBool,
        L::Str => K::TyStr,

        L::Underscore => K::Underscore,
        L::Ident => K::Ident,
        L::IntLit => K::IntLit,
        L::FloatLit => K::FloatLit,
        L::StrLit => K::StrLit,
        L::CharLit => K::CharLit,

        L::Ellipsis => K::Ellipsis,
        L::DotDotEq => K::DotDotEq,
        L::DotDot => K::DotDot,
        L::Concat => K::Concat,
        L::AndAnd => K::AndAnd,
        L::OrOr => K::OrOr,
        L::EqEq => K::EqEq,
        L::Neq => K::Neq,
        L::Le => K::Le,
        L::Ge => K::Ge,
        L::Shl => K::Shl,
        L::Shr => K::Shr,
        L::Walrus => K::Walrus,
        L::Arrow => K::Arrow,
        L::FatArrow => K::FatArrow,
        L::Pipeline => K::Pipeline,
        L::Coalesce => K::Coalesce,
        L::PlusEq => K::PlusEq,
        L::MinusEq => K::MinusEq,
        L::StarEq => K::StarEq,
        L::SlashEq => K::SlashEq,
        L::PercentEq => K::PercentEq,

        L::LParen => K::LParen,
        L::RParen => K::RParen,
        L::LBrace => K::LBrace,
        L::RBrace => K::RBrace,
        L::LBracket => K::LBracket,
        L::RBracket => K::RBracket,
        L::Comma => K::Comma,
        L::Semi => K::Semi,
        L::Dot => K::Dot,
        L::At => K::At,
        L::Hash => K::Hash,
        L::Dollar => K::Dollar,

        L::Eq => K::Eq,
        L::Plus => K::Plus,
        L::Minus => K::Minus,
        L::Star => K::Star,
        L::Slash => K::Slash,
        L::Percent => K::Percent,
        L::Lt => K::Lt,
        L::Gt => K::Gt,
        L::Bang => K::Bang,
        L::Amp => K::Amp,
        L::Pipe => K::Pipe,
        L::Caret => K::Caret,
        L::Tilde => K::Tilde,
        L::Colon => K::Colon,
        L::Question => K::Question,
    }
}

/// Tokenize a source string into a flat vector of tokens.
/// Always ends with a synthetic `Eof` token at the end-of-source position.
/// Unrecognized input becomes a `TokenKind::Error` token with the offending span,
/// and lexing continues — the parser is responsible for surfacing the diagnostic.
pub fn lex(source: &str) -> Vec<Token> {
    let mut out = Vec::new();
    for (result, span) in LogosTok::lexer(source).spanned() {
        let kind = match result {
            Ok(t) => map(t),
            Err(()) => TokenKind::Error,
        };
        out.push(Token {
            kind,
            span: Span::from(span),
        });
    }
    let eof = source.len() as u32;
    out.push(Token {
        kind: TokenKind::Eof,
        span: Span::new(eof, eof),
    });
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn kinds(src: &str) -> Vec<TokenKind> {
        lex(src).into_iter().map(|t| t.kind).collect()
    }

    #[test]
    fn empty_source_just_eof() {
        assert_eq!(kinds(""), vec![TokenKind::Eof]);
    }

    #[test]
    fn whitespace_and_comments_skipped() {
        let src = "  // a line comment\n /* block */ fn  ";
        assert_eq!(kinds(src), vec![TokenKind::KwFn, TokenKind::Eof]);
    }

    #[test]
    fn ellipsis_wins_over_dotdot_and_dot() {
        // `...` in `printf(fmt, ...)` must be one Ellipsis token, not `..` + `.`.
        let toks = kinds("(fmt, ...)");
        use TokenKind::*;
        assert_eq!(toks, vec![LParen, Ident, Comma, Ellipsis, RParen, Eof]);
    }

    #[test]
    fn block_comment_with_internal_stars() {
        // Regression: the classic `* /` boundary used to confuse the regex
        // and bail before consuming the closing slash.
        assert_eq!(
            kinds("/* one * two ** three ***/ fn"),
            vec![TokenKind::KwFn, TokenKind::Eof]
        );
    }

    #[test]
    fn keywords_are_two_letter() {
        let src = "fn if el lp rt mt st tr im ex mu br ct";
        use TokenKind::*;
        assert_eq!(
            kinds(src),
            vec![
                KwFn, KwIf, KwEl, KwLp, KwRt, KwMt, KwSt, KwTr, KwIm, KwEx, KwMu, KwBr, KwCt, Eof,
            ]
        );
    }

    #[test]
    fn primitive_types_lex_as_type_keywords_not_idents() {
        let src = "i8 i32 i64 u8 f64 bool str";
        use TokenKind::*;
        assert_eq!(
            kinds(src),
            vec![TyI8, TyI32, TyI64, TyU8, TyF64, TyBool, TyStr, Eof]
        );
    }

    #[test]
    fn ranges_dont_steal_from_int_literals() {
        // `1..5` must be Int, DotDot, Int — not Float, and not Int+Dot+Int either.
        // Regression: earlier the IntLit regex over-extended into a trailing `.`.
        let toks = lex("1..5");
        assert_eq!(
            toks.iter().map(|t| t.kind).collect::<Vec<_>>(),
            vec![
                TokenKind::IntLit,
                TokenKind::DotDot,
                TokenKind::IntLit,
                TokenKind::Eof
            ]
        );
        assert_eq!(toks[0].span, ailang_syntax::Span::new(0, 1));
        assert_eq!(toks[1].span, ailang_syntax::Span::new(1, 3));
        assert_eq!(toks[2].span, ailang_syntax::Span::new(3, 4));
    }

    #[test]
    fn float_lit_requires_digits_on_both_sides() {
        let toks = kinds("3.14 1.0 0.5");
        assert_eq!(
            toks,
            vec![
                TokenKind::FloatLit,
                TokenKind::FloatLit,
                TokenKind::FloatLit,
                TokenKind::Eof
            ]
        );
    }

    #[test]
    fn multi_char_operators_win_over_singles() {
        let toks = kinds(":= -> => |> == != <= >= && || << >> ?? ++ ..=");
        use TokenKind::*;
        assert_eq!(
            toks,
            vec![
                Walrus, Arrow, FatArrow, Pipeline, EqEq, Neq, Le, Ge, AndAnd, OrOr, Shl, Shr,
                Coalesce, Concat, DotDotEq, Eof,
            ]
        );
    }

    #[test]
    fn string_with_escape() {
        let toks = kinds(r#""hi \"there\"""#);
        assert_eq!(toks, vec![TokenKind::StrLit, TokenKind::Eof]);
    }

    #[test]
    fn underscore_is_wildcard_not_ident() {
        let toks = kinds("_");
        assert_eq!(toks, vec![TokenKind::Underscore, TokenKind::Eof]);
    }

    #[test]
    fn underscore_prefixed_ident_is_ident() {
        let toks = kinds("_foo");
        assert_eq!(toks, vec![TokenKind::Ident, TokenKind::Eof]);
    }

    #[test]
    fn unknown_char_becomes_error_token_but_lexing_continues() {
        let toks = kinds("fn `");
        assert_eq!(
            toks,
            vec![TokenKind::KwFn, TokenKind::Error, TokenKind::Eof]
        );
    }

    #[test]
    fn span_recovers_source_slice() {
        let src = "fn main";
        let toks = lex(src);
        assert_eq!(toks[0].span.slice(src), "fn");
        assert_eq!(toks[1].span.slice(src), "main");
    }

    #[test]
    fn fizzbuzz_example_lexes_clean() {
        let src = r#"
            fn main() {
              lp i in 1..101 {
                mt (i%3, i%5) {
                  (0,0) => println("FizzBuzz");
                  _     => println(i);
                }
              }
            }
        "#;
        // No Error tokens.
        assert!(lex(src).iter().all(|t| t.kind != TokenKind::Error));
    }
}

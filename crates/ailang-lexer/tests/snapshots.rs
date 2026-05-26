//! Snapshot tests for the lexer.
//!
//! Each example in `examples/` is lexed and the token-kind sequence is
//! captured by `insta`. Run `cargo insta review` to accept changes.

use ailang_lexer::lex;
use ailang_syntax::{Token, TokenKind};

fn render(source: &str, tokens: &[Token]) -> String {
    let mut s = String::new();
    for t in tokens {
        if t.kind == TokenKind::Eof {
            s.push_str(&format!("{:>4}..{:<4} <eof>\n", t.span.start, t.span.end));
        } else {
            s.push_str(&format!(
                "{:>4}..{:<4} {:<10} {:?}\n",
                t.span.start,
                t.span.end,
                t.kind,
                t.span.slice(source)
            ));
        }
    }
    s
}

macro_rules! snap_example {
    ($name:ident, $path:literal) => {
        #[test]
        fn $name() {
            let src = include_str!(concat!("../../../examples/", $path));
            let toks = lex(src);
            insta::assert_snapshot!(render(src, &toks));
        }
    };
}

snap_example!(hello,    "hello.ail");
snap_example!(fizzbuzz, "fizzbuzz.ail");
snap_example!(fib,      "fib.ail");
snap_example!(wc,       "wc.ail");
snap_example!(ffi,      "ffi.ail");

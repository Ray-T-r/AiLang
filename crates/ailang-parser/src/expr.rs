//! Pratt-style expression parser.
//!
//! Precedence ladder (highest → lowest binding):
//!
//! | Level | Operators                                | Assoc |
//! |-------|------------------------------------------|-------|
//! | 90    | postfix `()` `[]` `.f` `?`               | left  |
//! | 80    | prefix `-` `!`                           | right |
//! | 70    | `*` `/` `%`                              | left  |
//! | 65    | `++`  (concat)                           | left  |
//! | 60    | `+` `-`                                  | left  |
//! | 50    | `<<` `>>`                                | left  |
//! | 45    | `&`                                      | left  |
//! | 40    | `^`                                      | left  |
//! | 35    | `\|`                                     | left  |
//! | 30    | `..` `..=`                               | none  |
//! | 25    | `<` `<=` `>` `>=`                        | left  |
//! | 20    | `==` `!=`                                | left  |
//! | 15    | `&&`                                     | left  |
//! | 10    | `\|\|`                                   | left  |
//! | 7     | ternary `? :`                            | right |
//! | 6     | `\|>`  (pipe)                            | left  |
//! | 5     | `??`   (coalesce)                        | right |

use ailang_syntax::ast::*;
use ailang_syntax::token::TokenKind;

use crate::Parser;

impl Parser<'_> {
    pub(crate) fn parse_expr(&mut self) -> Option<Expr> {
        self.parse_expr_bp(0)
    }

    fn parse_expr_bp(&mut self, min_bp: u8) -> Option<Expr> {
        let mut lhs = self.parse_prefix()?;

        loop {
            // Postfix operators
            if let Some(post_bp) = postfix_bp(self.peek_kind()) {
                if post_bp < min_bp {
                    break;
                }
                lhs = self.parse_postfix(lhs)?;
                continue;
            }
            // Ternary (right-assoc, lowest-level handled inline)
            if self.at(TokenKind::Question) {
                let (l_bp, r_bp) = (7, 6);
                if l_bp < min_bp {
                    break;
                }
                self.bump();
                let then_ = self.parse_expr_bp(0)?;
                self.expect(TokenKind::Colon)?;
                let else_ = self.parse_expr_bp(r_bp)?;
                let span = lhs.span.join(else_.span);
                lhs = Expr {
                    kind: ExprKind::Ternary {
                        cond: Box::new(lhs),
                        then_: Box::new(then_),
                        else_: Box::new(else_),
                    },
                    span,
                };
                continue;
            }
            // Pipe operator
            if self.at(TokenKind::Pipeline) {
                let (l_bp, r_bp) = (6, 7);
                if l_bp < min_bp {
                    break;
                }
                self.bump();
                let rhs = self.parse_expr_bp(r_bp)?;
                let span = lhs.span.join(rhs.span);
                lhs = Expr {
                    kind: ExprKind::Pipe {
                        lhs: Box::new(lhs),
                        rhs: Box::new(rhs),
                    },
                    span,
                };
                continue;
            }
            // Binary operators
            if let Some((op, l_bp, r_bp)) = binop_bp(self.peek_kind()) {
                if l_bp < min_bp {
                    break;
                }
                let op_span = self.bump().span;
                let _ = op_span;
                let rhs = self.parse_expr_bp(r_bp)?;
                let span = lhs.span.join(rhs.span);
                lhs = Expr {
                    kind: ExprKind::Binary {
                        op,
                        lhs: Box::new(lhs),
                        rhs: Box::new(rhs),
                    },
                    span,
                };
                continue;
            }
            break;
        }

        Some(lhs)
    }

    fn parse_prefix(&mut self) -> Option<Expr> {
        use TokenKind::*;
        let tok = self.peek();
        match tok.kind {
            // Prefix unary
            Minus => {
                let start = self.bump().span;
                let operand = self.parse_expr_bp(80)?;
                Some(Expr {
                    span: start.join(operand.span),
                    kind: ExprKind::Unary {
                        op: UnOp::Neg,
                        operand: Box::new(operand),
                    },
                })
            }
            Bang => {
                let start = self.bump().span;
                let operand = self.parse_expr_bp(80)?;
                Some(Expr {
                    span: start.join(operand.span),
                    kind: ExprKind::Unary {
                        op: UnOp::Not,
                        operand: Box::new(operand),
                    },
                })
            }
            Star => {
                // Only legal in FFI contexts, but parse it everywhere; sema rejects.
                let start = self.bump().span;
                let operand = self.parse_expr_bp(80)?;
                Some(Expr {
                    span: start.join(operand.span),
                    kind: ExprKind::Unary {
                        op: UnOp::Deref,
                        operand: Box::new(operand),
                    },
                })
            }
            Amp => {
                let start = self.bump().span;
                let operand = self.parse_expr_bp(80)?;
                Some(Expr {
                    span: start.join(operand.span),
                    kind: ExprKind::Unary {
                        op: UnOp::AddrOf,
                        operand: Box::new(operand),
                    },
                })
            }

            // Parenthesized / tuple
            LParen => {
                let start = self.bump().span;
                if self.at(RParen) {
                    let end = self.bump().span;
                    return Some(Expr {
                        kind: ExprKind::Tuple(Vec::new()),
                        span: start.join(end),
                    });
                }
                let first = self.parse_expr()?;
                if self.at(Comma) {
                    let mut elems = vec![first];
                    while self.eat(Comma) {
                        if self.at(RParen) {
                            break;
                        }
                        elems.push(self.parse_expr()?);
                    }
                    let end = self.expect(RParen)?.span;
                    return Some(Expr {
                        kind: ExprKind::Tuple(elems),
                        span: start.join(end),
                    });
                }
                let end = self.expect(RParen)?.span;
                // single-element parens collapses to the inner expr but
                // preserves the outer span (for error messages).
                Some(Expr {
                    span: start.join(end),
                    kind: first.kind,
                })
            }

            // Array literal
            LBracket => {
                let start = self.bump().span;
                let mut elems = Vec::new();
                while !self.at(RBracket) && !self.at(Eof) {
                    elems.push(self.parse_expr()?);
                    if !self.eat(Comma) {
                        break;
                    }
                }
                let end = self.expect(RBracket)?.span;
                Some(Expr {
                    kind: ExprKind::Array(elems),
                    span: start.join(end),
                })
            }

            // Map literal or block
            LBrace => {
                // Decide between block and map by looking past the `{`.
                // Heuristic: if the body starts with `expr :` it's a map;
                // empty `{}` is an empty map. Otherwise treat as block.
                let is_map = self.is_map_literal();
                if is_map {
                    let start = self.bump().span;
                    let mut entries = Vec::new();
                    while !self.at(RBrace) && !self.at(Eof) {
                        let k = self.parse_expr()?;
                        self.expect(Colon)?;
                        let v = self.parse_expr()?;
                        entries.push((k, v));
                        if !self.eat(Comma) {
                            break;
                        }
                    }
                    let end = self.expect(RBrace)?.span;
                    Some(Expr {
                        kind: ExprKind::Map(entries),
                        span: start.join(end),
                    })
                } else {
                    let blk = self.parse_block()?;
                    Some(Expr {
                        span: blk.span,
                        kind: ExprKind::Block(blk),
                    })
                }
            }

            // Lambda
            KwFn if self.peek_ahead(1) == LParen => {
                let start = self.bump().span;
                let (params, _) = self.parse_param_list_lambda()?;
                // body is either a block or a single expression
                let body = if self.at(LBrace) {
                    LambdaBody::Block(self.parse_block()?)
                } else {
                    LambdaBody::Expr(Box::new(self.parse_expr_bp(0)?))
                };
                let end = match &body {
                    LambdaBody::Block(b) => b.span,
                    LambdaBody::Expr(e) => e.span,
                };
                Some(Expr {
                    kind: ExprKind::Lambda { params, body },
                    span: start.join(end),
                })
            }

            // `if` as expression
            KwIf => {
                let if_ = self.parse_if_expr()?;
                let span = if_.span;
                Some(Expr {
                    kind: ExprKind::If(Box::new(if_)),
                    span,
                })
            }

            // `mt` as expression
            KwMt => {
                let mt = self.parse_match_expr()?;
                let span = mt.span;
                Some(Expr {
                    kind: ExprKind::Match(Box::new(mt)),
                    span,
                })
            }

            // Literals
            IntLit | FloatLit | StrLit | CharLit | KwTrue | KwFalse | KwNil => {
                let lit = self.parse_literal()?;
                Some(Expr {
                    span: lit.span,
                    kind: ExprKind::Lit(lit),
                })
            }

            Ident => {
                let span = self.bump().span;
                Some(Expr {
                    kind: ExprKind::Ident(self.ident_text(span)),
                    span,
                })
            }

            Underscore => {
                let span = self.bump().span;
                Some(Expr {
                    kind: ExprKind::Underscore,
                    span,
                })
            }

            _ => {
                self.error(
                    format!("expected expression, got `{}`", tok.kind),
                    tok.span,
                );
                None
            }
        }
    }

    fn parse_postfix(&mut self, lhs: Expr) -> Option<Expr> {
        use TokenKind::*;
        match self.peek_kind() {
            LParen => {
                self.bump();
                let mut args = Vec::new();
                while !self.at(RParen) && !self.at(Eof) {
                    args.push(self.parse_expr()?);
                    if !self.eat(Comma) {
                        break;
                    }
                }
                let end = self.expect(RParen)?.span;
                let span = lhs.span.join(end);
                Some(Expr {
                    kind: ExprKind::Call {
                        callee: Box::new(lhs),
                        args,
                    },
                    span,
                })
            }
            LBracket => {
                self.bump();
                let index = self.parse_expr()?;
                let end = self.expect(RBracket)?.span;
                let span = lhs.span.join(end);
                Some(Expr {
                    kind: ExprKind::Index {
                        container: Box::new(lhs),
                        index: Box::new(index),
                    },
                    span,
                })
            }
            Dot => {
                self.bump();
                let name = self.parse_ident()?;
                let span = lhs.span.join(name.span);
                Some(Expr {
                    kind: ExprKind::Field {
                        container: Box::new(lhs),
                        name,
                    },
                    span,
                })
            }
            Question => {
                let span = self.bump().span;
                let total = lhs.span.join(span);
                Some(Expr {
                    kind: ExprKind::Try(Box::new(lhs)),
                    span: total,
                })
            }
            _ => unreachable!("parse_postfix called on non-postfix token"),
        }
    }

    fn parse_if_expr(&mut self) -> Option<IfStmt> {
        // delegate to statement form
        crate::Parser::parse_if(self, false)
    }

    fn parse_match_expr(&mut self) -> Option<MatchStmt> {
        crate::Parser::parse_match(self, false)
    }

    fn parse_param_list_lambda(&mut self) -> Option<(Vec<Param>, bool)> {
        // delegate
        self.parse_param_list_inner_lambda()
    }

    fn parse_param_list_inner_lambda(&mut self) -> Option<(Vec<Param>, bool)> {
        // Same as parse_param_list but doesn't allow `...`.
        self.expect(TokenKind::LParen)?;
        let mut params = Vec::new();
        while !self.at(TokenKind::RParen) && !self.at(TokenKind::Eof) {
            let name = self.parse_ident()?;
            let mut ty = None;
            let mut end = name.span;
            if self.eat(TokenKind::Colon) {
                let t = self.parse_type()?;
                end = t.span;
                ty = Some(t);
            }
            params.push(Param {
                span: name.span.join(end),
                name,
                ty,
            });
            if !self.eat(TokenKind::Comma) {
                break;
            }
        }
        self.expect(TokenKind::RParen)?;
        Some((params, false))
    }

    /// Heuristic for distinguishing `{ k: v }` from `{ stmt; ... }`. The lexer
    /// emits the same `LBrace`, so we look ahead a few tokens.
    fn is_map_literal(&self) -> bool {
        // Save position, scan a bit, decide.
        let save = self.cursor + 1; // past the `{`
        // Empty `{}` is a map literal.
        if self.tokens.get(save).map(|t| t.kind) == Some(TokenKind::RBrace) {
            return true;
        }
        // Look up to ~6 tokens ahead for an unambiguous `:` before `;`, `=`, or `,`.
        let mut depth_paren = 0;
        let mut depth_bracket = 0;
        let mut depth_brace = 0;
        for i in 0..16 {
            let Some(t) = self.tokens.get(save + i) else { return false };
            match t.kind {
                TokenKind::LParen => depth_paren += 1,
                TokenKind::RParen => depth_paren -= 1,
                TokenKind::LBracket => depth_bracket += 1,
                TokenKind::RBracket => depth_bracket -= 1,
                TokenKind::LBrace => depth_brace += 1,
                TokenKind::RBrace => {
                    if depth_brace == 0 {
                        return false;
                    }
                    depth_brace -= 1;
                }
                TokenKind::Colon
                    if depth_paren == 0 && depth_bracket == 0 && depth_brace == 0 =>
                {
                    return true;
                }
                TokenKind::Semi | TokenKind::Walrus | TokenKind::Eq
                    if depth_paren == 0 && depth_bracket == 0 && depth_brace == 0 =>
                {
                    return false;
                }
                TokenKind::Eof => return false,
                _ => {}
            }
        }
        false
    }
}

// ---- precedence tables ----

fn binop_bp(k: TokenKind) -> Option<(BinOp, u8, u8)> {
    // Don't `use BinOp::*` — several variant names (Shl/Shr/Concat/Coalesce/Lt/...)
    // collide with TokenKind variants, which would silently mis-pattern-match.
    use TokenKind::*;
    let (op, l, r) = match k {
        OrOr     => (BinOp::Or,        10, 11),
        AndAnd   => (BinOp::And,       15, 16),
        EqEq     => (BinOp::Eq,        20, 21),
        Neq      => (BinOp::Ne,        20, 21),
        Lt       => (BinOp::Lt,        25, 26),
        Le       => (BinOp::Le,        25, 26),
        Gt       => (BinOp::Gt,        25, 26),
        Ge       => (BinOp::Ge,        25, 26),
        DotDot   => (BinOp::Range,     30, 31),
        DotDotEq => (BinOp::RangeEq,   30, 31),
        Pipe     => (BinOp::BitOr,     35, 36),
        Caret    => (BinOp::BitXor,    40, 41),
        Amp      => (BinOp::BitAnd,    45, 46),
        Shl      => (BinOp::Shl,       50, 51),
        Shr      => (BinOp::Shr,       50, 51),
        Plus     => (BinOp::Add,       60, 61),
        Minus    => (BinOp::Sub,       60, 61),
        Concat   => (BinOp::Concat,    65, 66),
        Star     => (BinOp::Mul,       70, 71),
        Slash    => (BinOp::Div,       70, 71),
        Percent  => (BinOp::Mod,       70, 71),
        Coalesce => (BinOp::Coalesce,   5,  4), // right-assoc
        _ => return None,
    };
    Some((op, l, r))
}

fn postfix_bp(k: TokenKind) -> Option<u8> {
    use TokenKind::*;
    match k {
        LParen | LBracket | Dot | Question => Some(90),
        _ => None,
    }
}

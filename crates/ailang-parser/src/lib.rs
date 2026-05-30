//! AiLang parser — recursive descent + Pratt for expressions.
//!
//! Entry point: [`parse`]. Returns a `Module` plus a flat list of
//! [`ParseError`]s. The parser never panics on bad input — it records the
//! error, synchronizes to a known boundary (`;`, `}`, top-level `fn`/`st`/`ex`),
//! and keeps going so users see every problem in one run.

use ailang_syntax::ast::*;
pub use ailang_syntax::token::Span;
use ailang_syntax::token::{Token, TokenKind};

mod expr;
mod literal;

#[derive(Clone, Debug)]
pub struct ParseError {
    pub message: String,
    pub span: Span,
    pub help: Option<String>,
}

impl ParseError {
    pub fn new(message: impl Into<String>, span: Span) -> Self {
        Self {
            message: message.into(),
            span,
            help: None,
        }
    }
    pub fn with_help(mut self, help: impl Into<String>) -> Self {
        self.help = Some(help.into());
        self
    }
}

pub struct Parser<'a> {
    tokens: &'a [Token],
    pub(crate) source: &'a str,
    cursor: usize,
    pub(crate) errors: Vec<ParseError>,
}

/// Public entry point. Always returns *some* `Module` — partial parses are
/// useful for IDE-style tooling.
pub fn parse(source: &str, tokens: &[Token]) -> (Module, Vec<ParseError>) {
    let mut p = Parser {
        tokens,
        source,
        cursor: 0,
        errors: vec![],
    };
    let module = p.parse_module();
    (module, p.errors)
}

// ============================================================================
// Cursor & utilities
// ============================================================================

impl<'a> Parser<'a> {
    pub(crate) fn peek(&self) -> Token {
        self.tokens[self.cursor.min(self.tokens.len() - 1)]
    }
    pub(crate) fn peek_kind(&self) -> TokenKind {
        self.peek().kind
    }
    pub(crate) fn peek_ahead(&self, n: usize) -> TokenKind {
        self.tokens
            .get(self.cursor + n)
            .map(|t| t.kind)
            .unwrap_or(TokenKind::Eof)
    }
    pub(crate) fn bump(&mut self) -> Token {
        let t = self.peek();
        if t.kind != TokenKind::Eof {
            self.cursor += 1;
        }
        t
    }
    pub(crate) fn at(&self, kind: TokenKind) -> bool {
        self.peek_kind() == kind
    }
    pub(crate) fn eat(&mut self, kind: TokenKind) -> bool {
        if self.at(kind) {
            self.bump();
            true
        } else {
            false
        }
    }
    pub(crate) fn expect(&mut self, kind: TokenKind) -> Option<Token> {
        if self.at(kind) {
            Some(self.bump())
        } else {
            let got = self.peek();
            self.error(format!("expected `{}`, got `{}`", kind, got.kind), got.span);
            None
        }
    }
    pub(crate) fn ident_text(&self, span: Span) -> String {
        span.slice(self.source).to_string()
    }
    pub(crate) fn error(&mut self, msg: impl Into<String>, span: Span) {
        self.errors.push(ParseError::new(msg, span));
    }

    /// Skip tokens until we hit a top-level synchronization point so the next
    /// item parses cleanly.
    fn synchronize_item(&mut self) {
        use TokenKind::*;
        loop {
            match self.peek_kind() {
                Eof | KwFn | KwSt | KwEn | KwEx | KwIm | KwTr => break,
                _ => {
                    self.bump();
                }
            }
        }
    }

    /// Within a block: skip to next `;` or `}` so the next statement can parse.
    fn synchronize_stmt(&mut self) {
        use TokenKind::*;
        loop {
            match self.peek_kind() {
                Eof | Semi | RBrace => break,
                _ => {
                    self.bump();
                }
            }
        }
        self.eat(Semi);
    }
}

// ============================================================================
// Top-level: module & items
// ============================================================================

impl Parser<'_> {
    fn parse_module(&mut self) -> Module {
        let mut items = Vec::new();
        let mut top_stmts: Vec<Stmt> = Vec::new();
        let mut top_tail: Option<Expr> = None;

        while !self.at(TokenKind::Eof) {
            let start_cursor = self.cursor;

            // 1) Item keyword? Parse it as an item.
            if matches!(
                self.peek_kind(),
                TokenKind::KwFn
                    | TokenKind::KwSt
                    | TokenKind::KwEn
                    | TokenKind::KwIm
                    | TokenKind::KwEx
                    | TokenKind::KwCinc
            ) {
                if let Some(item) = self.parse_item() {
                    items.push(item);
                } else {
                    if self.cursor == start_cursor {
                        let tok = self.bump();
                        self.error(
                            format!("unexpected token `{}` at top level", tok.kind),
                            tok.span,
                        );
                    }
                    self.synchronize_item();
                }
                continue;
            }

            // 2) Otherwise: treat as a top-level statement for implicit `main`.
            if let Some(prev_tail) = top_tail.take() {
                top_stmts.push(Stmt::Expr(prev_tail));
            }
            match self.parse_stmt_or_tail() {
                Some(StmtOrTail::Stmt(s)) => {
                    top_stmts.push(s);
                    self.eat(TokenKind::Semi);
                }
                Some(StmtOrTail::TailExpr(e)) => {
                    if self.at(TokenKind::Semi) {
                        self.bump();
                        top_stmts.push(Stmt::Expr(e));
                    } else {
                        top_tail = Some(e);
                    }
                }
                None => {
                    if self.cursor == start_cursor {
                        let tok = self.bump();
                        self.error(
                            format!("unexpected token `{}` at top level", tok.kind),
                            tok.span,
                        );
                    }
                    self.synchronize_item();
                }
            }
        }

        // 3) If any top-level statements were collected AND no explicit `main`,
        //    synthesize `fn main() { <top_stmts> }`. This is the implicit-main
        //    token saver — small scripts skip the `fn main() { ... }` wrapper.
        let has_explicit_main = items
            .iter()
            .any(|i| matches!(i, Item::Fn(f) if f.name.name == "main"));
        let has_top_stmts = !top_stmts.is_empty() || top_tail.is_some();
        if has_top_stmts {
            if has_explicit_main {
                let span = top_stmts
                    .first()
                    .map(|s| stmt_span(s))
                    .or(top_tail.as_ref().map(|e| e.span))
                    .unwrap_or(Span::empty());
                self.error(
                    "top-level statements not allowed when `fn main()` is also defined",
                    span,
                );
            } else {
                if let Some(t) = top_tail.take() {
                    top_stmts.push(Stmt::Expr(t));
                }
                let body_span = top_stmts
                    .first()
                    .map(|s| stmt_span(s))
                    .unwrap_or(Span::empty());
                let body = Block {
                    stmts: top_stmts,
                    tail_expr: None,
                    span: body_span,
                };
                let synth_main = FnDecl {
                    name: Ident {
                        name: "main".to_string(),
                        span: Span::empty(),
                    },
                    type_params: Vec::new(),
                    params: Vec::new(),
                    return_ty: None,
                    body,
                    span: body_span,
                };
                items.push(Item::Fn(synth_main));
            }
        }

        Module { items }
    }

    fn parse_item(&mut self) -> Option<Item> {
        match self.peek_kind() {
            TokenKind::KwFn => self.parse_fn_item().map(Item::Fn),
            TokenKind::KwSt => self.parse_struct_item().map(Item::Struct),
            TokenKind::KwEn => self.parse_enum_item().map(Item::Enum),
            TokenKind::KwIm => self.parse_import_item().map(Item::Import),
            TokenKind::KwEx => self.parse_extern_item().map(Item::Extern),
            TokenKind::KwCinc => self.parse_cinclude_item().map(Item::CInclude),
            _ => None,
        }
    }

    /// `en Name { Variant, Variant(field:T), … }`. Trailing commas/semis
    /// are fine; field type annotations are required (no inference yet).
    fn parse_enum_item(&mut self) -> Option<EnumDecl> {
        let start = self.peek().span;
        self.expect(TokenKind::KwEn)?;
        let name = self.parse_ident()?;
        self.expect(TokenKind::LBrace)?;
        let mut variants = Vec::new();
        while !self.at(TokenKind::RBrace) && !self.at(TokenKind::Eof) {
            let v_name = self.parse_ident()?;
            let mut fields = Vec::new();
            let mut v_end = v_name.span;
            if self.eat(TokenKind::LParen) {
                while !self.at(TokenKind::RParen) && !self.at(TokenKind::Eof) {
                    let fname = self.parse_ident()?;
                    self.expect(TokenKind::Colon)?;
                    let ty = self.parse_type()?;
                    let end = ty.span;
                    fields.push(Field {
                        span: fname.span.join(end),
                        name: fname,
                        ty,
                    });
                    if !self.eat(TokenKind::Comma) {
                        break;
                    }
                }
                v_end = self.expect(TokenKind::RParen)?.span;
            }
            variants.push(EnumVariant {
                span: v_name.span.join(v_end),
                name: v_name,
                fields,
            });
            if !self.eat(TokenKind::Comma) && !self.eat(TokenKind::Semi) {
                break;
            }
        }
        let end = self.expect(TokenKind::RBrace)?.span;
        Some(EnumDecl {
            name,
            variants,
            span: start.join(end),
        })
    }

    fn parse_fn_item(&mut self) -> Option<FnDecl> {
        let start = self.peek().span;
        self.expect(TokenKind::KwFn)?;
        let name = self.parse_ident()?;
        let type_params = self.parse_type_params();
        let params = self.parse_param_list(/*allow_variadic=*/ false)?.0;
        let return_ty = self.parse_return_type()?;
        // Body is either a normal block `{ ... }` or, per spec §1.6, a single
        // bare expression: `fn add(a,b:i64) -> i64 a + b`. We desugar the
        // expression form into a one-tail-expr block.
        let body = if self.at(TokenKind::LBrace) {
            self.parse_block()?
        } else {
            let e = self.parse_expr()?;
            Block {
                span: e.span,
                stmts: Vec::new(),
                tail_expr: Some(Box::new(e)),
            }
        };
        Some(FnDecl {
            name,
            type_params,
            params,
            return_ty,
            span: start.join(body.span),
            body,
        })
    }

    /// Optional `<T1, T2, ...>` clause after a fn name. Always returns a
    /// Vec (empty when absent).
    fn parse_type_params(&mut self) -> Vec<Ident> {
        let mut out = Vec::new();
        if !self.at(TokenKind::Lt) {
            return out;
        }
        self.bump(); // <
        while !self.at(TokenKind::Gt) && !self.at(TokenKind::Eof) {
            if let Some(id) = self.parse_ident() {
                out.push(id);
            }
            if !self.eat(TokenKind::Comma) {
                break;
            }
        }
        self.expect(TokenKind::Gt);
        out
    }

    fn parse_struct_item(&mut self) -> Option<StructDecl> {
        let start = self.peek().span;
        self.expect(TokenKind::KwSt)?;
        let name = self.parse_ident()?;
        self.expect(TokenKind::LBrace)?;
        let mut fields = Vec::new();
        while !self.at(TokenKind::RBrace) && !self.at(TokenKind::Eof) {
            let fname = self.parse_ident()?;
            self.expect(TokenKind::Colon)?;
            let ty = self.parse_type()?;
            // Accept either `;` or `,` as separators; both are optional
            // before the closing `}`.
            let end = if self.at(TokenKind::Semi) || self.at(TokenKind::Comma) {
                self.bump().span
            } else {
                ty.span
            };
            fields.push(Field {
                span: fname.span.join(end),
                name: fname,
                ty,
            });
        }
        let end = self.expect(TokenKind::RBrace)?.span;
        Some(StructDecl {
            name,
            fields,
            span: start.join(end),
        })
    }

    fn parse_import_item(&mut self) -> Option<ImportDecl> {
        let start = self.peek().span;
        self.expect(TokenKind::KwIm)?;
        let path = self.parse_str_literal()?;
        let mut alias = None;
        let mut end = path.span;
        if self.eat(TokenKind::KwAs) {
            let id = self.parse_ident()?;
            end = id.span;
            alias = Some(id);
        }
        Some(ImportDecl {
            path,
            alias,
            span: start.join(end),
        })
    }

    /// `cinc "header.h"` — a C `#include`. Mirrors `parse_import_item`, but the
    /// payload names a C header (its decls/macros/typedefs), not an AiLang module.
    fn parse_cinclude_item(&mut self) -> Option<CIncludeDecl> {
        let start = self.peek().span;
        self.expect(TokenKind::KwCinc)?;
        let header = self.parse_str_literal()?;
        Some(CIncludeDecl {
            span: start.join(header.span),
            header,
        })
    }

    fn parse_extern_item(&mut self) -> Option<ExternDecl> {
        let start = self.peek().span;
        self.expect(TokenKind::KwEx)?;
        let lib = if self.at(TokenKind::StrLit) {
            Some(self.parse_str_literal()?)
        } else {
            None
        };
        let sig = self.parse_fn_sig(/*allow_variadic=*/ true)?;
        Some(ExternDecl {
            lib,
            span: start.join(sig.span),
            sig,
        })
    }

    fn parse_fn_sig(&mut self, allow_variadic: bool) -> Option<FnSig> {
        let start = self.peek().span;
        self.expect(TokenKind::KwFn)?;
        let name = self.parse_ident()?;
        let (params, variadic) = self.parse_param_list(allow_variadic)?;
        let return_ty = self.parse_return_type()?;
        let end = return_ty
            .as_ref()
            .map(|t| t.span)
            .unwrap_or_else(|| name.span);
        Some(FnSig {
            name,
            params,
            variadic,
            return_ty,
            span: start.join(end),
        })
    }

    /// Returns `(params, has_variadic)`. `has_variadic` is only true when
    /// `allow_variadic` and the list ended with `, ...`.
    fn parse_param_list(&mut self, allow_variadic: bool) -> Option<(Vec<Param>, bool)> {
        self.expect(TokenKind::LParen)?;
        let mut params = Vec::new();
        let mut variadic = false;
        while !self.at(TokenKind::RParen) && !self.at(TokenKind::Eof) {
            if self.at(TokenKind::Ellipsis) {
                let span = self.bump().span;
                if !allow_variadic {
                    self.error("`...` is only allowed in `extern` signatures", span);
                }
                variadic = true;
                break;
            }
            // Optional `mu` prefix marks the param as locally mutable.
            // C already passes by value, so we just discard the marker —
            // it's a parser-level affordance to skip the `mu x := x` dance
            // at the top of a body that wants to mutate its parameter.
            let start = if self.at(TokenKind::KwMu) {
                self.bump().span
            } else {
                self.peek().span
            };
            let name = self.parse_ident()?;
            let mut ty = None;
            let mut end = name.span;
            if self.eat(TokenKind::Colon) {
                let t = self.parse_type()?;
                end = t.span;
                ty = Some(t);
            }
            params.push(Param {
                span: start.join(end),
                name,
                ty,
            });
            if !self.eat(TokenKind::Comma) {
                break;
            }
        }
        self.expect(TokenKind::RParen)?;
        Some((params, variadic))
    }

    fn parse_return_type(&mut self) -> Option<Option<Type>> {
        if self.eat(TokenKind::Arrow) {
            Some(Some(self.parse_type()?))
        } else {
            Some(None)
        }
    }
}

// ============================================================================
// Statements & blocks
// ============================================================================

impl Parser<'_> {
    pub(crate) fn parse_block(&mut self) -> Option<Block> {
        let start = self.expect(TokenKind::LBrace)?.span;
        let mut stmts = Vec::new();
        let mut tail_expr: Option<Box<Expr>> = None;

        while !self.at(TokenKind::RBrace) && !self.at(TokenKind::Eof) {
            // Try to parse a statement; flush prior tail_expr first.
            if let Some(prev_tail) = tail_expr.take() {
                stmts.push(Stmt::Expr(*prev_tail));
            }
            let stmt_or_expr = self.parse_stmt_or_tail();
            match stmt_or_expr {
                Some(StmtOrTail::Stmt(s)) => {
                    stmts.push(s);
                    // Optional trailing `;`
                    self.eat(TokenKind::Semi);
                }
                Some(StmtOrTail::TailExpr(e)) => {
                    if self.at(TokenKind::Semi) {
                        self.bump();
                        stmts.push(Stmt::Expr(e));
                    } else {
                        tail_expr = Some(Box::new(e));
                    }
                }
                None => {
                    self.synchronize_stmt();
                }
            }
        }
        let end = self.expect(TokenKind::RBrace)?.span;
        Some(Block {
            stmts,
            tail_expr,
            span: start.join(end),
        })
    }

    fn parse_stmt_or_tail(&mut self) -> Option<StmtOrTail> {
        use TokenKind::*;
        match self.peek_kind() {
            KwRt => {
                let start = self.bump().span;
                let value = if matches!(self.peek_kind(), Semi | RBrace) {
                    None
                } else {
                    Some(self.parse_expr()?)
                };
                let end = value.as_ref().map(|e| e.span).unwrap_or(start);
                Some(StmtOrTail::Stmt(Stmt::Return {
                    value,
                    span: start.join(end),
                }))
            }
            KwBr => {
                let span = self.bump().span;
                Some(StmtOrTail::Stmt(Stmt::Break(span)))
            }
            KwCt => {
                let span = self.bump().span;
                Some(StmtOrTail::Stmt(Stmt::Continue(span)))
            }
            KwIf => {
                let if_ = self.parse_if(/*is_stmt=*/ true)?;
                Some(StmtOrTail::Stmt(Stmt::If(if_)))
            }
            KwLp => {
                let lp = self.parse_loop()?;
                Some(StmtOrTail::Stmt(Stmt::Loop(lp)))
            }
            KwMt => {
                let mt = self.parse_match(/*is_stmt=*/ true)?;
                Some(StmtOrTail::Stmt(Stmt::Match(mt)))
            }
            KwMu => {
                let start = self.bump().span;
                let decl = self.parse_decl_after_mu(true, start)?;
                Some(StmtOrTail::Stmt(decl))
            }
            _ => {
                // Either:
                //   ident := ...          (declaration; lvalue=ident)
                //   ident : ty := ...     (declaration with type)
                //   lvalue = expr         (assignment)
                //   expr                  (expression statement or tail)
                if self.looks_like_decl() {
                    return Some(StmtOrTail::Stmt(
                        self.parse_decl_after_mu(false, self.peek().span)?,
                    ));
                }
                let expr = self.parse_expr()?;
                if self.at(TokenKind::Eq) {
                    self.bump();
                    let value = self.parse_expr()?;
                    let span = expr.span.join(value.span);
                    return Some(StmtOrTail::Stmt(Stmt::Assign {
                        target: expr,
                        value,
                        span,
                    }));
                }
                // Compound assignment: `x += y` desugars to `x = x + y`.
                if let Some(op) = self.peek_compound_assign_op() {
                    self.bump();
                    let rhs = self.parse_expr()?;
                    let span = expr.span.join(rhs.span);
                    let new_value = Expr {
                        span,
                        kind: ExprKind::Binary {
                            op,
                            lhs: Box::new(expr.clone()),
                            rhs: Box::new(rhs),
                        },
                    };
                    return Some(StmtOrTail::Stmt(Stmt::Assign {
                        target: expr,
                        value: new_value,
                        span,
                    }));
                }
                Some(StmtOrTail::TailExpr(expr))
            }
        }
    }

    /// If the current token is a compound-assignment operator, return the
    /// matching `BinOp`. Otherwise `None`.
    fn peek_compound_assign_op(&self) -> Option<BinOp> {
        match self.peek_kind() {
            TokenKind::PlusEq => Some(BinOp::Add),
            TokenKind::MinusEq => Some(BinOp::Sub),
            TokenKind::StarEq => Some(BinOp::Mul),
            TokenKind::SlashEq => Some(BinOp::Div),
            TokenKind::PercentEq => Some(BinOp::Mod),
            _ => None,
        }
    }

    /// Lookahead: starts with an ident, then either `:=` or `: type :=`?
    fn looks_like_decl(&self) -> bool {
        if !matches!(self.peek_kind(), TokenKind::Ident | TokenKind::Underscore) {
            return false;
        }
        match self.peek_ahead(1) {
            TokenKind::Walrus => true,
            TokenKind::Colon => {
                // We don't fully parse the type — but `ident : ... :=` is a
                // declaration; `ident : ... =` is treated as a struct literal
                // (we don't have those yet, so for now always treat `:` as decl).
                true
            }
            _ => false,
        }
    }

    fn parse_decl_after_mu(&mut self, mutable: bool, start_span: Span) -> Option<Stmt> {
        let name = self.parse_ident()?;
        let ty = if self.eat(TokenKind::Colon) {
            Some(self.parse_type()?)
        } else {
            None
        };
        // Allow both `:=` and `=` (after explicit type annotation, `=` is the
        // initializer; without annotation we strictly require `:=`).
        let saw_walrus = self.eat(TokenKind::Walrus);
        if !saw_walrus {
            if ty.is_some() {
                if !self.eat(TokenKind::Eq) {
                    let got = self.peek();
                    self.error(
                        format!("expected `=` or `:=` in declaration, got `{}`", got.kind),
                        got.span,
                    );
                    return None;
                }
            } else {
                let got = self.peek();
                self.error(
                    format!("expected `:=` in declaration, got `{}`", got.kind),
                    got.span,
                );
                return None;
            }
        }
        let value = self.parse_expr()?;
        let span = start_span.join(value.span);
        Some(Stmt::Decl {
            mutable,
            name,
            ty,
            value,
            span,
        })
    }

    fn parse_if(&mut self, _is_stmt: bool) -> Option<IfStmt> {
        let start = self.expect(TokenKind::KwIf)?.span;
        let cond = self.parse_expr()?;
        let then_branch = self.parse_block_or_single_stmt()?;
        let mut else_branch = None;
        let mut end = then_branch.span;
        if self.eat(TokenKind::KwEl) {
            if self.at(TokenKind::KwIf) {
                let inner = self.parse_if(true)?;
                end = inner.span;
                else_branch = Some(ElseBranch::If(Box::new(inner)));
            } else {
                let blk = self.parse_block_or_single_stmt()?;
                end = blk.span;
                else_branch = Some(ElseBranch::Block(blk));
            }
        }
        Some(IfStmt {
            cond,
            then_branch,
            else_branch,
            span: start.join(end),
        })
    }

    /// `if`/`el`/`lp` bodies may be a block `{...}` or a single statement.
    /// Wrap a single stmt in a synthetic block to keep downstream uniform.
    /// If the single body is a tail expression (not a real statement),
    /// preserve it as the block's `tail_expr` so codegen can recognize
    /// implicit return when the if/match is in a value-yielding position.
    fn parse_block_or_single_stmt(&mut self) -> Option<Block> {
        if self.at(TokenKind::LBrace) {
            return self.parse_block();
        }
        let start = self.peek().span;
        match self.parse_stmt_or_tail()? {
            StmtOrTail::Stmt(stmt) => {
                let span = match &stmt {
                    Stmt::Decl { span, .. }
                    | Stmt::Assign { span, .. }
                    | Stmt::Return { span, .. }
                    | Stmt::Break(span)
                    | Stmt::Continue(span) => *span,
                    Stmt::If(s) => s.span,
                    Stmt::Loop(s) => s.span,
                    Stmt::Match(s) => s.span,
                    Stmt::Expr(e) => e.span,
                };
                Some(Block {
                    stmts: vec![stmt],
                    tail_expr: None,
                    span: start.join(span),
                })
            }
            StmtOrTail::TailExpr(e) => {
                let span = e.span;
                Some(Block {
                    stmts: vec![],
                    tail_expr: Some(Box::new(e)),
                    span: start.join(span),
                })
            }
        }
    }

    fn parse_loop(&mut self) -> Option<LoopStmt> {
        let start = self.expect(TokenKind::KwLp)?.span;
        // Four shapes:
        //   lp { ... }                       -> infinite
        //   lp ident in expr { ... }         -> for-in (single var)
        //   lp (k, v) in expr { ... }        -> for-in with tuple destructure
        //                                       (intended for map iteration)
        //   lp expr { ... }                  -> while
        let head = if self.at(TokenKind::LBrace) {
            None
        } else if self.at(TokenKind::Ident) && self.peek_ahead(1) == TokenKind::KwIn {
            let var = self.parse_ident()?;
            self.expect(TokenKind::KwIn)?;
            let iter = self.parse_expr()?;
            Some(LoopHead::ForIn {
                vars: vec![var],
                iter,
            })
        } else if self.at(TokenKind::LParen) && self.peek_ahead(1) == TokenKind::Ident {
            // Tuple destructure: `(k, v, …)`. Only valid when followed by `in`.
            self.bump(); // (
            let mut vars = Vec::new();
            while !self.at(TokenKind::RParen) && !self.at(TokenKind::Eof) {
                vars.push(self.parse_ident()?);
                if !self.eat(TokenKind::Comma) {
                    break;
                }
            }
            self.expect(TokenKind::RParen)?;
            self.expect(TokenKind::KwIn)?;
            let iter = self.parse_expr()?;
            Some(LoopHead::ForIn { vars, iter })
        } else {
            let e = self.parse_expr()?;
            Some(LoopHead::While(e))
        };
        let body = self.parse_block_or_single_stmt()?;
        let end = body.span;
        Some(LoopStmt {
            head,
            body,
            span: start.join(end),
        })
    }

    fn parse_match(&mut self, _is_stmt: bool) -> Option<MatchStmt> {
        let start = self.expect(TokenKind::KwMt)?.span;
        let scrutinee = self.parse_expr()?;
        self.expect(TokenKind::LBrace)?;
        let mut arms = Vec::new();
        while !self.at(TokenKind::RBrace) && !self.at(TokenKind::Eof) {
            let p = self.parse_pattern()?;
            self.expect(TokenKind::FatArrow)?;
            let body = self.parse_expr()?;
            let span = pattern_span(&p).join(body.span);
            arms.push(MatchArm {
                pattern: p,
                body,
                span,
            });
            // arms separated by `;` (optional after the last one).
            if !self.eat(TokenKind::Semi) {
                break;
            }
        }
        let end = self.expect(TokenKind::RBrace)?.span;
        Some(MatchStmt {
            scrutinee,
            arms,
            span: start.join(end),
        })
    }

    fn parse_pattern(&mut self) -> Option<Pattern> {
        use TokenKind::*;
        let tok = self.peek();
        match tok.kind {
            Underscore => {
                self.bump();
                Some(Pattern::Wildcard(tok.span))
            }
            IntLit | FloatLit | StrLit | CharLit | KwTrue | KwFalse | KwNil => {
                let lit = self.parse_literal()?;
                Some(Pattern::Literal(lit))
            }
            Ident => {
                let id = self.parse_ident()?;
                // Capitalized + `(` → variant pattern that binds positional
                // fields (`Some(v)`). Bare capitalized → unit variant
                // (`None`). Lowercase → plain binding.
                let starts_upper = id
                    .name
                    .chars()
                    .next()
                    .map_or(false, |c| c.is_ascii_uppercase());
                if starts_upper {
                    let mut bindings = Vec::new();
                    let mut end = id.span;
                    if self.at(LParen) {
                        self.bump();
                        while !self.at(RParen) && !self.at(Eof) {
                            if self.at(Underscore) {
                                bindings.push(ailang_syntax::ast::Ident {
                                    name: "_".to_string(),
                                    span: self.bump().span,
                                });
                            } else {
                                bindings.push(self.parse_ident()?);
                            }
                            if !self.eat(Comma) {
                                break;
                            }
                        }
                        end = self.expect(RParen)?.span;
                    }
                    let id_span = id.span;
                    Some(Pattern::Variant {
                        name: id,
                        bindings,
                        span: id_span.join(end),
                    })
                } else {
                    Some(Pattern::Binding(id))
                }
            }
            LParen => {
                let start = self.bump().span;
                let mut elems = Vec::new();
                while !self.at(RParen) && !self.at(Eof) {
                    elems.push(self.parse_pattern()?);
                    if !self.eat(Comma) {
                        break;
                    }
                }
                let end = self.expect(RParen)?.span;
                Some(Pattern::Tuple {
                    elems,
                    span: start.join(end),
                })
            }
            _ => {
                self.error(format!("expected pattern, got `{}`", tok.kind), tok.span);
                None
            }
        }
    }
}

fn pattern_span(p: &Pattern) -> Span {
    match p {
        Pattern::Wildcard(s)
        | Pattern::Tuple { span: s, .. }
        | Pattern::Variant { span: s, .. } => *s,
        Pattern::Literal(l) => l.span,
        Pattern::Binding(i) => i.span,
    }
}

fn stmt_span(s: &Stmt) -> Span {
    match s {
        Stmt::Decl { span, .. } | Stmt::Assign { span, .. } | Stmt::Return { span, .. } => *span,
        Stmt::Break(s) | Stmt::Continue(s) => *s,
        Stmt::If(i) => i.span,
        Stmt::Loop(l) => l.span,
        Stmt::Match(m) => m.span,
        Stmt::Expr(e) => e.span,
    }
}

// ============================================================================
// Types
// ============================================================================

impl Parser<'_> {
    pub(crate) fn parse_type(&mut self) -> Option<Type> {
        use TokenKind::*;
        let tok = self.peek();
        match tok.kind {
            LBracket => {
                let start = self.bump().span;
                let inner = self.parse_type()?;
                let end = self.expect(RBracket)?.span;
                Some(Type {
                    kind: TypeKind::Array(Box::new(inner)),
                    span: start.join(end),
                })
            }
            LBrace => {
                let start = self.bump().span;
                let k = self.parse_type()?;
                self.expect(Colon)?;
                let v = self.parse_type()?;
                let end = self.expect(RBrace)?.span;
                Some(Type {
                    kind: TypeKind::Map(Box::new(k), Box::new(v)),
                    span: start.join(end),
                })
            }
            Star => {
                let start = self.bump().span;
                let inner = self.parse_type()?;
                Some(Type {
                    span: start.join(inner.span),
                    kind: TypeKind::Ptr(Box::new(inner)),
                })
            }
            Question => {
                let start = self.bump().span;
                let inner = self.parse_type()?;
                Some(Type {
                    span: start.join(inner.span),
                    kind: TypeKind::Optional(Box::new(inner)),
                })
            }
            Bang => {
                let start = self.bump().span;
                let inner = self.parse_type()?;
                Some(Type {
                    span: start.join(inner.span),
                    kind: TypeKind::Result(Box::new(inner)),
                })
            }
            KwFn => {
                let start = self.bump().span;
                self.expect(LParen)?;
                let mut params = Vec::new();
                while !self.at(RParen) && !self.at(Eof) {
                    params.push(self.parse_type()?);
                    if !self.eat(Comma) {
                        break;
                    }
                }
                let close = self.expect(RParen)?.span;
                let mut end = close;
                let ret = if self.eat(Arrow) {
                    let r = self.parse_type()?;
                    end = r.span;
                    Some(Box::new(r))
                } else {
                    None
                };
                Some(Type {
                    kind: TypeKind::Fn { params, ret },
                    span: start.join(end),
                })
            }
            // Primitive type keywords + ident map to TypeKind::Path
            TyI8 | TyI16 | TyI32 | TyI64 | TyU8 | TyU16 | TyU32 | TyU64 | TyF32 | TyF64
            | TyBool | TyStr | Ident => {
                let span = self.bump().span;
                Some(Type {
                    kind: TypeKind::Path(self.ident_text(span)),
                    span,
                })
            }
            _ => {
                self.error(format!("expected type, got `{}`", tok.kind), tok.span);
                None
            }
        }
    }

    pub(crate) fn parse_ident(&mut self) -> Option<Ident> {
        let tok = self.peek();
        if tok.kind == TokenKind::Ident {
            self.bump();
            Some(Ident {
                name: self.ident_text(tok.span),
                span: tok.span,
            })
        } else {
            self.error(format!("expected identifier, got `{}`", tok.kind), tok.span);
            None
        }
    }
}

enum StmtOrTail {
    Stmt(Stmt),
    TailExpr(Expr),
}

// ============================================================================
// Tests
// ============================================================================

#[cfg(test)]
mod tests {
    use super::*;
    use ailang_lexer::lex;

    fn parse_str(src: &str) -> (Module, Vec<ParseError>) {
        let toks = lex(src);
        parse(src, &toks)
    }

    #[test]
    fn empty_module() {
        let (m, errs) = parse_str("");
        assert!(errs.is_empty());
        assert!(m.items.is_empty());
    }

    #[test]
    fn hello_fn() {
        let (m, errs) = parse_str("fn main() { println(\"hi\") }");
        assert!(errs.is_empty(), "errors: {:#?}", errs);
        assert_eq!(m.items.len(), 1);
        let Item::Fn(f) = &m.items[0] else {
            panic!("expected fn")
        };
        assert_eq!(f.name.name, "main");
        assert_eq!(f.params.len(), 0);
        assert!(f.return_ty.is_none());
    }

    #[test]
    fn fn_with_params_and_return() {
        let (m, errs) = parse_str("fn add(a:i64, b:i64) -> i64 { a + b }");
        assert!(errs.is_empty(), "errors: {:#?}", errs);
        let Item::Fn(f) = &m.items[0] else { panic!() };
        assert_eq!(f.params.len(), 2);
        assert!(f.return_ty.is_some());
        // tail expr is `a + b`
        assert!(f.body.tail_expr.is_some());
    }

    #[test]
    fn extern_with_variadic() {
        let (m, errs) = parse_str(r#"ex "c" fn printf(fmt:*u8, ...) -> i32"#);
        assert!(errs.is_empty(), "errors: {:#?}", errs);
        let Item::Extern(e) = &m.items[0] else {
            panic!()
        };
        assert!(e.sig.variadic);
        assert_eq!(e.sig.params.len(), 1);
    }

    #[test]
    fn struct_decl() {
        let (m, errs) = parse_str("st Point { x:f64; y:f64; }");
        assert!(errs.is_empty(), "errors: {:#?}", errs);
        let Item::Struct(s) = &m.items[0] else {
            panic!()
        };
        assert_eq!(s.fields.len(), 2);
    }

    #[test]
    fn for_loop_range() {
        let src = "fn main() { lp i in 1..10 { println(i) } }";
        let (_, errs) = parse_str(src);
        assert!(errs.is_empty(), "errors: {:#?}", errs);
    }

    #[test]
    fn while_loop() {
        let src = "fn main() { mu i := 0; lp i < 5 { i = i + 1 } }";
        let (_, errs) = parse_str(src);
        assert!(errs.is_empty(), "errors: {:#?}", errs);
    }

    #[test]
    fn fizzbuzz_parses_clean() {
        let src = include_str!("../../../examples/fizzbuzz.ail");
        let (m, errs) = parse_str(src);
        assert!(errs.is_empty(), "errors: {:#?}", errs);
        assert_eq!(m.items.len(), 1);
    }

    #[test]
    fn fib_parses_clean() {
        let src = include_str!("../../../examples/fib.ail");
        let (m, errs) = parse_str(src);
        assert!(errs.is_empty(), "errors: {:#?}", errs);
        assert_eq!(m.items.len(), 2); // fn fib + fn main
    }

    #[test]
    fn binary_precedence() {
        // `2 + 3 * 4` must parse as `2 + (3 * 4)`.
        let (m, errs) = parse_str("fn main() { 2 + 3 * 4 }");
        assert!(errs.is_empty());
        let Item::Fn(f) = &m.items[0] else { panic!() };
        let tail = f.body.tail_expr.as_ref().unwrap();
        let ExprKind::Binary { op, lhs, rhs } = &tail.kind else {
            panic!()
        };
        assert_eq!(*op, BinOp::Add);
        // lhs should be literal 2
        let ExprKind::Lit(_) = lhs.kind else {
            panic!("expected literal lhs")
        };
        // rhs should be `3 * 4`
        let ExprKind::Binary { op: r_op, .. } = &rhs.kind else {
            panic!()
        };
        assert_eq!(*r_op, BinOp::Mul);
    }
}

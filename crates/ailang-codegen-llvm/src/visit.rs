//! Generic AST traversal: a pre-order visit of every `Expr` node reachable
//! from a block / statement / expression, invoking a caller-supplied closure
//! on each. Both generic-instance discovery ([`crate::collect`]) and lambda
//! capture analysis are expressed as closures over this single walker, in
//! place of the three near-identical hand-rolled recursions they used before.
//!
//! Fidelity note: `if` / `match` in *expression* position are intentionally
//! NOT descended into, mirroring the original passes exactly. Statement-level
//! `if` / `loop` / `match` are traversed via [`walk_block`].
use ailang_syntax::ast::*;

/// Visit `e` and every descendant expression in pre-order, calling `f` on each.
pub(crate) fn walk_expr<F: FnMut(&Expr)>(e: &Expr, f: &mut F) {
    f(e);
    match &e.kind {
        ExprKind::Call { callee, args } => {
            walk_expr(callee, f);
            for a in args {
                walk_expr(a, f);
            }
        }
        ExprKind::Index { container, index } => {
            walk_expr(container, f);
            walk_expr(index, f);
        }
        ExprKind::Field { container, .. } => walk_expr(container, f),
        ExprKind::Binary { lhs, rhs, .. } => {
            walk_expr(lhs, f);
            walk_expr(rhs, f);
        }
        ExprKind::Unary { operand, .. } => walk_expr(operand, f),
        ExprKind::Ternary { cond, then_, else_ } => {
            walk_expr(cond, f);
            walk_expr(then_, f);
            walk_expr(else_, f);
        }
        ExprKind::Pipe { lhs, rhs } => {
            walk_expr(lhs, f);
            walk_expr(rhs, f);
        }
        ExprKind::Array(xs) | ExprKind::Tuple(xs) => {
            for x in xs {
                walk_expr(x, f);
            }
        }
        ExprKind::Map(es) => {
            for (k, v) in es {
                walk_expr(k, f);
                walk_expr(v, f);
            }
        }
        ExprKind::StructLit { fields, .. } => {
            for (_, v) in fields {
                walk_expr(v, f);
            }
        }
        ExprKind::Block(b) => walk_block(b, f),
        ExprKind::Try(inner) => walk_expr(inner, f),
        ExprKind::Lambda { body, .. } => match body {
            LambdaBody::Expr(inner) => walk_expr(inner, f),
            LambdaBody::Block(b) => walk_block(b, f),
        },
        _ => {}
    }
}

/// Visit every expression reachable from the statements (and tail expr) of `b`.
pub(crate) fn walk_block<F: FnMut(&Expr)>(b: &Block, f: &mut F) {
    for s in &b.stmts {
        walk_stmt(s, f);
    }
    if let Some(t) = &b.tail_expr {
        walk_expr(t, f);
    }
}

fn walk_stmt<F: FnMut(&Expr)>(s: &Stmt, f: &mut F) {
    match s {
        Stmt::Decl { value, .. } => walk_expr(value, f),
        Stmt::Assign { target, value, .. } => {
            walk_expr(target, f);
            walk_expr(value, f);
        }
        Stmt::Expr(e) => walk_expr(e, f),
        Stmt::Return { value: Some(e), .. } => walk_expr(e, f),
        Stmt::If(i) => walk_if(i, f),
        Stmt::Loop(l) => {
            match &l.head {
                Some(LoopHead::ForIn { iter, .. }) => walk_expr(iter, f),
                Some(LoopHead::While(c)) => walk_expr(c, f),
                None => {}
            }
            walk_block(&l.body, f);
        }
        Stmt::Match(m) => {
            walk_expr(&m.scrutinee, f);
            for arm in &m.arms {
                walk_expr(&arm.body, f);
            }
        }
        _ => {}
    }
}

fn walk_if<F: FnMut(&Expr)>(i: &IfStmt, f: &mut F) {
    walk_expr(&i.cond, f);
    walk_block(&i.then_branch, f);
    match &i.else_branch {
        Some(ElseBranch::Block(b)) => walk_block(b, f),
        Some(ElseBranch::If(inner)) => walk_if(inner, f),
        None => {}
    }
}

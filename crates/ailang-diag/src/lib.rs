//! Diagnostic reporting — thin wrapper over `ariadne` for AiLang errors.

use ailang_syntax::token::Span;
use ariadne::{Color, Label, Report, ReportKind, Source};

#[derive(Clone, Debug)]
pub struct Diagnostic {
    pub severity: Severity,
    pub message: String,
    pub primary: Span,
    pub help: Option<String>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Severity {
    Error,
    Warning,
    Note,
}

impl Diagnostic {
    pub fn error(message: impl Into<String>, primary: Span) -> Self {
        Self {
            severity: Severity::Error,
            message: message.into(),
            primary,
            help: None,
        }
    }
    pub fn warning(message: impl Into<String>, primary: Span) -> Self {
        Self {
            severity: Severity::Warning,
            message: message.into(),
            primary,
            help: None,
        }
    }
    pub fn with_help(mut self, help: impl Into<String>) -> Self {
        self.help = Some(help.into());
        self
    }
}

/// Render diagnostics to stderr with source carets and color.
pub fn report(file: &str, source: &str, diags: &[Diagnostic]) {
    for d in diags {
        let kind = match d.severity {
            Severity::Error => ReportKind::Error,
            Severity::Warning => ReportKind::Warning,
            Severity::Note => ReportKind::Advice,
        };
        let start = d.primary.start as usize;
        let end = d.primary.end as usize;
        let mut builder = Report::build(kind, file, start)
            .with_message(&d.message)
            .with_label(
                Label::new((file, start..end))
                    .with_message(&d.message)
                    .with_color(Color::Red),
            );
        if let Some(h) = &d.help {
            builder = builder.with_help(h);
        }
        let _ = builder.finish().eprint((file, Source::from(source)));
    }
}

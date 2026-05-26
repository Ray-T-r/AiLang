# Changelog

All notable changes are documented here. Loosely follows
[Keep a Changelog](https://keepachangelog.com/) and
[Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.1.0] — 2026-05-26

First public release. The compiler runs the full
lex → parse → sema → codegen → `clang -O2` pipeline; **42 automated tests
pass and 12 end-to-end example programs compile and run correctly**.

### Language

- Functions with implicit return of the trailing expression
- `if` / `el` with braceless single-statement bodies
- Unified `lp` loop — same keyword for `for-in` ranges and `while` conditions
- `mt` match with tuple patterns
- `|>` pipe operator
- Variable bindings via `mu? ident := expr`, type-inferred
- Compound assignment: `+=`, `-=`, `*=`, `/=`, `%=`
- Implicit `main` — top-level statements auto-wrap into one
- Optional type annotations (default `i64`)
- Unified `+` for arithmetic and string concatenation
- Arrays (`[i64]`, `[str]`) and maps (`{i64:i64}`) on Boehm GC
- Modules via `im "path"` with cycle-safe import resolution
- Direct FFI to libc via `ex fn …`
- Generics syntax (currently monomorphic over `i64`)

### Tooling

- `ailangc` CLI: `tokens`, `parse`, `compile`, `run`, plus `--emit-c` and
  experimental `--backend ir`
- Boehm GC auto-discovery via `$BDW_GC_PREFIX` → `pkg-config` → `brew`
- ariadne-powered colored diagnostics with caret pointers
- AiLang skill at [`skill/SKILL.md`](skill/SKILL.md) for Claude Code

### Distribution

- Pre-built binaries on the GitHub release page:
  `ailangc-macos-aarch64`, `ailangc-windows-x86_64.exe`
- One-line installers ([`install.sh`](install.sh) for macOS/Linux,
  [`install.ps1`](install.ps1) for Windows)
- GitHub Actions release workflow builds all four platforms (macOS arm64
  + x86_64, Linux x86_64, Windows x86_64) on `v*.*.*` tag push starting
  with the next release

[Unreleased]: https://github.com/Ray-T-r/AiLang/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/Ray-T-r/AiLang/releases/tag/v0.1.0

# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## What this is

**AiLang** is a compiled language whose source syntax is optimized for LLM token efficiency. The compiler is written in Rust, lowers `.ail` source through a multi-crate pipeline, and produces native binaries by handing generated C99 to `clang -O2`. End-user language docs live in [`README.md`](README.md); this file covers compiler-developer concerns.

## Toolchain & build

- Rust **stable** (pinned in [`rust-toolchain.toml`](rust-toolchain.toml)) with `rustfmt`/`clippy`.
- `clang` (or `$CC`) on PATH for the codegen step.
- Boehm GC (`bdw-gc`) for the C backend's runtime. The driver auto-discovers it via `$BDW_GC_PREFIX` → `pkg-config --variable=prefix bdw-gc` → `brew --prefix bdw-gc`. Install on macOS: `brew install bdw-gc pkg-config`.
- [`.cargo/config.toml`](.cargo/config.toml) replaces crates.io with a Tsinghua mirror — useful for users in China, harmless otherwise.

```bash
cargo build --release            # produces ./target/release/ailangc
cargo test                       # workspace tests, includes e2e
cargo test -p ailang-driver e2e_fib        # single e2e case
cargo test -p ailang-lexer                 # lexer snapshot tests (insta)
cargo insta review               # accept snapshot diffs after lexer changes
./target/release/ailangc run examples/fib.ail
./target/release/ailangc compile examples/fib.ail --emit-c     # dump generated C
./target/release/ailangc compile examples/fib.ail --backend ir # alternate backend
```

## Release / install flow

End users install via `install.sh` (macOS/Linux) or `install.ps1` (Windows), both at the repo root and uploaded to each GitHub Release. Those scripts **download** the pre-built binary and `skill/SKILL.md` from the latest Release — they do not build from source. The release-asset naming convention is:

```
ailangc-macos-aarch64
ailangc-macos-x86_64
ailangc-linux-x86_64
ailangc-windows-x86_64.exe
SKILL.md
install.sh
install.ps1
```

To stage a release: on each target platform, run `scripts/package-release.sh` (builds the host's binary and copies it + the scripts + SKILL.md into `./dist/`). Collect every host's `dist/` contents and upload them as assets on one GitHub Release.

If you edit `skill/SKILL.md` locally, copy it manually to `~/.Codex/skills/ailang/SKILL.md` to refresh the running Codex; users get the update via the next Release.

E2E tests in [`crates/ailang-driver/tests/e2e.rs`](crates/ailang-driver/tests/e2e.rs) early-return when their `examples/*.ail` source is missing, so deleting an example doesn't break the suite — keep the `project_has(...)` guards when adding new cases. Each case compiles via the driver API (not the CLI), runs the binary, and diffs stdout against [`tests/e2e/<name>.out`](tests/e2e/).

Generated `.c`, `.ll`, and binaries land **next to** the source `.ail`. They're gitignored under `/examples/` but can pile up elsewhere — the bench dirs in particular accumulate them.

## Pipeline & crate layout

```
source.ail
  → ailang-lexer (logos)              tokens
  → ailang-parser (RD + Pratt)        ast::Module
  → ailang-sema                       ResolvedModule (+ fn_table, expr_types side tables)
  → ailang-codegen-llvm   (default)   C99 string
    or ailang-codegen-ir  (--backend ir)  LLVM IR text
  → ailang-driver invokes clang -O2 [-lgc]
  → native binary
```

Crates (all under [`crates/`](crates/)):

- **`ailang-syntax`** — pure data: `Token`, `TokenKind`, `Span`, and the AST (`Module`, `Item`, `Expr`, …). No logic. Both the lexer and parser depend on this.
- **`ailang-lexer`** — `logos`-driven; whitespace and comments are skipped before the parser sees them. Snapshot-tested against fixtures in `examples/`.
- **`ailang-parser`** — recursive-descent for items + Pratt for expressions ([`expr.rs`](crates/ailang-parser/src/expr.rs)). Error-recovering: parses everything it can, returns `(Module, Vec<ParseError>)`.
- **`ailang-diag`** — thin wrapper around `ariadne` for colored caret diagnostics.
- **`ailang-sema`** — name resolution, signature collection, **permissive** type tagging. Unknown types collapse to `Ty::Unknown`, which codegen renders as `int64_t`. Populates two side tables (`fn_table`, `expr_types`) on the AST rather than lowering to a new IR. Two-phase: collect sigs, then revisit unannotated return types once all sigs are known (so `println` is identified as void before bodies are checked).
- **`ailang-codegen-llvm`** — **misnamed; actually emits C99.** Default backend. Exposes `emit_c(&ResolvedModule) -> String`. The crate name is historical (planned to be inkwell-based) and not worth churning yet.
- **`ailang-codegen-ir`** — the real LLVM IR backend (textual `.ll`, not `inkwell` — see the module-doc rationale). Initial drop covers only `println(literal)` + implicit main; everything else stubs out as TODO comments. Selectable via `--backend ir`.
- **`ailang-driver`** — pipeline orchestration, import resolution, clang invocation, GC discovery. Exports `compile`, `run`, `dump_tokens`, `dump_ast`.
- **`ailang-hir`** — placeholder (empty `lib.rs`). Reserved for a future desugared IR; nothing routes through it yet.
- **`ailang-runtime`** — placeholder no_std staticlib. The actual runtime ships inline as a C prelude inside `ailang-codegen-llvm`.
- **`ailangc`** — clap-based CLI binary; thin shell over `ailang-driver`.

## Things that aren't obvious from the file tree

- **Two codegen backends, with confusingly named crates.** Default is `ailang-codegen-llvm` (which emits C). The `--backend ir` flag selects `ailang-codegen-ir` (which actually emits LLVM IR text). When working on codegen, double-check which crate you're editing — it's easy to fix something in the LLVM-named crate and have nothing change because you're producing C.
- **No HIR layer today.** Sema annotates the AST in place via side tables. Don't add an HIR pass without buy-in — `ailang-hir` is intentionally empty until the design lands.
- **Import resolution is the driver's job, not the parser's.** [`parse_with_imports`](crates/ailang-driver/src/lib.rs) walks `Item::Import` nodes, re-parses each file, drops their `fn main`, and concatenates everything into a single `Module` before sema runs. Cycles are blocked by a canonical-path visited set. Cross-file diagnostics still point at the root file's source — a known limitation.
- **Sema is deliberately permissive.** A type mismatch is a warning, not an error, and unknown types become `int64_t` at codegen. The strict checker is gated on an HIR that doesn't exist yet — don't tighten sema in isolation.
- **The runtime is a C string baked into the codegen crate** (the `PRELUDE` constant in [`ailang-codegen-llvm/src/lib.rs`](crates/ailang-codegen-llvm/src/lib.rs)). Runtime helpers for arrays/maps/strings live there. To add a new array element type or map (K,V) combo, you register a typedef, three helpers, and a `_Generic` arm — same template the existing `[i64]` / `[str]` / `{i64:i64}` use.
- **`print`/`println` dispatch is C11 `_Generic`-based** at compile time, not real overloading in the type system.
- **Milestone markers in comments (M0–M6+)** correspond to the status table in the README. Treat `/* not supported in M2 */` etc. as a real signal — that branch was deliberately stubbed and may need wiring up before you can use it.
- **`std/` is duplicated at `examples/std/`** so in-tree e2e examples can `im "std/..."` against a path that exists relative to the example file. Keep both copies in sync if you edit one.
- **`bench/`** has two parallel layouts: token-count benchmarks at the top level (run `python3 bench/count_all.py`, needs `tiktoken`) and runtime benchmarks under `bench/perf/` (run `hyperfine` against the compiled binaries).

# Contributing to AiLang

Thanks for taking a look. The project is in early development — bug
reports, fixes, and design discussions are all welcome.

## Setup

```bash
# macOS
brew install rust pkg-config bdw-gc

# Debian / Ubuntu
sudo apt-get install -y clang libgc-dev pkg-config rustc cargo

git clone https://github.com/Ray-T-r/AiLang
cd AiLang
cargo build --release
cargo test
```

You should see **42 tests passing**. End-to-end tests invoke `clang` at
runtime and link Boehm GC, so make sure both are on `PATH` before
running the suite.

## Workspace layout

| Crate                 | Role                                              |
|-----------------------|---------------------------------------------------|
| `ailang-syntax`       | Token + AST + Span (pure data)                    |
| `ailang-lexer`        | source → tokens (logos)                           |
| `ailang-parser`       | tokens → AST (RD + Pratt)                         |
| `ailang-diag`         | ariadne wrapper for colored diagnostics           |
| `ailang-sema`         | name resolution + arity + permissive type tagging |
| `ailang-codegen-llvm` | AST → C99 transpiler (name is historical)         |
| `ailang-codegen-ir`   | experimental AST → LLVM IR text backend           |
| `ailang-driver`       | pipeline orchestration + linker                   |
| `ailang-hir`          | placeholder for desugared IR (M6+)                |
| `ailang-runtime`      | placeholder no_std staticlib (M6+)                |
| `ailangc`             | CLI binary                                        |

## Running tests

```bash
cargo test                              # whole workspace
cargo test -p ailang-driver e2e_fib     # single e2e case
cargo test -p ailang-lexer              # lexer snapshot tests
cargo insta review                      # accept snapshot diffs after lexer changes
```

End-to-end fixtures live in [`tests/e2e/`](tests/e2e/) — one `.out`
expected-stdout file per example program (the example itself sits under
[`examples/`](examples/)).

## CI

Every push to `main` and every pull request triggers
[`ci.yml`](.github/workflows/ci.yml), which runs `cargo build --release`
and `cargo test --release` on **macOS and Ubuntu**. Please make sure
tests pass locally before opening a PR — the same matrix runs on the PR
itself.

## Releases

Tagging `vX.Y.Z` on `main` triggers
[`release.yml`](.github/workflows/release.yml), which cross-builds
binaries for macOS arm64/x86_64, Linux x86_64, and Windows x86_64 in
parallel, then uploads them to the GitHub release alongside the
installer scripts and `SKILL.md`. No manual step required after the tag.

## Commit messages

No strict convention. Use a short imperative subject (`add …`,
`fix …`, `bump …`). Reference issues with `#N`. Wrap body at ~80
columns.

## Where to file things

- Bugs and feature requests → [GitHub issues](https://github.com/Ray-T-r/AiLang/issues)
- Performance numbers or regressions → include the benchmark source and
  the exact `hyperfine` command line so the result can be reproduced
- Anything that touches the syntax or token-saving choices → please read
  [`spec/ablation-plan.md`](spec/ablation-plan.md) first and frame the
  proposal against that methodology

## What's in scope right now

The roadmap is the milestone table in the [README](README.md#status).
M0–M5+ are done; M4+ (arrays/maps polish), M6 (generics + stdlib seed),
and M6+ (LLVM IR backend) are the live workstreams. Patches in those
areas land fast; patches that fight the language's token-minimization
goal will need a stronger argument.

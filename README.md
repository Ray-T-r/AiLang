# AiLang — the self-hosted compiler

The AiLang compiler **written in AiLang itself**: a 4,769-line program
(`selfhost/main.ail`) that lexes, parses, type-tracks, and lowers `.ail`
source to C, then drives `clang` to a native binary — the whole pipeline
authored in `.ail`.

It is **self-hosting at a strict fixpoint**: compiling its own source produces
a byte-identical compiler (`stage2.c == stage3.c`, 6,841 lines), with **no Rust
toolchain anywhere in the loop**.

> The original Rust implementation (`ailangc`) lives in a sibling repo,
> **AiLang_Rust**. It's still the reference oracle and the full-featured
> production compiler (semantic diagnostics, the complete language + driver).
> This repo is the self-hosted line, split off once it reached byte-for-byte
> parity with `ailangc` across the example corpus.

## What it compiles

The core language plus the full standard library: functions and recursion,
control flow, structs, `en` enums + `mt` match (recursive ADTs, heap-boxed
self-references), real generics, closures with capture, `[T]` arrays and
`{K:V}` maps (open-addressing, hash-ordered like the reference), tuples +
multi-return, `!T` / `?` error propagation, floats, bytes, string
interpolation `"${e}"`, pointers, UFCS (`x.f(a)`), `cinc` C-header interop,
variadic externs (`ex fn printf(fmt, ...)`), and the 10 `std/*` modules —
sockets, HTTP, TLS, Postgres, Redis, WebSocket, JSON, time, str, math — pulled
in via `im`.

## Status

| | |
|---|---|
| `selfhost/main.ail` | 4,769 lines |
| strict fixpoint | `stage2.c == stage3.c` — **6,841 lines, byte-identical** |
| sample programs | **32**, each output-verified against a frozen fixture |
| standard library | 10 modules, all compiling |
| Rust in the build | **none** |

## Layout

```
selfhost/
  main.ail        the compiler: lexer + parser + type tracking + C codegen + clang driver
  lexer.ail       standalone token-dump harness (illustrative)
  parser.ail      standalone tree-eval harness (illustrative)
  seed/ailc.c     the bootstrap seed — main.ail self-compiled to C (the fixpoint snapshot)
  bootstrap.sh    rebuild the compiler from the seed, with no Rust
  verify.sh       prove correctness: sample fidelity + strict fixpoint
std/              the standard library (.ail modules the compiler imports via `im`)
examples-selfhost/
  *.ail           sample programs the compiler builds
  expected/*.out  frozen known-good output (byte-equal to Rust `ailangc` at split time)
```

## Build it — from nothing but C

No Rust, no prior AiLang binary required — just `clang` and Boehm GC:

```bash
bash selfhost/bootstrap.sh          # seed.c → ./selfhost/ailc, verifies the fixpoint
./selfhost/ailc prog.ail prog       # CLI is: ailc <input.ail> <output-binary>
./prog
```

`bootstrap.sh` compiles the checked-in C seed into a working `ailc`, uses it to
recompile `main.ail` from source, and checks the result reproduces the seed
byte-for-byte — the self-hosting fixpoint, established without any Rust.

## Verify

```bash
bash selfhost/verify.sh
```

Two self-contained proofs, no external reference compiler consulted:

- **Sample fidelity** — every `examples-selfhost/*.ail` builds to output matching
  its committed `expected/*.out` fixture (frozen known-good, byte-equal to the
  Rust `ailangc` when the repos were split).
- **Strict fixpoint** — `ailc(main.ail)` reproduces `seed/ailc.c` exactly.

## Requirements

`clang` (or `$CC`), Boehm GC (`bdw-gc`), and — because the runtime prelude
currently links them unconditionally — OpenSSL and libpq.

```bash
# macOS
brew install bdw-gc openssl libpq pkg-config
# Debian / Ubuntu
sudo apt-get install -y clang libgc-dev libssl-dev libpq-dev pkg-config
```

## Updating the seed

The seed is regenerated whenever `main.ail` changes — otherwise `bootstrap.sh`'s
fixpoint check reports a mismatch. See
[`selfhost/seed/README.md`](selfhost/seed/README.md) for the one-liner.

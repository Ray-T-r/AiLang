# AiLang — the self-hosted compiler

This is the AiLang compiler **written in AiLang itself**. It lexes, parses,
type-tracks, and lowers `.ail` source to C, then hands the C to `clang` —
the whole pipeline authored in `.ail`, compiling (a large subset of) the
language including itself.

It is **self-hosting at a strict fixpoint**: the compiler compiles its own
source to a byte-identical compiler, with no Rust toolchain in the loop.

> The original Rust implementation of AiLang (`ailangc`) lives in a separate
> repo, **AiLang_Rust**. It remains the reference oracle and the full-featured
> production compiler (semantic diagnostics, the complete language). This repo
> is the self-hosted line; the two were split once the self-host reached parity
> on the example corpus.

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
  expected/*.out  frozen known-good output (== Rust ailangc at split time)
```

## Build it — from nothing but C

No Rust, no prior AiLang binary required — just `clang` and Boehm GC:

```bash
bash selfhost/bootstrap.sh          # seed.c → ./selfhost/ailc, verifies the fixpoint
./selfhost/ailc prog.ail prog       # compile an AiLang program to a native binary
./prog
```

`bootstrap.sh` compiles the checked-in C seed into a working `ailc`, uses it to
recompile `main.ail` from source, and checks the result reproduces the seed
byte-for-byte.

## Verify

```bash
bash selfhost/verify.sh
```

Two self-contained proofs: every `examples-selfhost/*.ail` builds to output
matching its committed fixture, **and** `ailc(main.ail)` reproduces the seed
(strict fixpoint). No external reference compiler is consulted.

## Requirements

`clang` (or `$CC`), Boehm GC (`bdw-gc`), and — because the runtime prelude
currently pulls them in — OpenSSL and libpq.

```bash
# macOS
brew install bdw-gc openssl libpq pkg-config
# Debian/Ubuntu
sudo apt-get install -y clang libgc-dev libssl-dev libpq-dev pkg-config
```

## Updating the seed

The seed is regenerated whenever `main.ail` changes (otherwise the fixpoint
check reports a mismatch) — see [`selfhost/seed/README.md`](selfhost/seed/README.md).

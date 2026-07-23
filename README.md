# AiLang — the self-hosted compiler

The AiLang compiler **written in AiLang itself**: a ~7,700-line compiler
(`selfhost/main.ail` + the `selfhost/src/*.ail` modules) that lexes, parses,
type-checks, and lowers `.ail` source to C, then drives `clang` to a native
binary — the whole pipeline authored in `.ail`.

It is **self-hosting at a strict fixpoint**: compiling its own source produces
a byte-identical compiler (`stage2.c == stage3.c`, 11,588 lines), with **no Rust
toolchain anywhere in the loop**.

> The original Rust implementation (`ailangc`) lives in a sibling repo,
> **AiLang_Rust**. It's still the reference oracle and the full-featured
> production compiler (semantic diagnostics, the complete language + driver).
> This repo is the self-hosted line, split off once it reached byte-for-byte
> parity with `ailangc` across the example corpus.

## What it compiles

The core language plus the full standard library: functions and recursion,
control flow, structs, classes (`cl` — single inheritance + `vt` virtual
methods, lowered to C vtables), `en` enums + `mt` match (recursive ADTs,
heap-boxed self-references; guards `if`, `_` catch-all, and nested
destructuring), real generics — generic functions and generic data types
(`st Box<T>`, `en Option<T>` / `Result<T>`) monomorphized per use into real C
functions, so a generic fn may take a closure parameter
(`fn map2<T,U>(xs:[T], f:fn(T)->U)`); multi-parameter generics (`<A,B>`), and
`tr` traits + `<T: Trait>` constrained generics (compile-time-checked) — closures
with capture, `[T]` arrays
and `{K:V}` maps (open-addressing, hash-ordered like the reference), tuples +
multi-return, `!T` / `?` error propagation, floats, bytes, string
interpolation `"${e}"`, pointers, UFCS (`x.f(a)`), operator overloading
(`a + b` → `a.add(b)` when a class defines the method), `cinc` C-header interop,
C++ library interop (`csrc` + an `extern "C"` shim — inline in the `.ail` or an
external `.cpp`, POSIX), variadic externs
(`ex fn printf(fmt, ...)`), OS-thread concurrency (pthread-backed
`thread_spawn`/`thread_join`, `mutex_*`, and bounded blocking `chan_*` channels,
POSIX), `{str:[T]}` map-of-array values (so `group_by` returns real buckets),
and the 25 `std/*` modules — sockets, HTTP (server **and** client: `http_get`/
`http_post` with chunked decoding, https via SNI), TLS, the database trio
(Postgres, MySQL via libmysqlclient, SQLite via libsqlite3 — the native libs
linked only by programs that use them), Redis, WebSocket, filesystem (`fs_*`
ops + path helpers), JSON (flat + nested), CSV, time, str, math, threads, `seq`
(composable `any`/`keep`/`map_to`/`fold`/`sort_by`/`flat_map`/`zip_with`/
`group_by` combinators), `web` (Express-style routing: `:id` params, middleware,
`req_header`, handler closures held in the routes table), `jwt` (HS256
sign/verify, real interoperable tokens), and the app-building set `encoding`
(base64/base64url/hex), `url` (percent-encoding + query strings), `uuid` (v4),
`args` (CLI flag/option parsing), `log` (leveled logfmt), and `env` (typed
env + `.env`) — pulled in via `im` (with
optional `im "path" as m` aliasing for a namespaced, collision-free `m.fn()`).
Whole-stream stdin (`read_stdin`) plus the CSV reader/writer and the recursive JSON
parser/serializer make read→transform→write pipelines —
`csv_parse(data) |> keep(…) |> map_to(…)`, then `json_str(…)` — a few tokens. And a
full backend reads like Express: `web_get(&app, "/users/:id", fn(r:Req) …)` over
HTTP+sockets, `jwt_verify(tok, secret)` middleware, Postgres/MySQL for storage
(networking is POSIX-only — mac/Linux, WSL on Windows).

## Benchmarks

What's load-bearing here is the **self-hosting fixpoint** above and the **tight
agent feedback loop** below (`ailc check`/`run`/`test` + `--json` diagnostics).
Two other properties are real and measured — native speed and terse source — but
neither is the pitch, and the second is more modest than it first looks.

**Runtime** — recursive `fib(40)`, `hyperfine` mean on Apple Silicon, each
language at its standard optimization (`clang -O2`, `rustc -O`, `go build`,
`javac`, `node`, `python3`). Re-measured on this machine:

| language | time | vs AiLang |
|---|---|---|
| **AiLang** (→ C, `clang -O2`) | **156.8 ms** | 1.00× |
| Rust | 155.8 ms | 0.99× |
| C | 151.0 ms | 0.96× |
| Java | 171.7 ms | 1.10× |
| Go | 227.1 ms | 1.45× |
| Node | 495.7 ms | 3.16× |
| Python | 6756 ms | 43× |

AiLang runs neck-and-neck with C and Rust — it *is* C, generated. The
self-hosted `ailc` produces identically fast binaries (`ailc fib40.ail` → 156.8 ms,
matching `clang -O2`).

**Source density** — source tokens (`tiktoken`, `cl100k_base`) for four tiny
programs (`fib`, `fizzbuzz`, `greet`, `sum`):

| | AiLang | Python | JS | Rust | Go | Java | C |
|---|---|---|---|---|---|---|---|
| tokens | **130** | 136 | 167 | 186 | 223 | 261 | 270 |
| vs AiLang | 1.00× | 1.05× | 1.28× | 1.43× | 1.72× | 2.01× | 2.08× |

AiLang is the densest of the seven — but the margin over Python is **1.05×** on a
four-program micro-corpus, which is within noise, not a reason to switch. And
whether terse syntax actually saves an *LLM* tokens is a different, harder
question: only ~1,400 lines of AiLang exist anywhere, so the language is
out-of-distribution for every model, and the resulting check→fix retries can cost
more than the characters saved. That's the honest open question —
[`study/`](study/) measures it on real tasks against a matched Python baseline,
and reports it whichever way it falls.

## Status

| | |
|---|---|
| compiler source | ~8,000 lines across `main.ail` + 6 `src/` modules |
| strict fixpoint | `stage2.c == stage3.c` — **11,832 lines, byte-identical** |
| sample programs | **73**, each output-verified against a frozen fixture — including loopback runtime tests for sockets/HTTP/WebSocket/web (in-process server thread, deterministic output) |
| standard library | 25 modules — networked/db modules exercised by loopback samples + compile-and-link checks (`selfhost/tests/compileonly/`) |
| CLI | `ailc run` (compile + execute) · `ailc check` (type-check only) · `ailc test` (run `assert`-based `*_test.ail`, PASS/FAIL summary) · `ailc lib` (compile to a `.dylib`/`.so` shared library + C header) · `--json` machine-readable diagnostics with stable `AIL####` codes |
| concurrency | OS threads + mutex + bounded channels (pthread, POSIX); `spawn`/`wait`/`channel` via `im "std/thread.ail"` |
| type checking | conservative — confident mismatches at the `.ail` `line:col`: types & `!T` results, `mt` exhaustiveness (guard-aware)/variants/bindings/nesting, call/callback/generic arity, and `<T: Trait>` bound satisfaction. Reports **every** error in one run (not just the first) and suggests the nearest name on a misspelled variant/field/method (*"did you mean …?"*). Exercised by **44 negative tests**, all caught |
| Rust in the build | **none** |

## Layout

```
selfhost/
  main.ail        driver entry — `im`s the modules below, then runs the pipeline
  src/
    lexer.ail     token kinds + lexer
    ast.ail       AST node types + parser state + diagnostics
    parser.ail    recursive-descent + Pratt parser
    types.ail     static type tracking
    codegen.ail   C code generation
    driver.ail    import resolution + compile_to_c + clang invocation
  lexer.ail       standalone token-dump harness (illustrative)
  parser.ail      standalone tree-eval harness (illustrative)
  seed/ailc.c     the bootstrap seed — main.ail self-compiled to C (the fixpoint snapshot)
  bootstrap.sh    rebuild the compiler from the seed, with no Rust
  verify.sh       prove correctness: sample fidelity + compile-only stubs + negative tests + CLI guards + strict fixpoint
  tests/
    neg/          negative tests (each must FAIL to compile with the expected message)
    compileonly/  std-module stubs that must compile AND link, never executed (mysql/pg/redis/tls)
std/              the standard library (.ail modules the compiler imports via `im`)
examples-selfhost/
  *.ail           sample programs the compiler builds
  lib/            helper modules imported by samples (no fixtures — a fixture-less root sample FAILS verify)
  expected/*.out  frozen known-good output (byte-equal to Rust `ailangc` at split time)
```

## Install

Pre-built binaries are on the [Releases page](https://github.com/Ray-T-r/AiLang/releases).
Pick your platform below.

### macOS / Linux

**1. Run the installer** (in Terminal):

```bash
curl -fsSL https://github.com/Ray-T-r/AiLang/releases/latest/download/install.sh | bash
```

It downloads `ailc` to `~/.local/bin`, adds that to your `PATH`,
auto-installs the native toolchain it needs (`clang`, `bdw-gc`, OpenSSL,
`libpq` — via Homebrew / apt / dnf / pacman), and installs the AiLang skill
for [Claude Code](https://claude.com/claude-code). Add `--no-deps` to skip
the dependency step.

**2. Open a new terminal** (so the `PATH` change takes effect), then:

```bash
echo 'println("hello from ailang")' > /tmp/hi.ail
ailc /tmp/hi.ail /tmp/hi && /tmp/hi          # → hello from ailang
```

### Installing by hand (or when GitHub is unreachable)

The installer auto-installs the native deps; if you install manually — or the
one-liner hangs because GitHub is blocked — do it yourself. The macOS `ailc`
**dynamically links** Boehm GC, OpenSSL, and libpq (from `/opt/homebrew`), so
install those **first** or the binary won't start:

```bash
brew install bdw-gc openssl@3 libpq pkg-config       # macOS — required at runtime
```

Then grab the binary + std library, optionally through a GitHub mirror (set
`M=` empty to hit github.com directly):

```bash
M=https://ghfast.top/     # GitHub mirror prefix — mirrors come and go, try another if it fails
mkdir -p ~/.local/bin ~/.local/share/ailang
curl -fL "${M}https://github.com/Ray-T-r/AiLang/releases/latest/download/ailc-macos-aarch64.tar.gz" | tar -xz -C ~/.local/bin
curl -fL "${M}https://github.com/Ray-T-r/AiLang/releases/latest/download/ailc-std.tar.gz"          | tar -xz -C ~/.local/share/ailang
echo 'export PATH="$HOME/.local/bin:$PATH"'          >> ~/.zshrc
echo 'export AILANG_STD="$HOME/.local/share/ailang"' >> ~/.zshrc
```

(Linux: install the deps with your package manager — see [Requirements](#requirements) —
and use the `ailc-linux-x86_64.tar.gz` asset.)

### Windows

`ailc` runs **natively** on Windows and builds **self-contained `.exe`** files —
no WSL, no reboot. (`ailc` emits C and compiles it with `clang`, so it needs the
small MSYS2 **mingw64** toolchain — `clang` + Boehm GC — which the installer sets
up for you.)

**1. Run the installer in PowerShell:**

```powershell
iwr -useb https://github.com/Ray-T-r/AiLang/releases/latest/download/install.ps1 | iex
```

It installs the mingw64 toolchain (via `winget install MSYS2.MSYS2` if it's not
already there), downloads `ailc.exe`, adds it + `mingw64\bin` to your `PATH`,
and installs the AiLang skill for
[Claude Code](https://claude.com/claude-code).

**2. Open a new terminal** (so `PATH` refreshes), then:

```
echo 'println("hello")' > hi.ail
ailc hi.ail hi          # build a native, self-contained hi.exe
.\hi.exe                # → hello
```

Just `ailc` — same as macOS/Linux. It produces a **native, self-contained `.exe`**
(no WSL, no extra DLLs); compiling needs the mingw64 `clang` + `gc` installed above.

The produced `.exe` depends only on Windows' own `KERNEL32` + `msvcrt` — copy it
anywhere and it runs. `ailc` itself needs `clang` + Boehm GC on `PATH` to compile
(that's the mingw64 toolchain the installer set up); if `clang` isn't installed
yet:

```powershell
winget install MSYS2.MSYS2
# then, in the "MSYS2 MINGW64" shell:
pacman -S --needed mingw-w64-x86_64-clang mingw-w64-x86_64-gc
```

> **Networking & regex → WSL.** The compiler runs natively, but programs that use
> the POSIX-only stdlib (sockets, HTTP, TLS, Postgres, Redis, regex) can't be
> built into a native `.exe` yet — their runtime needs BSD sockets / `fork()` /
> POSIX regex. Build and run those inside **WSL** (`bash install.sh` there gives
> you the full language). Everything else is native.

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

## Call it from C — shared libraries

`ailc lib` compiles an `.ail` file to a native shared library — `.dylib` on
macOS, `.so` on Linux, `.dll` (plus a `.dll.a` import library) on Windows —
along with a matching C header, so C/C++/Rust/Python-ctypes — anything that
speaks the C ABI — can call AiLang functions:

```bash
ailc lib mathlib.ail            # → mathlib.dylib / .so / .dll  +  mathlib.h
```

Every top-level `fn` in the file whose signature uses only C-friendly types
(`i64`/`i32`/`u32`/`bool`/`f64`/`str`/`bytes`) is exported under its **plain
name** (`i64`→`int64_t`, `f64`→`double`, `str`→`const char*`); functions taking
or returning aggregates (arrays/maps/structs) are skipped with a note, since
those use AiLang's internal runtime layout. Boehm GC initialises itself when the
library loads (a load-time constructor), so the host calls no init routine:

```c
#include "mathlib.h"
int main(void){ return (int)ail_add(2, 3); }   // clang app.c mathlib.dylib -o app
```

Only the exported wrappers land in the dynamic symbol table — the library is
built `-fvisibility=hidden` (POSIX) / with `__declspec(dllexport)` (Windows), so
internal functions and the runtime stay private. On Windows link against the
generated `mathlib.dll.a` import library.

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
brew install bdw-gc openssl@3 libpq pkg-config
# Debian / Ubuntu
sudo apt-get install -y clang libgc-dev libssl-dev libpq-dev pkg-config
```

## Updating the seed

The seed is regenerated whenever `main.ail` changes — otherwise `bootstrap.sh`'s
fixpoint check reports a mismatch. See
[`selfhost/seed/README.md`](selfhost/seed/README.md) for the one-liner.

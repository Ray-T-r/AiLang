# AiLang — the self-hosted compiler

The AiLang compiler **written in AiLang itself**: a ~4,700-line compiler
(`selfhost/main.ail` + the `selfhost/src/*.ail` modules) that lexes, parses,
type-tracks, and lowers `.ail` source to C, then drives `clang` to a native
binary — the whole pipeline authored in `.ail`.

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

## Benchmarks

AiLang's bet: **the fewest source tokens** (cheap for an LLM to write) at
**native speed** (it lowers to C, built with `clang -O2`). Both, measured.

**Token efficiency** — source tokens (`tiktoken`, `cl100k_base`) for the same
four programs (`fib`, `fizzbuzz`, `greet`, `sum`):

| | AiLang | Python | JS | Rust | Go | Java | C |
|---|---|---|---|---|---|---|---|
| tokens | **130** | 136 | 167 | 186 | 223 | 261 | 270 |
| vs AiLang | 1.00× | 1.05× | 1.28× | 1.43× | 1.72× | 2.01× | 2.08× |

The densest of the seven — fewer tokens than Python, under half of C.

**Runtime** — recursive `fib(40)`, `hyperfine` mean on Apple Silicon, each
language at its standard optimization (`clang -O2`, `rustc -O`, `go build`,
`javac`, `node`, `python3`):

| language | time | vs AiLang |
|---|---|---|
| **AiLang** (→ C, `clang -O2`) | **142 ms** | 1.00× |
| C | 142 ms | 1.00× |
| Rust | 143 ms | 1.00× |
| Java | 159 ms | 1.12× |
| Go | 219 ms | 1.54× |
| Node | 479 ms | 3.37× |
| Python | 5394 ms | 38× |

AiLang runs neck-and-neck with C and Rust — it *is* C, generated. The
self-hosted `ailc` produces identically fast binaries (`ailc fib40.ail` → 145 ms,
matching `clang -O2`).

## Status

| | |
|---|---|
| compiler source | ~4,800 lines across `main.ail` + 6 `src/` modules |
| strict fixpoint | `stage2.c == stage3.c` — **6,912 lines, byte-identical** |
| sample programs | **32**, each output-verified against a frozen fixture |
| standard library | 10 modules, all compiling |
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
  verify.sh       prove correctness: sample fidelity + strict fixpoint
std/              the standard library (.ail modules the compiler imports via `im`)
examples-selfhost/
  *.ail           sample programs the compiler builds
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

### Windows

AiLang's runtime is POSIX-only, so on Windows it runs inside **WSL2** (Ubuntu).
The installer sets that up for you and adds `ailrun` / `ailexe` / `ailc`
commands to your Windows `PATH`, so you never type `wsl` yourself.

**1. Run the installer in PowerShell:**

```powershell
iwr -useb https://github.com/Ray-T-r/AiLang/releases/latest/download/install.ps1 | iex
```

**2. First time only — if WSL isn't installed yet,** the script needs to
install it (this part requires admin + a reboot):

  1. A **UAC prompt** appears → click **Yes**. An elevated window runs
     `wsl --install -d Ubuntu`.
  2. **Reboot** if Windows asks you to.
  3. Launch **Ubuntu** once from the Start menu and create a Linux
     **username + password** (remember the password — it's used for
     installing dependencies).
  4. **Re-run the same `iwr … | iex` line** (a normal PowerShell window is
     fine now). It finishes installing AiLang inside WSL.

  *(If WSL is already set up, step 1 installs everything in one go — skip
  step 2.)*

**3. Open a new terminal** (so `PATH` refreshes), then use AiLang like a
native tool — from any cmd or PowerShell window:

```
echo 'println("hello")' > hi.ail
ailrun hi.ail            # compile + run, output right here
ailexe hi.ail hi.exe     # build a native, self-contained Windows .exe
ailc   hi.ail prog       # Linux binary (run it with:  wsl ./prog)
```

| command  | what you get | needs |
|----------|--------------|-------|
| `ailrun` | compile **and run**, output in your terminal | WSL only (installed above) |
| `ailexe` | a **native `.exe`** — self-contained, runs with no WSL | WSL **+** MSYS2 mingw64 (`clang` + `gc`) |
| `ailc`   | a Linux binary (run via `wsl ./prog`) | WSL only |

`ailexe` compiles the generated C with mingw clang and links Boehm GC
statically, producing a real native Windows executable (no DLLs, no WSL to
run it) — it works for core AiLang programs. To enable it, install the
toolchain once:

```powershell
winget install MSYS2.MSYS2
# then, in the "MSYS2 MINGW64" shell:
pacman -S --needed mingw-w64-x86_64-clang mingw-w64-x86_64-gc
```

Programs that use the networking/regex stdlib (sockets, HTTP, TLS, Postgres,
Redis, regex) stay POSIX-only for now — run those with `ailrun`.

> **Why WSL?** AiLang's compiler runtime uses POSIX APIs (BSD sockets,
> `fork()`, POSIX regex) with no native Windows equivalent, so the compiler
> itself can't run natively on Windows yet. A native Winsock port may come
> later; until then, WSL gives you the full language and `ailexe` covers the
> "I want a `.exe`" case for ordinary programs.

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

# AiLang

A compiled programming language with a **minimum-token syntax** —
short enough for an LLM to generate cheaply, fast as `clang -O2`
once compiled.

```
fn fib(n) {
  if n < 2 rt n
  fib(n-1) + fib(n-2)
}

println(fib(30))   // 832040
```

```bash
$ ailangc run examples/fib.ail
832040
```

---

## Install

Pre-built binaries live on the [Releases page](https://github.com/Ray-T-r/AiLang/releases).
The installer downloads the right binary for your platform, places it in
a sane location, adds it to `$PATH`, and also drops an AiLang skill into
`~/.claude/skills/` so [Claude Code](https://claude.com/claude-code)
knows how to write `.ail` for you.

**macOS / Linux:**

```bash
curl -fsSL https://github.com/Ray-T-r/AiLang/releases/latest/download/install.sh | bash
```

You need a C compiler (`clang` / `cc`) and Boehm GC (`brew install bdw-gc
pkg-config` on macOS, `apt install libgc-dev pkg-config` on Debian/Ubuntu)
installed — `ailangc` calls them at compile time.

**Windows (PowerShell):**

```powershell
iwr -useb https://github.com/Ray-T-r/AiLang/releases/latest/download/install.ps1 | iex
```

You need LLVM (clang) on PATH — install from <https://llvm.org/>.

**Verify:**

```bash
ailangc --version
echo 'println("hello")' > /tmp/hi.ail && ailangc run /tmp/hi.ail
```

### Build from source instead

```bash
brew install rust pkg-config bdw-gc       # macOS prereqs
git clone https://github.com/Ray-T-r/AiLang
cd AiLang
cargo build --release
./target/release/ailangc run examples/fib.ail
```

---

## Why the syntax looks like this

The language is deliberately aggressive about token reduction. Most
savings come from cutting **structural overhead** the BPE tokenizer
charges full price for (braces, type annotations, `fn main()`
wrappers) — not from making keywords short. `fn` and `function` are
both 1 BPE token.

| AiLang form                                  | What it removes                       |
|----------------------------------------------|---------------------------------------|
| Implicit `main`: top-level stmts auto-wrap   | the whole `fn main() { … }` boilerplate |
| Optional type annotations (default `i64`)    | `: i64`/`-> i64` on most signatures |
| Compound assign: `+= -= *= /= %=`            | the `x = x + y` restatement |
| Unified `+` for strings (or explicit `++`)   | a parallel concat operator |
| `lp i in 1..10` unifies `for` and `while`    | one fewer keyword to recall |
| `x := expr` (declaration)                    | drops `let`/`var` |
| Implicit return of trailing expression       | no explicit `return` in the common case |
| Single-statement bodies skip `{...}`         | `if x>0 print(x)` is legal |
| Expression-bodied fns: `fn add(a, b) a + b`  | no `{ return … }` boilerplate, no type stutter |

Measured on a 4-program benchmark (fib, fizzbuzz, greet, sum) with
GPT-4's `cl100k_base` tokenizer — no comments, idiomatic in every language:

| Language   | Tokens | vs AiLang |
|------------|-------:|----------:|
| **AiLang** | **130** | **1.00×** |
| Python     | 136    | 1.05× |
| JavaScript | 167    | 1.28× |
| Rust       | 186    | 1.43× |
| Go         | 223    | 1.72× |
| Java       | 261    | 2.01× |
| C          | 270    | 2.08× |

Source: [`bench/`](bench/) — reproduce with `python3 bench/count_all.py`.

---

## What works today

The compiler runs the full pipeline: lex → parse → sema → codegen → `clang -O2` → native binary. **42 automated tests pass; 12 end-to-end example programs compile and run correctly.**

Working: functions, recursion, mutual recursion, `if`/`el`, `lp` (for-in
and while), `mt` match with tuple patterns, `|>` pipe, arrays (`[i64]`
and `[str]`), maps (`{i64:i64}`), string `+` concat with Boehm GC, FFI
to libc (`ex fn ...`), modules (`im "path"`), a seed stdlib in
[`std/`](std/), and generics syntax (currently monomorphic over `i64`).

Default backend transpiles to C99/C11 — `clang -O2` produces the
machine code. There's also an experimental LLVM IR text backend
(`--backend ir`) that covers a tiny subset (println of literals) and
grows incrementally.

---

## A short tour

All of these compile and run today. Each lives under [`examples/`](examples/)
with an expected-output fixture in [`tests/e2e/`](tests/e2e/).

### Functions, implicit return, braceless one-liners

```
fn add(a, b) a + b                            // expression body; types inferred

fn fib(n) {
  if n < 2 rt n                                // braceless single-stmt if
  fib(n-1) + fib(n-2)                          // trailing expr = return value
}

println(fib(30))                               // 832040 (no fn main needed)
```

### Unified `lp` loop + compound assign

```
lp i in 1..10 {           // for-in range (exclusive upper bound)
  print(i); print(" ")
}
println("")

mu n := 5                  // `mu` makes it mutable; `:=` is the binding form
lp n > 0 {                 // same `lp`, used as while
  println(n)
  n -= 1
}
```

### Match with tuple patterns

```
lp i in 1..16 {
  mt (i%3, i%5) {
    (0,0) => println("FizzBuzz");
    (0,_) => println("Fizz");
    (_,0) => println("Buzz");
    _     => println(i);
  }
}
```

### Pipe operator

```
fn double(x) x * 2
fn square(x) x * x

println(5 |> double |> square)     // square(double(5)) = 100
```

### String concat (Boehm GC backed)

```
fn greet(name:str) "Hello, " + name + "!"   // `+` concatenates when an operand is str

msg := greet("AiLang")
println(msg)            // → Hello, AiLang!
println(len(msg))       // → 14
```

### FFI to libc

```
ex fn puts(s:str) -> i32
ex fn abs(n:i32) -> i32

puts("hello via libc puts!")
println(abs(-7))        // → 7
```

### Arrays & maps

```
nums := [10, 20, 30, 40, 50]
println(len(nums))      // 5
println(nums[2])        // 30

mu total := 0
lp x in nums {
  total += x
}
println(total)          // 150

m := {1: 10, 2: 20, 3: 30}
m[4] = 40                // mutating insert
println(has(m, 4))       // true
```

### Modules

```
// in mathlib.ail
fn square(x) x * x

// in main.ail
im "mathlib.ail"
println(square(7))      // 49
```

A seed stdlib lives in [`std/`](std/) — `std/math.ail` (`min`/`max`/
`pow`/`gcd`/libc `abs`) and `std/str.ail` (libc `strcmp`/`atoi` and a
`parse_int` wrapper). The same files are mirrored at
[`examples/std/`](examples/std/) so in-tree e2e tests can
`im "std/…"` against a path that resolves relative to the example
file — keep both copies in sync when editing.

For the full language reference (every keyword, every operator, the
EBNF), see [`spec/grammar.ebnf`](spec/grammar.ebnf).

---

## Performance

Two micro-benchmarks: a 1M-iteration trial-division prime sieve and
recursive `fib(40)`. All compiled languages use `-O2`/release; all
binaries verified to produce the same output. Measured with `hyperfine
--warmup 1 --min-runs 5` on Apple Silicon.

`fib(40)` (recursive, 165M calls):

| Implementation | mean ± σ      | vs fastest |
|----------------|---------------|-----------:|
| Rust (`-O`)    | (fastest)     |       1.00× |
| C (`clang -O2`) | ≈ Rust        |       1.00× |
| **AiLang**     | **+7%**       |   **1.07×** |
| Java (HotSpot) | +13%          |       1.13× |
| Go (`go build`) | +57%          |       1.57× |
| Node.js v24    | +251%         |       3.51× |
| Python 3.14    | +3700%        |      38.00× |

Counting primes < 500 001:

| Implementation | mean ± σ      | vs fastest |
|----------------|---------------|-----------:|
| C (`clang -O2`) | 9.4 ms       |       1.00× |
| Go (`go build`) | 9.5 ms       |       1.01× |
| Rust (`-O`)    | 9.8 ms        |       1.04× |
| **AiLang**     | **10.2 ms**   |   **1.08×** |
| Node.js v24    | 28.9 ms       |       3.09× |
| Java (HotSpot) | 33.7 ms       |       3.60× |
| Python 3.14    | 346 ms        |      36.8×  |

AiLang lands within **~8% of hand-written C** and edges past Rust on
the prime sieve — the C transpilation backend gives `clang -O2` the
same IR clang would synthesize from C, so the optimizer's room is
roughly the same. Reproduce with `hyperfine` against the programs in
[`bench/perf/`](bench/perf/).

---

## How it works

```
foo.ail
   ↓ logos                ailang-lexer
Vec<Token>
   ↓ RD + Pratt           ailang-parser
ast::Module
   ↓ name + arity check   ailang-sema  ── inferred fn returns, expr_types side table
ResolvedModule
   ↓ codegen              ailang-codegen-llvm (→ C, default) or -codegen-ir (→ LLVM IR)
foo.c | foo.ll
   ↓ clang -O2 [-lgc]     ailang-driver
foo                       (native Mach-O / ELF binary)
```

Errors at any stage become **ariadne diagnostics** with colored carets:

```
Error: undefined name `unknown_name`
   ╭─[sema_error.ail:3:8]
   │
 3 │   x := unknown_name
   │        ───────┬────
   │               ╰──── undefined name `unknown_name`
───╯
```

---

## Using AiLang from Claude Code

If you use [Claude Code](https://claude.com/claude-code), running
`./install.sh` also drops an `ailang` skill into
`~/.claude/skills/ailang/`. After that, Claude knows the language well
enough to write correct `.ail` programs on first try — just ask it for
something like "write FizzBuzz in AiLang" or "show me how AiLang's
match works."

The skill content lives at [`skill/SKILL.md`](skill/SKILL.md) — read it
yourself if you want a concise reference.

---

## Contributing

Workspace lives under [`crates/`](crates/) — `ailang-lexer`,
`ailang-parser`, `ailang-sema`, `ailang-codegen-llvm` (emits C — name
is historical), `ailang-codegen-ir` (emits LLVM IR text),
`ailang-driver` (pipeline orchestration), `ailangc` (CLI). Pure-data
types are in `ailang-syntax`.

```bash
cargo build --release            # build the compiler
cargo test                       # workspace tests + e2e
cargo test -p ailang-driver e2e_fib   # single e2e case
```

End-to-end test fixtures live in [`tests/e2e/`](tests/e2e/) (expected
stdout, one per example).

---

## License

Dual-licensed under either of

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or
  <https://www.apache.org/licenses/LICENSE-2.0>)
- MIT license ([LICENSE-MIT](LICENSE-MIT) or
  <https://opensource.org/licenses/MIT>)

at your option.

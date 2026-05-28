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

> **macOS Gatekeeper note** — the released binary is not yet signed with
> an Apple Developer ID. The `curl | bash` installer above avoids the
> warning automatically. If you instead **download `ailangc-macos-*`
> from the Releases page in your browser**, macOS will refuse to run it
> with "Apple cannot verify ... safety." Clear the quarantine attribute
> once and you're done:
>
> ```bash
> xattr -d com.apple.quarantine ~/Downloads/ailangc-macos-aarch64
> ```
>
> Or right-click the file in Finder → **Open** → confirm once.

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

---

## Three-program benchmark

To check whether the syntax actually pays off and the runtime keeps
up, we compare AiLang against six mainstream languages on three
representative workloads — a script, a backend handler, and a numeric
algorithm — measured for both **token count** (cl100k_base, the
GPT-4 family BPE tokenizer) and **wall-clock runtime**. All sources
live under [`bench/perf/`](bench/perf/); each program is comment-free
and idiomatic for its language. Runtime measured with `hyperfine
--warmup 1 --min-runs 5` on Apple Silicon, all compiled languages at
`-O2`/`-O`/release.

### The three programs

| program     | what it does | category |
|-------------|--------------|----------|
| `wordcount` | repeat a 9-word seed 500 000× → split into ~4.5 M tokens → hash-count frequencies | script |
| `jsonapi`   | iterate 50 000 user records → filter by `age >= 40` → format each as a JSON record → sum response bytes | backend |
| `primes`    | count primes below 500 001 via trial division | algorithm |

All seven implementations of each program produce the **exact same
output** (verified) — `9 / 1000000` for wordcount, `28836 / 1198341`
for jsonapi, `41538` for primes.

### Token cost (lower is better)

| program   | AiLang | Python | JS  | Rust | Go  | Java | C   |
|-----------|-------:|-------:|----:|-----:|----:|-----:|----:|
| wordcount |     67 |     62 |  74 |  107 |  86 |  107 | 421 |
| jsonapi   |    100 |     92 | 110 |  128 | 139 |  141 | 148 |
| primes    |    104 |    104 | 121 |  139 | 144 |  152 | 148 |
| **TOTAL** |**271** |    258 | 305 |  374 | 369 |  400 | 717 |
| vs AiLang |  1.00× |  0.95× |1.13×|1.38× |1.36×|1.48× |2.65× |

AiLang is within **5%** of Python on tokens — Python is the
generally-acknowledged king of script density, so being neck-and-neck
with it is the goal of the syntax design. Against the other compiled
languages AiLang wins by **30–50%**, and against hand-written C with
its missing-batteries (no hash map, no `split`) it wins by **2.6×**.

Reproduce: `python3 bench/count_three.py`.

### Runtime (lower is better)

| program   | C      | Go     | Rust   | **AiLang** | Java   | Node   | Python  |
|-----------|-------:|-------:|-------:|-----------:|-------:|-------:|--------:|
| wordcount | 32.1ms | 76.4ms | 83.9ms |**145.9ms** |188.1ms |302.5ms | 336.0ms |
| jsonapi   |  3.3ms |  3.1ms |  2.6ms |  **9.4ms** | 32.1ms | 18.6ms |  19.6ms |
| primes    |  9.5ms |  9.6ms |  9.9ms | **15.4ms** | 33.7ms | 28.9ms | 362.6ms |
| **vs C**  |  1.00× |  ≈1.0× |  ≈1.0× |  **2.7×*** |  6.4×  |  6.0×  |  17×    |

\* geomean across the three programs.

The same data as ratios to the fastest compiled language for each row:

| program   | vs fastest compiled | AiLang slowdown |
|-----------|--------------------:|----------------:|
| wordcount | C is fastest (32ms) |          4.55× |
| jsonapi   | Rust is fastest (2.6ms) |      3.62× |
| primes    | C is fastest (9.5ms) |         1.62× |

AiLang **beats** every dynamic / JIT language (Python, Node, Java) on
every program, and stays within a small constant factor of C/Rust/Go.
The wordcount gap is the largest because AiLang's `{str:i64}` is a
generic hash table with growing — the hand-written C version uses a
9-entry linear scan, which is the right data structure for nine unique
words but not what you'd reach for in production code. (Compare that
421-token C wordcount to AiLang's 67 to see why.)

Reproduce: `hyperfine` against the three programs under
[`bench/perf/<lang>/`](bench/perf/) — see
[`bench/results/SUMMARY.md`](bench/results/SUMMARY.md) for the raw
numbers.

### What this benchmark is *not*

- Not a microbench of one operation (`fib(40)` etc.) — those over-emphasize
  arithmetic and miss the actual cost shape of real programs.
- Not a megabench — three programs is enough to triangulate
  "scripting / backend / numeric" without cherry-picking.
- Not run under JIT warmup conditions — Java and Node pay startup cost
  the way they do in production (a CLI invocation), which matches the
  scripting use case better than a benchmark harness that hides it.

---

## What works today

The compiler runs the full pipeline: lex → parse → sema → codegen → `clang -O2` → native binary. **55 automated tests pass; 25 end-to-end example programs compile and run correctly.**

Working: functions, recursion, mutual recursion, `if`/`el`, `lp` (for-in
+ while + map `(k,v)` destructure), `mt` match with tuple **and variant**
patterns, `|>` pipe, arrays (`[i64]` and `[str]`), maps (`{i64:i64}`,
`{str:i64}`, `{str:str}`), string `+` concat with Boehm GC, FFI to
libc (`ex fn ...`), modules (`im "path"`), a seed stdlib in
[`std/`](std/), **real generic monomorphization** (one `fn id<T>(x:T)`,
specialized per call-site type via `_Generic`), **user-defined structs**
(`st Point { x:i64; y:i64 }` + struct literal + `.field` access + auto
pretty-print), **ADTs / tagged unions** (`en Maybe { None, Some(v:i64) }`
+ constructor calls + destructuring match), **first-class lambdas with
capture** (lifted to top-level static fns, `{fn, env}` fat pointers,
env GC-allocated, by-value capture; **str-returning closures supported**),
**implicit return from tail-position `if`/`mt`** (no explicit `rt`
needed on each branch), `mu` params for in-fn mutation, plus a broad
set of always-available builtins:

- **I/O & process**: `read_file`, `write_file`, `read_line`, `args`,
  `exit`, `get_env`
- **String ops**: `contains`, `starts_with`, `ends_with`, `index_of`,
  `to_upper`, `to_lower`, `trim`, `substring`, `replace`, `split`,
  `repeat`, `pad_left`, `pad_right`, `chr`, `ord`
- **Conversions + formatting**: `int_to_str`, `str_to_int`,
  `float_to_str`, `str_to_float`, `str_to_bool`,
  `format(fmt, ...)` (printf-style)
- **Containers**: `push`/`pop`/`sort`/`reverse`/`slice` (return fresh
  array), polymorphic `contains`/`index_of` (str / `[i64]` / `[str]`),
  `join(arr, sep)` (for `[i64]` and `[str]`), `len`, `has`, `keys(m)`,
  `values(m)`, auto pretty-print via `println(…)` for arrays, maps,
  and user structs
- **Higher-order**: `map(arr, f)`, `filter(arr, pred)`, `reduce(arr, init, f)`
  for both `[i64]` and `[str]` — pair with inline lambdas
  (`map(xs, fn(x) x*2)`)
- **Regex**: `regex_match`, `regex_find` (POSIX extended)
- **Math**: `abs_i64`, `abs_f64`, `sign(n)`, `clamp(n, lo, hi)`
  always-available; `sqrt`/`pow`/`sin`/`cos`/`tan`/`log`/`log2`/
  `log10`/`exp`/`floor`/`ceil` via `im "std/math.ail"` (libm linked
  unconditionally)
- **Result `!T` + `?` propagation**: `ok(v)` / `err_i64(msg)` /
  `err_str(msg)` / etc. construct; `expr?` propagates the first failure
  to the enclosing fn's return; `is_ok` / `is_err` / `unwrap` / `err_msg`
  inspect

Default backend transpiles to C99/C11 — `clang -O2` produces the
machine code. There's also an experimental LLVM IR text backend
(`--backend ir`) that covers a tiny subset (println of literals) and
grows incrementally.

**Known limitations**:
- Arrays/maps only carry primitive elements (`[i64]`/`[str]`,
  `{i64:i64}`/`{str:i64}`/`{str:str}`); element types of user structs
  or enums need a follow-up runtime variant.
- Nested JSON (`std/json.ail` flat-only) wants `[JsonValue]` and a
  recursive variant — depends on the array-of-struct work above.
- Real generics support T = primitive (`i64`/`f64`/`bool`/`str`) with
  unification through `[T]`/`{K:V}`; T = struct/enum/fn-pointer is
  future work.
- `?` postfix and `!T` only support T = primitive.
- ADT variants can't yet recurse into the enclosing enum (e.g.
  `Cons(head:i64, tail:List)`) — needs heap boxing for self-reference.
- `arr[i] = x` from inside a function body currently mis-codegens
  (the `ailang_at` macro is rvalue-only); top-level main works. Mutating
  algorithms across function calls need to use functional rebuild
  (`filter`/`push`) for now.

---

## A short tour

All of these compile and run today. Each lives under [`examples/`](examples/)
with an expected-output fixture in [`tests/e2e/`](tests/e2e/).

### The three benchmark programs

The full source of each row in the benchmark table — note that none of
the AiLang files have comments and none wrap their top-level code in
`fn main()`:

**`bench/perf/ailang/wordcount.ail`** — 67 tokens:

```
seed := "the quick brown fox jumps over the lazy dog "
text := repeat(seed, 500000)
words := split(text, " ")
mu counts:{str:i64} := {}
lp w in words {
  counts[w] = counts[w] + 1
}
println(len(counts))
println(counts["the"])
```

**`bench/perf/ailang/jsonapi.ail`** — 100 tokens:

```
mu count := 0
mu total := 2
lp i in 0..50000 {
  age := 18 + (i % 52)
  if age >= 40 {
    rec := format("{\"id\":%lld,\"name\":\"user_%lld\",\"age\":%lld}", i, i, age)
    if count > 0 { total += 1 }
    total += len(rec)
    count += 1
  }
}
println(count)
println(total)
```

**`bench/perf/ailang/primes.ail`** — 104 tokens:

```
fn is_prime(n) {
  if n < 2 rt 0
  if n == 2 rt 1
  if n % 2 == 0 rt 0
  mu i := 3
  lp i*i <= n {
    if n % i == 0 rt 0
    i += 2
  }
  1
}

mu c := 0
lp k in 2..500001 {
  c += is_prime(k)
}
println(c)
```

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

### Structs

A capitalized identifier followed immediately by `{` parses as a struct
literal — `if cond { body }` (lowercase cond) stays unambiguous.

```
st Point { x: i64; y: i64 }
st Person { name: str; age: i64 }

fn dist_sq(p:Point) p.x * p.x + p.y * p.y

p := Point { x: 3, y: 4 }
println(dist_sq(p))                 // 25

alice := Person { name: "alice", age: 30 }
println(alice.name)                  // alice
```

### Lambdas, closures, higher-order

```
add := fn(a, b) a + b
println(add(3, 4))                          // 7

threshold := 3
nums := [1, 2, 3, 4, 5, 6]
println(filter(nums, fn(x) x > threshold))  // [4, 5, 6]
println(map(nums, fn(x) x * 2))             // [2, 4, 6, 8, 10, 12]
println(reduce(nums, 0, fn(a, b) a + b))    // 21
```

### ADTs / tagged unions

```
en Shape {
  Circle(r:i64),
  Square(side:i64),
  Rect(w:i64, h:i64),
}

fn area(s:Shape) {
  mt s {
    Circle(r)  => r * r * 3;
    Square(s)  => s * s;
    Rect(w, h) => w * h;
  }
}

println(area(Circle(5)))                    // 75
println(area(Rect(3, 7)))                   // 21
```

### `!T` results + `?` propagation

```
fn parse_int_safe(s:str) -> !i64 {
  if regex_match("^-?[0-9]+$", s) rt ok(str_to_int(s))
  err_i64("not a number: " + s)
}

fn parse_sum(a:str, b:str) -> !i64 {
  x := parse_int_safe(a)?
  y := parse_int_safe(b)?
  ok(x + y)
}

println(unwrap(parse_sum("3", "4")))        // 7
r := parse_sum("3", "oops")
println(err_msg(r))                         // not a number: oops
```

### Generics

```
fn id<T>(x:T) -> T x
fn first<T>(arr:[T]) -> T arr[0]

println(id(42))                             // 42
println(id("hello"))                        // hello
println(first([10, 20, 30]))                // 10
println(first(["alice", "bob"]))            // alice
```

### Modules + seed stdlib

```
// in main.ail
im "std/math.ail"

println(max(3, 7))                          // 7
println(gcd(36, 24))                        // 12
```

A small stdlib lives in [`std/`](std/):

- `std/math.ail` — `min`/`max`/`ipow`/`gcd` + libc `abs`/`rand`/`srand`
  + libm `sqrt`/`pow`/`sin`/`cos`/`tan`/`log`/`log2`/`log10`/`exp`/
  `floor`/`ceil`.
- `std/str.ail` — libc `strcmp`/`atoi` and an `eq`/`parse_int` wrapper.
- `std/json.ail` — `parse_flat_obj_str(text) -> {str:str}` and
  `parse_flat_obj_int(text) -> {str:i64}`, hand-written in pure
  AiLang (~150 lines).

The same files are mirrored at [`examples/std/`](examples/std/) so
in-tree e2e tests can `im "std/…"` against a path that resolves
relative to the example file — keep both copies in sync when editing.

For the full language reference (every keyword, every operator, the
EBNF), see [`spec/grammar.ebnf`](spec/grammar.ebnf).

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

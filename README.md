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

Lambdas are lifted to top-level static C functions, and the runtime
value of a closure is a `{fn, env}` fat pointer. Capturing outer-scope
locals just works — the env struct is GC-allocated and snapshots each
captured variable by value at construction time.

```
// Plain lambda, no capture.
add := fn(a, b) a + b
println(add(3, 4))                          // 7

// Capturing closure — `threshold` is captured by value.
threshold := 3
nums := [1, 2, 3, 4, 5, 6]
println(filter(nums, fn(x) x > threshold))  // [4, 5, 6]

// String-returning closure.
greeting := "hello, "
greet := fn(name:str) greeting + name + "!"
println(greet("alice"))                     // hello, alice!

// HOF combinators.
println(map(nums, fn(x) x * 2))             // [2, 4, 6, 8, 10, 12]
println(reduce(nums, 0, fn(a, b) a + b))    // 21
```

User functions that take other functions need an annotated parameter
type so codegen knows the C function-pointer shape:

```
fn apply(f:fn(i64,i64)->i64, x, y) f(x, y)
println(apply(fn(a, b) a * b, 5, 6))        // 30
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

// Unit-only variants — classic enum.
en Color { Red, Green, Blue }
```

Each `en` lowers to a tagged union; constructor calls take the
matching field types; `mt` arms can destructure by position.

### `!T` results + `?` propagation

```
fn parse_int_safe(s:str) -> !i64 {
  if regex_match("^-?[0-9]+$", s) rt ok(str_to_int(s))
  err_i64("not a number: " + s)
}

fn parse_sum(a:str, b:str) -> !i64 {
  x := parse_int_safe(a)?    // bail with err if not numeric
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

Each call site monomorphizes the generic fn for the concrete T, then
a `#define` + `_Generic` dispatches the bare name to the right
instance.

### Modules

```
// in mathlib.ail
fn square(x) x * x

// in main.ail
im "mathlib.ail"
println(square(7))      // 49
```

A seed stdlib lives in [`std/`](std/):

- `std/math.ail` — `min`/`max`/`ipow`/`gcd` + libc `abs`/`rand`/`srand`
  + libm `sqrt`/`pow`/`sin`/`cos`/`tan`/`log`/`log2`/`log10`/`exp`/
  `floor`/`ceil`.
- `std/str.ail` — libc `strcmp`/`atoi` and an `eq`/`parse_int` wrapper.
- `std/json.ail` — `parse_flat_obj_str(text) -> {str:str}` and
  `parse_flat_obj_int(text) -> {str:i64}`. Hand-written in **pure
  AiLang** (~150 lines) — proof the language is bootstrapping-capable
  for its own libraries. Flat-only for now; nested objects/arrays land
  alongside sum-type support.

The same files are mirrored at [`examples/std/`](examples/std/) so
in-tree e2e tests can `im "std/…"` against a path that resolves
relative to the example file — keep both copies in sync when editing.

### Builtins (always in scope, no `im` needed)

Each maps to a `static` C helper baked into the codegen prelude; all
returned strings are GC-allocated.

**I/O & process**

| Builtin | Signature | What it does |
|---------|-----------|--------------|
| `read_file(path)`        | `(str) -> str`        | Slurp the whole file. Returns `""` on error. |
| `write_file(path, body)` | `(str, str) -> bool`  | Overwrite the file. `true` on success. |
| `read_line()`            | `() -> str`           | One stdin line, trailing `\n` stripped. `""` at EOF. |
| `args()`                 | `() -> [str]`         | `argv[1..]` of the running process. |
| `exit(code)`             | `(i64) -> ()`         | Terminate with the given exit code. |
| `get_env(name)`          | `(str) -> str`        | Read an env var; `""` if unset. |

**Conversions & formatting**

| Builtin | Signature |
|---------|-----------|
| `int_to_str(n)` / `str_to_int(s)`     | `(i64) -> str` / `(str) -> i64` |
| `float_to_str(x)` / `str_to_float(s)` | `(f64) -> str` / `(str) -> f64` |
| `str_to_bool(s)`                      | `(str) -> bool` (accepts `true`/`1`/`yes`) |
| `chr(i)` / `ord(s)`                   | `(i64) -> str` (1 byte) / `(str) -> i64` |
| `format(fmt, ...)`                    | printf-style → fresh GC string |

**String operations** (byte-oriented; not Unicode-aware)

| Builtin | Signature |
|---------|-----------|
| `contains(haystack, needle)`     | `(str, str) -> bool` (also works on arrays) |
| `starts_with(s, prefix)` / `ends_with(s, suffix)` | `(str, str) -> bool` |
| `index_of(haystack, needle)`     | `(str, str) -> i64` (`-1` if not found) |
| `to_upper(s)` / `to_lower(s)`    | `(str) -> str` |
| `trim(s)`                        | `(str) -> str` (strip ASCII whitespace) |
| `substring(s, start, end)`       | `(str, i64, i64) -> str` (byte indices, clamped) |
| `replace(s, old, new)`           | `(str, str, str) -> str` |
| `split(s, sep)`                  | `(str, str) -> [str]` (Python-style) |
| `repeat(s, n)`                   | `(str, i64) -> str` |
| `pad_left(s, w, pad)` / `pad_right(s, w, pad)` | `(str, i64, str) -> str` |

**Containers**

| Builtin | Signature |
|---------|-----------|
| `len(x)`     | `(str / [T] / {K:V}) -> i64` |
| `has(m, k)`  | `({K:V}, K) -> bool` |
| `push(arr, x)` / `pop(arr)`       | value-semantics, returns a fresh array |
| `sort(arr)` / `reverse(arr)` / `slice(arr, lo, hi)` | same |
| `contains(arr, x)` / `index_of(arr, x)` | polymorphic over `[i64]` / `[str]` |
| `join(arr, sep)`                  | `([i64] \| [str], str) -> str` |
| `keys(m)` / `values(m)`           | per-map element type |
| `map(arr, f)` / `filter(arr, p)` / `reduce(arr, init, f)` | higher-order |

**Math** (`abs_i64` / `abs_f64` / `sign` / `clamp` always in scope;
the rest via `im "std/math.ail"`)

| Builtin | Signature |
|---------|-----------|
| `abs_i64(n)` / `abs_f64(x)`       | `(i64) -> i64` / `(f64) -> f64` |
| `sign(n)`                         | `(i64) -> i64` (`-1` / `0` / `1`) |
| `clamp(n, lo, hi)`                | `(i64, i64, i64) -> i64` |
| `sqrt` / `pow` / `sin` / `cos` / `tan` / `log` / `log2` / `log10` / `exp` / `floor` / `ceil` | libm wrappers (need `im "std/math.ail"`) |

**Regex** (POSIX extended, libc-backed)

| Builtin | Signature |
|---------|-----------|
| `regex_match(pat, text)` | `(str, str) -> bool` |
| `regex_find(pat, text)`  | `(str, str) -> str` (first match, `""` if none) |

**Result `!T`**

| Builtin | Signature |
|---------|-----------|
| `ok(v)`                                  | polymorphic: wraps `v:T` as `!T` |
| `err_i64(msg)` / `err_str(msg)` / `err_bool(msg)` / `err_f64(msg)` | construct the error variant |
| `unwrap(r)`                              | unwrap or abort with the err msg |
| `is_ok(r)` / `is_err(r)` / `err_msg(r)`  | inspect |
| `expr?`                                  | postfix; propagate err to the enclosing fn's return |

```
// Round-trip through the filesystem + use a CLI arg.
a := args()
path := a[0]
write_file(path, "hello from AiLang\n")
println(read_file(path))     // hello from AiLang

// String pipeline + str-keyed map (word counter).
mu counts := {"_pin": 0_i64}     // type-pin so codegen picks {str:i64}
counts["_pin"] = 0               // clear the pin
lp w in split("the fox and the dog", " ") {
  counts[w] = counts[w] + 1
}
println(counts)                   // {"the": 2, "fox": 1, "and": 1, "dog": 1}

// printf-style formatting + libm.
im "std/math.ail"
println(format("sqrt(2) = %.6f", sqrt(2.0)))   // sqrt(2) = 1.414214

// POSIX regex.
println(regex_match("[0-9]+", "user42"))    // true
println(regex_find("[A-Z][a-z]+", "hello World")) // World
```

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

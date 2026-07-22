---
name: ailang
description: Write, read, and compile AiLang (`.ail`) source with the self-hosted `ailc` compiler. Use whenever the user asks for AiLang code, references AiLang syntax, mentions an `.ail` file, asks about the `ailc` compiler, or works inside an AiLang project. AiLang is a compiled, statically-typed language with a deliberately minimal-token syntax — 2-char keywords (`fn`/`lp`/`mt`/`rt`/`el`/`en`/`st`), optional type annotations (default `i64`), implicit `main`, implicit return. It compiles to C and links via `clang -O2`. Supports structs, classes (single inheritance + virtual methods), enums/recursive ADTs, real generics (generic structs `Box<T>` + enums `Option<T>` + multi-param `<A,B>` + `tr` trait bounds), operator overloading, closures with capture, `!T`/`?` error propagation, string interpolation `"${e}"`, UFCS, module namespacing (`im "…" as m`), C++ library interop via `csrc`, a multi-error type checker with *"did you mean?"* suggestions, `ailc run`/`check`/`test` + `--json` machine-readable diagnostics, and a 25-module stdlib (sockets/HTTP server+client/TLS/Postgres/Redis/WebSocket/JSON/CSV/time/str/math/threads/seq/web/jwt/mysql/sqlite/fs/encoding/url/uuid/args/log/env).
---

# AiLang Quick Reference (self-hosted `ailc`)

AiLang's compiler is **written in AiLang itself** (`ailc`). It lexes, parses, type-tracks, and lowers `.ail` → C, then drives `clang -O2` to a native binary. The syntax is terse (short keywords, `i64` defaults, implicit `main`) and the binary runs at C speed because it *is* C, generated — but the property you'll actually lean on is the tight `check`/`run`/`test` loop with structured diagnostics (below).

## CLI

```
ailc [run|check|test|lib] [--keep-c|-k] [--json] [--asan] <input.ail> [output] [-- prog-args]
```

- **`ailc <in.ail> [out]`** — compile to a native binary (does NOT run it). Default output = input with `.ail` stripped (`fib.ail` → `fib`).
- **`ailc run <in.ail> [-- args]`** — compile to a throwaway binary, run it (forwarding any post-`--` args), then delete it. The one-step everyday loop: you see only the program's stdout, and its exit status propagates (nonzero child → `ailc run` exits nonzero).
- **`ailc check <in.ail>`** — parse + type-check ONLY, no clang link (fast). Prints `ok` and exits 0 when clean; prints diagnostics and exits nonzero otherwise. The inner loop while fixing errors.
- **`ailc test <f1.ail> [f2.ail …]`** — compile + run each file; print `PASS`/`FAIL` per file + a `N passed, M failed` summary; exit nonzero if any failed. Pair it with the **`assert(cond, msg)`** builtin (aborts with `assertion failed: msg` + nonzero on a false condition). A `*_test.ail` that runs its `assert`s and exits 0 is a passing test. (A POSIX shell expands `ailc test tests/*_test.ail`.)
- **`ailc lib <in.ail> [out]`** — compile to a native **shared library** (`.dylib` macOS / `.so` Linux / `.dll` + `.dll.a` import lib on Windows) + a matching C header (`out.h`), for calling AiLang from C/C++/Rust/ctypes. Each top-level `fn` **of the root file** with a C-ABI signature (params & return in `i64`/`i32`/`u32`/`bool`/`f64`/`str`/`bytes`) is exported under its **plain name** (`i64`/`bool`→`int64_t`, `f64`→`double`, `str`→`const char*`, `bytes`→`ailang_bytes`); aggregate-typed fns (arrays/maps/structs) are skipped with a note. Put private helpers in an imported module — only the root file's fns are exported. Boehm GC self-initialises at load (constructor), so the host needs **no init call**; only the wrappers are exported (`-fvisibility=hidden` / `dllexport`). Link a host with `clang app.c out.dylib -o app` (Windows: `clang app.c out.dll.a -o app.exe`).
- **`--asan`** — link `-fsanitize=address -g` into the build (POSIX `build`/`run`/`test` only; ignored/no-op on the Windows `.exe` path). For chasing down a bare RE with no other diagnostic — AddressSanitizer reports the exact file:line and kind (heap/stack overflow, use-after-free) on the crashing access instead of a bare nonzero exit. **macOS (Apple Silicon) caveat:** the dynamic ASan runtime relies on `DYLD_INSERT_LIBRARIES` interposition, which is blocked by default for a normal ad-hoc-signed binary — you'll see `ERROR: Interceptors are not working` at runtime even though the flag compiled and linked fine (confirm the link with `otool -L <bin> | grep asan`). Getting the dynamic interposition to actually fire needs a real (non ad-hoc) code-signing identity with the `com.apple.security.cs.allow-dyld-environment-variables` entitlement — not just worth chasing for a one-off debug session. On macOS, reach for **`lldb ./bin` → `run`** instead (prints the crashing frame on SIGSEGV/SIGABRT, no extra setup). `--asan` works as expected on Linux (no such sandboxing).
- **`--json`** — emit diagnostics as machine-readable JSONL: one `{"severity","code","line","col","message","source"}` object per error (`"code"` is a stable `AIL####`), and `{"severity":"ok"}` on a clean `check`. For agents/tools that parse errors instead of scraping human text.
- `--keep-c` / `-k` keeps the generated `<output>.c` (deleted by default after a successful compile).
- Positional otherwise (arg 2 = output path). `run`/`check`/`test`/`lib`/`compile` are the only subcommands; there is no `--emit-c`/`--backend`. (The Rust `ailangc run/compile` CLI is a different, archived compiler.)
- `im "..."` imports resolve relative to the source file; `std/` modules resolve via `$AILANG_STD` (set by the installer — see "Standard library" below). `ailc` then shells out to `clang`, auto-adding `-lgc` and (when used) OpenSSL / libpq / `-lm`.

### Tight loop (agents: cheapest feedback first)

```bash
ailc check x.ail --json   # parse + type errors, structured JSONL, no clang. Fix ALL reported, then:
ailc run   x.ail          # compile + run in one step
ailc test  x_test.ail     # if it has assert()s — PASS/FAIL summary, nonzero if any fail
```
The checker reports **every** confident error in one pass — fix the whole batch per iteration, don't loop one-at-a-time. Reach for plain `ailc x.ail [out]` only when you want a persistent binary to ship.

```bash
echo 'println("hi")' > hi.ail
ailc run hi.ail              # hi                ← compile + run in one step
ailc check hi.ail            # ok                ← type-check only, no link
ailc check hi.ail --json     # {"severity":"ok"}
ailc hi.ail /tmp/hi && /tmp/hi   # build a persistent binary, then run it
ailc -k hi.ail && cat hi.c   # inspect generated C
```

### On Windows

`ailc` runs natively and writes a **`.exe`**: `ailc hi.ail hi` produces `hi.exe`, run it
with `.\hi.exe` (not `./hi`). The `.exe` is self-contained (depends only on
`KERNEL32`/`msvcrt`). Networking/regex
programs (sockets/HTTP/TLS/Postgres/Redis/`regex_*`) are POSIX-only — build and run
those under WSL, not native `ailc`.

## Mental model

- Default type is **`i64`**. Unannotated params and locals are `i64`.
- All keywords are short (`fn`, `lp`, `mt`, `rt`, `el`, `en`, `st`, `mu`, `im`, `ex`). Do **not** substitute `for`/`while`/`match`/`return`/`else`/`enum`/`struct`/`let`/`var`.
- Top-level statements form an **implicit `main`** — don't write `fn main` for a script.
- The trailing expression of a function/block body is its **return value** — no `rt` needed at the end.
- `if` and `mt` and `{...}` blocks are **expressions** — they yield a value.

## Keywords

| kw | meaning | kw | meaning |
|----|---------|----|---------|
| `fn` | function / lambda | `mt` | match |
| `rt` | return (early only) | `st` | struct declaration |
| `if` / `el` | if / else (`el if` chains) | `en` | enum / ADT declaration |
| `lp` | loop (while + for-in + range + k,v) | `im` | import a file |
| `br` / `ct` | break / continue | `ex` | extern C function |
| `mu` | mutable binding | `cinc` | include a C header |
| `in` | iterator separator in `lp` | `true` `false` | bool literals |
| `cl` | class (single inheritance) | `vt` | virtual-method marker |
| `csrc` | compile + link a C++ shim | `super` | parent-method call (in a method) |
| `tr` | trait declaration (generic bound) | `<T>` `<A,B>` | generic type params |

`println` / `print` are **builtins, not keywords**. Full-word forms do **not** exist — use the short keyword: `cl` (not `class`), `st` (not `struct`), `en` (not `enum`), `lp` (not `for`/`while`), `el` (not `else`), `rt` (not `return`), `mt` (not `match`). Also no `def` / `let` / `var`.

## Operators

```
declare     :=                 introduce a NEW binding
assign      =                  reassign an existing `mu` binding
compound    += -= *= /= %=     (binding must be mu)
arithmetic  + - * / %          (- x is unary negate)
concat      +  (both str)  or  ++   (explicit string concat)
compare     == != < <= > >=
logical     && || !
bitwise     & | ^ << >>        (prefix & is address-of)
pipe        |>                 x |> f  ⇒ f(x);  x |> f(b) ⇒ f(x, b)
coalesce    ??                 m[k] ?? default
error prop  expr?              postfix — propagate err inside an !T fn
range       .. ..=             ONLY inside `lp i in lo..hi` (exclusive / inclusive)
interp      "${expr}"          string interpolation
```

**No ternary `cond ? a : b`** — use an `if` expression. `?` is only postfix error-propagation.

## Bindings

```
x := 10           // immutable (cannot reassign)
mu n := 5         // mutable
n = 7             // ok — n is mu
n += 1            // compound needs mu too
```

Empty literals need a type annotation so element types are known:

```
mu xs:[i64] := []
mu m:{str:i64} := {}
```

## Functions, generics, closures

```
fn add(a, b) a + b                       // expression body; params default i64
fn fib(n) {                              // block body
  if n < 2 rt n                          // braceless single-stmt if + early return
  fib(n-1) + fib(n-2)                    // trailing expr = return value
}
fn greet(name:str) -> str "Hello, " + name + "!"   // annotate non-i64

fn id<T>(x:T) -> T x                     // real generic, monomorphized to a real C fn per call
fn pick<A,B>(a:A, b:B) -> A a            // multi-param — A and B inferred independently
fn dump<T: Show>(x:T) -> str x.fmt()     // constrained — T must satisfy trait Show (see Traits)
fn map2<T,U>(xs:[T], f:fn(T)->U) -> [U] { map(xs, fn(x) f(x)) }  // generic HOF — takes a closure

inc := fn(x) x + 1                          // lambda (closure) — fn(params) body
println(inc(41))                            // 42 — stored lambda, direct call: fine
fn apply(f:fn(i64)->i64, a:i64) -> i64 { f(a) }  // fn-type param: annotate -> ret
println(apply(inc, 41))                     // 42 — stored lambda → user fn: fine
threshold := 3
println(filter([1,2,3,4,5], fn(x) x > threshold))  // [4, 5] — capture by value
```

Lambda syntax is `fn(x) body` or `fn(x) { ... }`. **Not** `|x| ...`, `(x) => ...`, or `lambda x:`.

Generic functions monomorphize into **real C functions** (one per type instantiation), so a generic fn may take a **closure parameter** (`fn map2<T,U>(xs:[T], f:fn(T)->U)`) and have a full multi-statement body with loops, locals, and early return. `std/seq.ail` is a combinator library built this way. (When you pass a lambda to such a fn over non-`i64` elements, annotate the lambda's param to match: `keep(words, fn(s:str) starts_with(s,"a"))`.)

Two real constraints of the current self-hosted compiler:
- A fn that takes a `fn(...)->R` **parameter** and returns the result of calling it must **annotate its return type** (`-> i64`) or use an explicit `rt` — implicit-return inference fails there and defaults to `void`.
- The builtins `map`/`filter`/`reduce` want the lambda **inline** (`map(xs, fn(x) x*2)`). A lambda stored in a variable works for direct calls and for your own fn-type params, but **not** as a `map`/`filter`/`reduce` argument.

## Control flow

```
// lp has FOUR forms, one keyword:
lp i in 1..10 { print(i) }      // for-in range (exclusive; ..= inclusive)
lp x in nums { total += x }     // for-in collection
lp (k, v) in m { ... }          // map iteration, tuple-destructured
mu n := 5
lp n > 0 { n -= 1 }             // while
lp { ...; if done br }          // infinite loop

// if / el — also usable as an expression
grade := if s >= 90 { "A" } el if s >= 80 { "B" } el { "C" }
```

## Structs

```
st Point { x:i64; y:i64 }              // fields separated by ; or ,
p := Point(3, 4)                        // positional construction
q := Point{ x: 0, y: 1 }                // named (order-independent)
println(p.x)                            // field access
```

## Classes (single inheritance + explicit virtual)

```
cl Shape {
  tag:i64
  fn describe(self) -> i64 { self.tag }            // method: implicit self (*Shape)
  vt fn area(self) -> i64 { 0 }                    // `vt` = virtual (vtable dispatch)
}
cl Circle : Shape {                                // `: Base` = single inheritance
  r:i64
  vt fn area(self) -> i64 { 3*self.r*self.r }      // override
  vt fn name(self) -> str { "c/" ++ super.name() } // override + super call
}
c := Circle(0, 5)        // ctor: inherited fields first, then own → (tag, r)
println(c.area())        // 75  — UFCS call; virtual → Circle::area
println(c.describe())    // 0   — inherited static method
```
- Methods take an implicit `self` (typed `*ClassName`); call with UFCS `obj.m(args)`.
- `fn` = static dispatch (by the receiver's static type); `vt fn` = **virtual** (runtime dispatch via a vtable). A same-named `vt fn` in a subclass **overrides** it; `super.m()` calls the parent's impl.
- A class IS a struct under the hood — `Name(...)` construction, `println(obj)`, `[Name]` arrays, `{str:Name}` maps and `!Name` all work for free.
- **Single inheritance only.** Lowers to plain C (vtables = function-pointer tables) → works on macOS, Linux, and Windows.

**Operator overloading (structural).** A class that defines a conventionally-named method gets the operator — no trait/keyword needed:

```
cl Vec2 { x:i64  y:i64
  fn add(self, o:Vec2) -> Vec2 { Vec2(self.x+o.x, self.y+o.y) }   // enables  a + b
  fn eq(self, o:Vec2) -> bool { self.x==o.x && self.y==o.y }      // enables  a == b
}
c := a + b        // → a.add(b)
```
Map: `+ - * / %` → `add sub mul div mod`; `== != < > <= >=` → `eq ne lt gt le ge`. Dispatch is on the LEFT operand's class; bind intermediates (`c := a+b`) before chaining `.m()`.

## Traits & generic bounds (structural — no `impl`)

```
tr Show { fn fmt(self) -> str; }                 // a bundle of required method names
cl Dog { nm:str  fn fmt(self) -> str { "dog:" + self.nm } }   // satisfies Show by HAVING fmt
fn dump<T: Show>(x:T) -> str { x.fmt() }         // bound: T must provide Show's methods
println(dump(Dog("rex")))                        // dog:rex
```
A type satisfies a trait just by defining its methods (Go-interface style). The checker enforces the bound at the call site: `dump(5)` → `type 'i64' does not satisfy bound 'Show': missing method 'fmt'`.

## Generic data types (monomorphized per use)

```
st Box<T> { val: T }                             // generic struct
st Pair<A,B> { a: A  b: B }                       // multi-param
en Option<T> { Some(v:T), None }                  // generic enum
en Result<T> { Ok(v:T), Err(e:str) }

b := Box(5)            // → Box_i64 (T inferred from the ctor arg)
p := Pair(3, "hi")     // → Pair_i64_str  (order matters: Pair_i64_str ≠ Pair_str_i64)

fn lookup(k:i64) -> Option<i64> { if k>0 { Some(k*10) } el { None } }
fn opt_get(o:Option<i64>) -> i64 { mt o { Some(v) => v; None => 0-1 } }
```
- `Some(x)` / `Ok(x)` infer the type param from the payload. A **payload-less / non-T variant** (`None`, `Err`) infers it from the **enclosing fn's declared return type** — so return `None` from a `-> Option<i64>` fn; `f(None)` at a call site (no context) is not supported.
- Use an explicit annotation (`Box<i64>` in a param/field/return) for instantiations over a non-scalar type (e.g. `Box<Vec2>`).

## Enums / ADTs (recursive OK — self-references are heap-boxed)

```
en Color { Red, Green, Blue }           // nullary variants = bare names
c := Blue

en Expr {                               // recursive ADT
  Num(v:i64),
  Add(l:Expr, r:Expr),
  Neg(x:Expr),
}
e := Add(Num(2), Neg(Num(3)))           // variant = call-style constructor

fn eval(x:Expr) -> i64 {
  mt x {                                 // match — `;` separates arms, `=>` per arm
    Num(v)   => v;
    Add(l,r) => eval(l) + eval(r);
    Neg(y)   => 0 - eval(y);
  }
}
```

`mt` is an expression; variant patterns bind positionally. Also supported: a `_` wildcard arm, per-arm **guards** (`Circle(r) if r > 10 => …`), and one-level **nested** destructuring (`Some(Pair(a,b)) => …`). The checker verifies **exhaustiveness** (guard-aware — a guarded arm doesn't count as covering), variant validity, and binding arity, reported at the `.ail` line.

## Types

- Primitives: `i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 bool str bytes void`.
  **Caveat:** integer widths are cosmetic — most lower to 64-bit; `f32`/`f64` are both `double`. Don't rely on wraparound.
- `str` is an immutable string; `bytes` is a binary buffer (may contain NUL).
- Composite: `[T]` array, `{K:V}` map (open-addressing hash), `*T` pointer, `!T` result, `(A, B, ...)` tuple, `fn(A,B)->R` closure type.

```
*T pointers:  fn bump(c:*Counter) { c.n = c.n + 1 }   // p.f auto-derefs
              bump(&ctr)                                // &x = address-of
tuples:       a, b := divmod(17, 5)                     // multi-return + destructure
```

## Bytes

`bytes` is a binary buffer distinct from `str` — survives NUL bytes, used for file I/O, network payloads, and raw encoding. Printed as `b"..."` (printable ASCII shown verbatim).

```
b := str_to_bytes("hello")     // str → bytes
println(bytes_at(b, 0))        // 104  ('h')  — by index fn
println(b[1])                  // 101  ('e')  — [] also works
sl := bytes_slice(b, 1, 4)     // slice [lo, hi)  → bytes
println(bytes_to_str(sl))      // "ell"
println(len(b))                // 5
write_file_bytes("/tmp/x.bin", b)       // write binary file
got := read_file_bytes("/tmp/x.bin")    // read → bytes (empty on missing)
println(bytes_to_str(got))              // hello
```

Text files use `read_file(path) -> str` / `write_file(path, s) -> bool`.

## Error handling: `!T` + `?`

```
fn parse(s:str) -> !i64 {
  if regex_match("^-?[0-9]+$", s) rt ok(str_to_int(s))
  err_i64("not a number: " + s)         // err_<type>(msg): err_i64/err_str/err_f64/...
}

fn sum2(a:str, b:str) -> !i64 {
  x := parse(a)?                          // ? propagates the err to this fn's return
  y := parse(b)?
  ok(x + y)
}

r := sum2("3", "4")
if is_ok(r) println(unwrap(r)) el println(err_msg(r))
```

`?` only works inside a fn whose return type is `!T`. The implicit top-level `main` is not `!T` — wrap fallible code in a `fn run() -> !i64 { ... }`. No generic `err()` — the constructor name encodes the type.

## String interpolation, UFCS, C interop

```
name := "Ada"; n := 6
println("lang=${name} sq=${n * n}")     // ${expr} holes; required braces

s.f(a)                                   // UFCS: sugar for f(s, a)
s.cstr                                   // property sugar for cstr(s)

cinc "math.h"                            // pull a C header into scope
ex fn sqrt(x:f64) -> f64                 // then bind its symbols
ex fn printf(fmt:str, ...) -> i32        // variadic extern
ex "z" fn zlibVersion() -> str           // "lib" → adds -lz

csrc "shim.cpp"                          // C++ interop (POSIX only): compile a C++
ex fn Acc_new() -> i64                    //   shim with clang++ & link it; bind its
ex fn Acc_free(h:i64)                     //   extern "C" fns. C++ objects = i64 handles
```
`csrc` links a C++ shim: expose `extern "C"` functions, declare them with `ex fn`, pass opaque C++ objects across as `i64` handles (`reinterpret_cast`). macOS/Linux only — on Windows a `csrc` program errors clearly. Two forms — external file `csrc "shim.cpp"`, or **inline in one file** with a backtick block (the C++ is extracted, compiled with clang++, and cleaned up):

```
csrc `
#include <set>
extern "C" { int64_t set_new(){ return (int64_t)new std::set<int64_t>(); } }
`
ex fn set_new() -> i64
```

## Builtins (always in scope — no `im`)

**Don't name your own functions after a builtin** (`unwrap`, `split`, `len`, `keys`, `to_str`, …) — the builtin wins and yours is silently misrouted.

### Output & conversion

| Builtin | Notes |
|---------|-------|
| `print(x)` / `println(x)` | type-dispatched; arrays of scalars and scalar maps print directly; bools always print `true`/`false` |
| `to_str(x)` | any → str; used inside `"${expr}"` interpolation |
| `int_to_str(n)` / `str_to_int(s)` | |
| `float_to_str(f)` / `str_to_float(s)` | |
| `str_to_bool(s)` | `"true"` → 1, anything else → 0 |
| `format(fmt, ...)` | printf-style — `format("%lld and %s", n, "hi")` — types must match |

### String operations

| Builtin | Notes |
|---------|-------|
| `len(s)` | byte length |
| `contains(s, sub)` | substring test → bool |
| `starts_with(s, pre)` / `ends_with(s, suf)` | |
| `index_of(s, sub)` | first position or -1 |
| `substring(s, lo, hi)` | `[lo, hi)` slice of a string |
| `to_upper(s)` / `to_lower(s)` / `trim(s)` | |
| `replace(s, old, new)` | replaces ALL occurrences |
| `repeat(s, n)` | repeat string n times |
| `pad_left(s, width, ch)` / `pad_right(s, width, ch)` | pad with a 1-char string |
| `chr(n)` | int → single-char string; `chr(65)` → `"A"` |
| `ord(s)` | first byte of string → int; `ord("A")` → 65 |
| `split(s, sep)` → `[str]` | split by separator string |
| `join(arr, sep)` | join `[str]` or `[i64]` with a separator |
| `regex_match(pat, s)` / `regex_find(pat, s)` | POSIX extended regex |
| `cstr(s)` | `str` → C `const char*` handle (for `ex fn` interop) |

### Arrays & collections

| Builtin | Notes |
|---------|-------|
| `len(a)` | array / map / bytes / str |
| `push(a, v)` / `pop(a)` | **return a fresh array** — original unchanged; re-assign: `xs = push(xs, v)` |
| `sort(a)` | **returns a fresh sorted array** — original unchanged; works for `[i64]` and `[str]` |
| `reverse(a)` / `slice(a, lo, hi)` | return fresh arrays |
| `contains(a, v)` | element membership for `[i64]` / `[str]` |
| `index_of(a, v)` | first index of v, or -1 |
| `map(arr, f)` / `filter(arr, pred)` / `reduce(arr, init, f)` | pass the lambda **inline** (`map(xs, fn(x) x*2)`) — a stored lambda doesn't work here; use `std/seq.ail` instead |
| `has(m, k)` / `keys(m)` / `values(m)` | map operations |

### Numeric

| Builtin | Notes |
|---------|-------|
| `abs(x)` | works for both `i64` and `f64` |
| `sign(n)` | -1 / 0 / 1 |
| `clamp(n, lo, hi)` | `clamp(15, 0, 10)` → 10 |

### I/O & environment

| Builtin | Notes |
|---------|-------|
| `read_line()` | one line; returns `""` at EOF **and** for a blank line — can't distinguish them; prefer `read_stdin()` for multi-line input |
| `read_stdin()` | read **all** of stdin at once |
| `read_file(path)` → `str` | UTF-8 text (strips BOM, transcodes UTF-16 on Windows) |
| `write_file(path, s)` → `bool` | write UTF-8 text |
| `read_file_bytes(path)` → `bytes` | binary read; empty bytes on missing file |
| `write_file_bytes(path, b)` → `bool` | binary write |
| `get_env(name)` → `str` | env var or `""` if unset |
| `exe_dir()` → `str` | directory of the running binary |
| `args()` → `[str]` | command-line arguments (skips `argv[0]`) |
| `flush()` | flush stdout — required before `sleep_ms` in a live counter/spinner |
| `assert(cond, msg)` | if `cond` is false: print `assertion failed: msg` to stderr + `exit(1)`; else no-op. The unit-test primitive — drive it with `ailc test` |
| `now_ms()` / `mono_ms()` / `now_us()` | wall-clock ms, monotonic ms, wall-clock µs |
| `time_iso(ms)` → `str` | **takes a ms timestamp**, not zero args — `time_iso(now_ms())` for current ISO time |
| `sleep_ms(ms)` | |

### Concurrency builtins (POSIX only; `std/thread.ail` wraps these with cleaner names)

| Builtin | std/thread.ail wrapper | Notes |
|---------|----------------------|-------|
| `thread_spawn(fn()->i64)` → `i64` | `spawn(f)` | handle is an opaque i64 |
| `thread_join(h)` → `i64` | `wait(h)` | returns thread's return value |
| — | `wait_all(hs)` | join array of handles, sum results |
| `mutex_new()` → `i64` | `mutex()` | |
| `mutex_lock(m)` / `mutex_unlock(m)` | `lock(m)` / `unlock(m)` | |
| `chan_new(cap)` → `i64` | `channel(cap)` | bounded blocking ring buffer |
| `chan_send(ch, v)` / `chan_recv(ch)` | `send(ch,v)` / `recv(ch)` | blocks when full/empty |
| `chan_close(ch)` | `close(ch)` | unblocks receivers (returns 0 after drain) |

**Shared mutable state across closures:** arrays are reference-typed — a 1-element array is the idiomatic shared mutable cell:
```
cnt := [0]           // all closures capturing `cnt` share the same backing store
m := mutex_new()
h := thread_spawn(fn() { mutex_lock(m); cnt[0] = cnt[0] + 1; mutex_unlock(m); 0 })
thread_join(h)
println(cnt[0])      // 1
```

### Socket/net (POSIX only)

`tcp_*`, `sock_*`, `tls_*` (incl. `tls_connect_host(ctx,fd,host)` — SNI for https clients), `pg_*`, `sq_*` (SQLite), `fs_*` (filesystem), `sha1` — baked into codegen, no `ex fn` decl needed. See `std/sock.ail`, `std/http.ail`, `std/tls.ail`, `std/pg.ail`, `std/sqlite.ail`, `std/fs.ail` for wrappers.

### Process (POSIX only)

`proc_fork()`, `proc_getpid()`, `proc_no_zombies()`, `proc_reap()` — raw POSIX process control; rarely needed directly.

## Standard library (`im "std/<name>.ail"`)

| module | purpose | key functions |
|--------|---------|---------------|
| `std/math.ail`  | libm + helpers | `sqrt`/`pow`/`sin`/`cos`/`log`/`floor`/`ceil(f64)`, `min`/`max`/`ipow`/`gcd`, `rand`/`srand` |
| `std/str.ail`   | string utils | `eq(a,b)`, `parse_int(s)`, `strcmp`, `atoi` |
| `std/time.ail`  | timing | `tick()`, `elapsed_ms(t)`, `since(t)`, `sleep_s(s)`, `now_iso()` |
| `std/sock.ail`  | TCP | `must_listen(host,port,banner)`, `sock_send_str_all(fd,s)`, `env_int(name,def)` |
| `std/http.ail`  | HTTP/1.1 | `http_recv_request(fd)`, `http_method`/`http_path`/`http_header(req[,name])`, `http_text`/`http_json`/`http_html(status,body)` |
| `std/json.ail`  | JSON (flat + nested) | flat: `parse_flat_obj_str`/`parse_flat_obj_int`; **nested**: `json_parse(s) -> Json`, `json_str(j)`, accessors `obj_get(j,k)`/`arr_at(j,i)`/`arr_len`/`as_int`/`as_float`/`as_str`/`as_bool`/`is_null`/`json_keys` |
| `std/csv.ail`   | CSV reader/writer | `csv_parse(text) -> [Row]` (quoted fields, CRLF/LF), `csv_emit(rows)`, `csv_field(f)`, `row_map(header,r) -> {str:str}` (a `Row` wraps `cells:[str]`) |
| `std/tls.ail`   | TLS I/O | `tls_send_str_all(ssl,s)`, `tls_send_all(ssl,bytes)` |
| `std/pg.ail`    | Postgres | `pg_must_connect(dsn)`, `pg_one(conn,sql)`, `pg_first_col(conn,sql) -> [str]`, `pg_print_table(res)`. **Prepared statements (injection-safe):** `pg_query_one(conn,sql,params:[str])->str`, `pg_query_col(...)->[str]` with `$1,$2,…` placeholders (values bound, not interpolated). |
| `std/redis.ail` | Redis | `redis_connect(host,port)`, `redis_get`/`redis_set`, `redis_incr`, `redis_del`, `redis_ping` |
| `std/ws.ail`    | WebSocket | `ws_handshake_response(key)`, `ws_send_text(fd,p)`, `ws_recv_text(fd)`, `b64_encode(bytes)` |
| `std/thread.ail`| OS threads (pthread, POSIX) | Wrappers over builtins: `spawn(f)`/`wait(h)`/`wait_all(hs:[i64])`, `mutex()`/`lock(m)`/`unlock(m)`, `channel(cap)`/`send(ch,v)`/`recv(ch)`/`close(ch)`. Import for the clean names; the builtins (`thread_spawn`, `mutex_new`, `chan_new`, …) work without the import. |
| `std/seq.ail`   | generic combinators (`\|>`-friendly) | `any`/`all`/`count`/`find_index`/`take`/`drop`/`keep`/`map_to`/`flat_map`/`fold`/`sort_by`/`for_each`/`zip_with`/`group_by(xs, key)->{str:[T]}` — each takes a passed closure; annotate the lambda param when elements aren't `i64`; iterate `sort(keys(g))` for stable group output |
| `std/web.ail`   | Express-style web framework (POSIX) | `web_new()`, `web_get`/`web_post`/`web_put`/`web_delete(&app, pat, fn(r:Req)->str)`, `web_use(&app, mw)` middleware, `:id` path params via `req_param(r,"id")`, headers via `req_header(r,"Authorization")`, `web_handle(&app, raw)->resp` (socket-free, testable), `web_listen(&app, host, port)` (live server). Handlers are closures in the routes table. |
| `std/jwt.ail`   | JWT HS256 (POSIX) | `jwt_sign(payload_json, secret)->str`, `jwt_verify(token, secret)->bool`, `jwt_payload(token)->str`, `jwt_claim(token, key)->str`; `b64url_encode`/`b64url_decode_str`. Real interoperable tokens (byte-identical to PyJWT). |
| `std/mysql.ail` | MySQL/MariaDB (libmysqlclient, POSIX) | `mysql_must_connect(host,user,pass,db,port)`, `mysql_one(c,sql)->str`, `mysql_rows(c,sql)->[MRow]`, `mysql_exec`/`mysql_escape`/`mysql_close`. **Prepared statements (injection-safe, `mysql_stmt_*`):** `mysql_run(c,sql,params:[str])->affected`, `mysql_query_one(c,sql,params)->str` with `?` placeholders (values bound). Opt-in (only programs that use it link `-lmysqlclient`); needs the client lib + a server. (Postgres: `std/pg.ail`.) |
| `std/sqlite.ail` | SQLite (libsqlite3, POSIX) | `sqlite_must_open(path)` (`":memory:"` works), `sqlite_one(c,sql)->str`, `sqlite_rows(c,sql)->[SqRow]`, `sqlite_exec`/`sqlite_changes`/`sqlite_last_id`/`sqlite_escape`/`sqlite_print_table`/`sqlite_close`. **Prepared statements (injection-safe — prefer these for user input):** `sqlite_query(c,sql,params:[str])->[SqRow]`, `sqlite_run(c,sql,params)->changes`, `sqlite_query_one(c,sql,params)->str` — use `?` placeholders, values are BOUND not interpolated (`sqlite_run(c,"INSERT INTO t(x) VALUES(?)",[v])`). For a no-param query use `sqlite_one`/`sqlite_rows` (a bare `[]` types `[i64]`, not `[str]`). Opt-in `-lsqlite3` (macOS SDK; Linux needs libsqlite3-dev). |
| `std/http_client.ail` | HTTP(S) client (POSIX) | `http_get(url)->HttpResp{status,head,body}`, `http_post(url,ctype,body)`, `http_resp_header(r,name)`, `http_dechunk` (chunked TE), `http_url_parse`. https via SNI (`tls_connect_host`); importing it links OpenSSL; status `-1` on connect/handshake failure; bodies are `str` (NUL truncates). Don't also `im "std/http.ail"` directly. |
| `std/fs.ail`    | filesystem (POSIX) | builtins (no import needed): `fs_mkdir`/`fs_rmdir`/`fs_unlink`/`fs_rename`/`fs_exists`/`fs_is_dir`->bool, `fs_size`/`fs_mtime`->i64 (-1 missing; mtime whole seconds), `fs_list_dir(p)->[str]` (**order varies — `sort()` it**). Module adds `fs_join`/`fs_basename`/`fs_dirname`/`fs_mkdir_p`. |
| `std/encoding.ail` | base64 / base64url / hex (pure) | `b64_encode(bytes)->str`/`b64_encode_str(s)`, `b64_decode_str`, `b64url_encode`/`b64url_encode_str`/`b64url_decode_str`, `hex_encode`/`hex_encode_str`/`hex_decode_str`. Encoders take `bytes`; decoders return `str` (a decoded NUL truncates). Alias it (`im … as enc`) if you also `im` jwt/ws (they carry private copies). |
| `std/url.ail`   | URL encode + query strings (pure) | `url_encode(s)`/`url_decode(s)` (percent, `+`→space), `qs_parse(q)->{str:str}`, `qs_get(m,k,dflt)`, `qs_build(keys,vals)->str`. |
| `std/uuid.ail`  | v4-shaped UUIDs (sha1→OpenSSL, POSIX) | `uuid_v4()->str` (36-char, version 4), `uuid_nil()`, `uuid_is_v4(s)->bool`. **Not** a CSPRNG — request-ids/log-keys, not security tokens. |
| `std/args.ail`  | CLI arg parser (pure) | `args_parse(argv:[str])->Parsed`, then `arg_flag(p,name)->bool`, `arg_opt(p,name,dflt)`, `arg_int(p,name,dflt)`, `arg_pos(p,i)`, `arg_count(p)`. Handles `--flag`, `--k=v`, `--k v`, `-f`, `--`, positionals. Pass it `args()`. |
| `std/log.ail`   | leveled logfmt logging (pure) | `log_info`/`log_warn`/`log_error`/`log_debug(msg)`, `log_kv(level,msg,keys,vals)`; levels `LDebug`/`LInfo`/`LWarn`/`LError`; pure `log_format(ts,level,msg,keys,vals)->str` (pass a fixed `ts` → testable). |
| `std/env.ail`   | typed env + `.env` (pure) | `env_str(name,dflt)`, `env_bool(name,dflt)` (`1`/`true`/`yes`/`on`), `dotenv_parse(text)->{str:str}` (`KEY=VAL`, `#` comments, quoted values), `dotenv_get(m,name,dflt)`. (`env_int` is in the auto-imported `std/sock.ail`.) |

`std/math.ail` and `std/sock.ail` are auto-imported. The net/TLS/PG/Redis/thread builtins are baked into codegen, so the modules are thin convenience wrappers.

**Resolving `std/`.** `im "std/…"` is searched in three places, in order: (1) beside your source file, (2) `$AILANG_STD/std/…`, (3) beside the `ailc` binary. The installer sets `AILANG_STD` — and it must point at the directory that **contains** `std/` (e.g. the repo root), **not** at `std/` itself (a common mistake: `AILANG_STD=…/std` makes it look for `…/std/std/math.ail`). So once `AILANG_STD` is set, `im "std/math.ail"` resolves from any directory; otherwise put a `std/` next to your `.ail` or next to `ailc`. An import that resolves nowhere is a hard error (it no longer silently drops).

**Namespaced imports.** `im "path" as m` aliases a module; call its fns qualified as `m.fn(...)`. The module's functions are isolated under the alias, so two modules can define the same name without colliding (`im "a.ail" as a` + `im "b.ail" as b` → `a.run()` / `b.run()`). Plain `im "path"` still splices unqualified.

## Top gotchas (what an LLM gets wrong)

1. **`ailc run src.ail` compiles + runs in one step; `ailc check src.ail` type-checks only** (add `--json` for machine-readable errors). Plain `ailc src.ail [out]` only builds a binary. (The old Rust `ailangc run` is a different, archived compiler.)
2. **`el` not `else`; `rt` not `return`; `lp` not `for`/`while`; `en` not `enum`; `st` not `struct`.**
3. **Reassignment needs `mu`.** `x := v` is const; use `mu x := v` then `x = v`.
4. **String interpolation is `"${expr}"`** — braces required. Not `{}`, `%s`, or `$var`.
5. **No ternary** — use `if c { a } el { b }` as an expression.
6. **Errors are `!T` + `ok()`/`err_<type>()`/`?`**, not exceptions. `?` only inside an `!T` fn.
7. **Enum variants are call-style** (`Add(l,r)`, bare `Red`) and matched with `mt x { V(b) => ...; }` using `;` separators.
8. **Lambdas are `fn(x) body`** — no `|x|` / `=>` / `->` arrow forms.
9. **Implicit `main`** — don't wrap a top-level script in `fn main`.
10. **Integer widths are cosmetic** (stored 64-bit). The type checker is conservative but real — it reports confident mistakes at the `.ail` `line:col` (type/`!T` mismatches, `mt` exhaustiveness/variants/arity, call & generic arity, `<T: Trait>` bounds, generic-instance mismatches), **all errors in one run**, with *"did you mean?"* spelling suggestions. It's not a full type system, so some mistakes still surface as C-compiler errors.
11. **`map`/`filter`/`reduce` need an inline lambda** — `map(xs, fn(x) x*2)`, not a lambda stored in a variable. (`std/seq.ail`'s `keep`/`map_to`/`fold` accept a *passed/stored* closure where the builtins won't.) And a non-generic fn that returns the result of calling a `fn(...)->R` parameter must **annotate its return type** (`-> i64`) or use explicit `rt`.
12. **`sort` / `push` / `pop` / `reverse` / `slice` return a fresh array — the original is unchanged.** Re-assign: `xs = push(xs, v)`, `ys = sort(xs)`. Forgetting the re-assign is a silent no-op.
13. **Lambda whose block body ends with an assignment or a loop** compiles correctly and implicitly returns `0`. This is not an error, but: `f := fn() { arr[0] = arr[0] + 1 }` — the lambda returns `i64` (0), so an fn-type annotation like `fn()->i64` is required if you pass it as a typed param.
14. **`format(fmt, args…)` is printf-style with manual type matching** — `%lld` for i64, `%s` for str, `%g` for f64. Mismatch is a runtime crash, not a compile error. Use string interpolation `"${e}"` instead when types are mixed or unknown.

## Worked examples

```
// hello — implicit main
println("hello, AiLang")
```

```
// fizzbuzz — lp range + mt tuple patterns
lp i in 1..16 {
  mt (i%3, i%5) {
    (0,0) => println("FizzBuzz");
    (0,_) => println("Fizz");
    (_,0) => println("Buzz");
    _     => println(i);
  }
}
```

```
// recursive fib
fn fib(n) {
  if n < 2 rt n
  fib(n-1) + fib(n-2)
}
println(fib(30))                          // 832040
```

```
// arrays, maps, higher-order
nums := [5, 2, 8, 1, 9]
println(len(nums))                        // 5
println(reduce(nums, 0, fn(a, b) a + b))  // 25
mu counts:{str:i64} := {}
lp w in ["a", "b", "a"] { counts[w] = counts[w] + 1 }
println(counts["a"])                      // 2
```

```
// recursive ADT + match expression
en Tree { Leaf(v:i64), Node(l:Tree, r:Tree) }
fn sum(t:Tree) -> i64 {
  mt t {
    Leaf(v)   => v;
    Node(l,r) => sum(l) + sum(r);
  }
}
println(sum(Node(Leaf(1), Node(Leaf(2), Leaf(3)))))   // 6
```

```
// !T result + ? propagation
fn half(n) -> !i64 {
  if n % 2 == 0 rt ok(n / 2)
  err_i64("odd: ${n}")
}
fn run() -> !i64 {
  a := half(8)?
  b := half(a)?
  ok(a + b)
}
r := run()
if is_ok(r) println(unwrap(r)) el println(err_msg(r))   // 6
```

```
// tiny HTTP server (std/sock auto-imported; im http)
im "std/http.ail"
fd := must_listen("127.0.0.1", 8080, "listening on :8080")
lp {
  cli := tcp_accept(fd)
  req := http_recv_request(cli)
  sock_send_str_all(cli, http_text(200, "you asked for ${http_path(req)}\n"))
  sock_close(cli)
}
```

When unsure, mimic the shape of programs in `examples-selfhost/*.ail` rather than translating literally from another language.

## Bundled examples (`skill/examples/`)

Focused, runnable `.ail` files covering features not fully shown in the inline snippets above:

| File | What it covers |
|------|---------------|
| [`bytes.ail`](examples/bytes.ail) | `bytes` type: `str_to_bytes`, `bytes_at`, `b[i]`, `bytes_slice`, `bytes_to_str`, `read_file_bytes`, `write_file_bytes`, text `read_file`/`write_file` |
| [`strings.ail`](examples/strings.ail) | Full string builtin set: `contains`, `starts_with`/`ends_with`, `index_of`, `substring`, case/pad/replace/repeat, `chr`/`ord`, `split`/`join`, `format` |
| [`threads.ail`](examples/threads.ail) | `std/thread.ail` wrappers (`spawn`/`wait`/`wait_all`, `mutex`, `channel`/`send`/`recv`/`close`); 1-element array as shared mutable cell |
| [`pipeline.ail`](examples/pipeline.ail) | ETL: `std/csv.ail` → `std/seq.ail` combinators → `std/json.ail` output; `\|>` pipe style; lambda param annotations for non-`i64` elements |
| [`web_jwt.ail`](examples/web_jwt.ail) | `std/web.ail` routes + `:param` + middleware + `std/jwt.ail` Bearer auth; `web_handle` for socket-free testing |

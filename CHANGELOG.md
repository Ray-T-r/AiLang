# Changelog

All notable changes are documented here. Loosely follows
[Keep a Changelog](https://keepachangelog.com/) and
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed

- **String interpolation no longer corrupts types across `${…}` sites.** Each
  `${expr}` was re-parsed from a standalone slice, giving its AST nodes spans
  relative to that slice (starting at 0) — so the same embedded text in two
  places collided on identical spans, and sema (which keys side tables by span)
  let the second site's types clobber the first's. Two `fn`s each doing
  `"n=${len(xs)}"` over different element types would miscompile. Spans are now
  rebased into the enclosing string literal's byte range, making every
  interpolation site globally unique. New e2e: `nested_ast`.
- **Variant-pattern bindings get their real field type.** `mt s { SIf(c, t, e) … }`
  now types `t`/`e` from the variant's declared fields (e.g. `[Stmt]`) instead
  of `Unknown`, so `len(t)` and other type-directed dispatch work on match-bound
  aggregates. (sema `bind_pattern` resolves field types via the enum table.)

### Language

- **Empty `[]` literals honor the element type, and `slice`/`reverse` work over
  aggregate arrays.** Now also covers empty `[str]`/`[bytes]` (not just
  `[Struct]`): an annotated `mu xs:[str] := []` or a push-pinned `mu xs := []`
  lowers to the right array type. `mu toks:[Token] := []` (explicit annotation) and
  `mu toks := []` pinned by a later `push(toks, Tok(..))` no longer miscompile to
  `[i64]`; sema records the annotation onto the literal and back-propagates the
  element type from the `push`. `slice(xs, lo, hi)` and `reverse(xs)` gained
  per-type helpers for `[Struct]`/`[Enum]`. (`sort` over aggregates still
  unsupported — needs an element comparator.) Found while writing the
  AiLang-in-AiLang bootstrap compiler under `selfhost/`. New e2e: `empty_agg_arr`.

- **Recursive ADTs — the foundation for an AST.** Enum variants may refer to
  their own enum, directly (`en Expr { Num(v:i64), Add(l:Expr, r:Expr) }` — the
  self-referential payload is boxed on the GC heap automatically), through an
  array (`Branch(kids:[Tree])`), and **mutually across two enums**
  (`en Expr`↔`en Stmt`, detected by a value-type cycle search). Codegen
  forward-declares each aggregate as a named struct tag, then emits bodies, so
  pointers and `[T]` child lists can close the cycle.
- **Generics over user types.** `fn id<T>(x:T)` / `fn fst<T>(a:T, b:T)` now
  monomorphize when `T` is a struct or enum, not just a primitive. The dispatch
  macro became multi-argument (so multi-param generics work at all), instances
  mangle on the type name, and a generic-call result binds to a local via C
  `__auto_type` inference.
- **Arrays *and* string-keyed maps of structs/enums.** `[Point]`, `[Expr]`,
  `[Op]` and `{str:Sym}` support construction, indexing + field access,
  iteration (`lp x in xs`, `lp (k,v) in m`), and `len`/`push`/`pop` /
  `has`/`keys`/`values` via on-demand per-type templates. With recursive ADTs
  this yields real list-children ASTs (`[Stmt]`, `[Expr]`) and symbol tables.
- **`if` / `mt` / `{...}` blocks work as expressions.** `x := if c {a} el {b}`,
  `x := mt v { … }`, and `y := { …; tail }` lower to GNU statement-expressions
  instead of the previous silent-`0` stub. The arm/branch value can be any type,
  including a struct/enum.
- **`!T` results + `?` over aggregate T.** A fallible function may return
  `!Struct` / `!Enum` (`fn parse() -> !Ast`); `ok`/`err`/`unwrap`/`is_ok`/
  `is_err`/`err_msg` and `?` propagation all work, via on-demand
  `ailang_result_<T>` types and re-emitted `_Generic` dispatch macros.
- **Non-exhaustive `mt` on an enum now warns** (permissive — lists the missing
  variants, still compiles), catching the "added a variant, forgot an arm" bug.

- **`${...}` interpolation now takes any expression, not just bare idents.**
  Field access (`${p.x}`), calls (`${to_upper(s)}`), indexing (`${nums[0]}`)
  and arithmetic (`${age + 1}`) are re-parsed inline and wrapped in `to_str`,
  so they no longer have to be hoisted into a local first. A non-expression
  like `${not an ident here}` still stays verbatim, as does any `"`-containing
  expression (the outer quote closes early — hoist `${m["k"]}` into a local).
  Interpolation is now a full replacement for `format(...)` on plain
  string-building, and shorter (no `%lld`/`%s` placeholders, no trailing args).

- **C FFI now consumes headers and links libraries.** `cinc "zlib.h"` emits
  `#include <zlib.h>` into the generated C (making the header's
  macros/typedefs/structs visible), and `ex "lib" fn …` both declares the
  extern and links `-llib` — e.g. `ex "z" fn zlibVersion() -> str` prints the
  zlib version with no driver changes. Unknown type names pass through to C
  verbatim, so a header typedef can be named directly in a signature. Still
  hand-written signatures (no bindgen); `#define` macros aren't visible as
  AiLang names.

- **Empty `{}` / `[]` infer `(K,V)` / element type from later usage.** A
  binding like `counts := {}` followed by `counts[w] += 1` inside a `lp`
  body now correctly picks up `{str:i64}` from the loop variable's and
  value's static types. Previously this fell back to `{i64:i64}` and
  silently treated string keys as pointers; the workaround was
  `{"_seed_": 0}` type-pin literals or explicit `:{str:i64}` annotations.
  Both are no longer needed for the common case.

### Language

- **C struct fields are now readable/writable through pointers, and `&x`
  yields a real pointer type.** Added a `Ty::Ptr` to the type system: `*T`
  in a signature and `&x` (address-of) now infer to `Ptr(T)` instead of
  collapsing to `int64_t`, and `p.field` on a pointer codegens as `p->field`
  (by-value `s.field` still uses `.`). Concretely this enables (a) reading
  fields of a typedef'd C struct returned by value (`div(17,5).quot`),
  (b) passing opaque struct pointers like `*FILE`/`*sqlite3` around, and
  (c) **pointer out-params for AiLang's own `st` structs** —
  `fn bump(c:*Counter) { c.value = c.value + 1 }` + `bump(&ctr)` mutates the
  caller's struct. New example/e2e: `ptr_fields`. Still unsupported: tag-only
  C structs (`struct tm` with no typedef) have no AiLang spelling.

### Fixed

- **In-place array element assignment `arr[i] = v` now codegens correctly.**
  The assignment path emitted `ailang_at(arr, i) = v`, but `ailang_at` (the
  read accessor) expands to a by-value `_Generic` call and is not an lvalue,
  so *every* `[i64]`/`[str]` index-assignment failed to compile ("expression
  is not assignable") — at top level and inside functions alike. Codegen now
  recognizes an array-typed index target and writes through the shared data
  buffer (`arr.data[i] = v`). Because the buffer pointer is shared across the
  array struct's value copies, mutation also persists across function-call
  boundaries, so in-place algorithms like quicksort/bubble-sort that pass the
  array to a `swap(arr, i, j)` helper now work. Map index-assignment
  (setter-based) was already correct and is unchanged. New e2e:
  `inplace_sort` (recursive in-place quicksort via a `swap` helper).

### Benchmarks

- **wordcount: 67 → 57 tokens** (-15%), driven by the inference change
  above plus switching `counts[w] = counts[w] + 1` to `counts[w] += 1`.

- **jsonapi: 100 → 96 tokens** (-4%), from replacing the
  `format("...%lld...", i, i, age)` line with string interpolation
  `"{\"id\":${i},...,\"age\":${age}}"`. AiLang's three-program total is now
  **257 — the lowest of any language measured**, edging out Python's 258
  (it was neck-and-neck at 261 before).

### Performance

- **GC no longer scans pointer-free numeric heap.** The `[i64]` array
  backing stores and the `{i64:i64}` / `{str:i64}` map key/value/occupied
  arrays now allocate via `GC_malloc_atomic` instead of `GC_MALLOC`, so
  the Boehm collector marks them live without scanning their contents for
  pointers. A `[i64]` value or `{i64:i64}` map is now entirely scan-free.
  On a GC-stress benchmark (256 MB live numeric heap) this cut total
  world-stopped marking from **37 ms to 1 ms** and moved the whole live
  set out of the "pointers" class (`262 MB pointers → 262 MB other`). It
  also removes a latent false-retention hazard where an integer payload
  that happened to look like a heap address could keep garbage alive.
  `[str]` / `{str:str}` stores still hold real pointers and stay scanned.

- **The TLS and Postgres runtime is now emitted on demand.** The C prelude
  previously baked `<openssl/ssl.h>` + the `tls_*` / `sha1` helpers and
  `<libpq-fe.h>` + the `pg_*` helpers into *every* program, so even
  `println("hi")` parsed the (heavy) OpenSSL headers and link-depended on
  libssl/libcrypto. Codegen now scans the generated body and prepends the
  TLS / Postgres sections only when the program calls into them. For the
  common case that uses neither, **clang time drops ~31%** (66 → 45 ms on a
  trivial program; OpenSSL's headers alone were ~24 ms) and the binary no
  longer links openssl or libpq at all — they revert to truly optional
  native deps. TLS/Postgres programs are unaffected (sections + link flags
  appear exactly as before). Detection is a coarse text scan of the emitted
  C, mirroring how the driver already decides `-lssl` / `-lpq`.

### Tooling

- **The generated `.c` / `.ll` is now a temporary build artifact.** `compile`
  and `run` previously left the transpiled C (or LLVM IR) sitting next to the
  binary forever; it's now deleted automatically once the native binary
  links. Pass `--keep-c` to `compile`/`run` to retain it (the old behavior),
  and `--emit-c` still prints the C to stdout without invoking clang. A
  *failed* compile always keeps the file so clang's errors can be traced back
  into the generated source.

## [0.2.0] — 2026-05-27

Major language + stdlib expansion. **55 automated tests pass; 25 end-to-end
example programs compile and run correctly.**

### Language

- **ADTs / tagged unions** via `en Name { Variant, Variant(field:T), … }` —
  unit and payload variants, constructor calls, destructuring `mt` arms.
- **`!T` result type + `?` propagation** — `ok(v)` / `err_T(msg)` / `unwrap`
  / `is_ok` / `is_err` / `err_msg`; `expr?` propagates the first error to
  the enclosing fn's return.
- **First-class lambdas with capture** — lifted to top-level static fns,
  runtime closure value is a `{fn, env}` fat pointer; env is GC-allocated;
  by-value capture; str-returning closures supported.
- **Real generic monomorphization** — `fn id<T>(x:T)` specialized per
  call-site type via `_Generic` (T = primitive; `[T]` / `{K:V}` shapes
  unify).
- **User structs** — `st Point { x:i64; y:i64 }` + struct literal +
  `.field` access + auto pretty-print via `println(...)`.
- **Implicit return from tail-position `if`/`mt`** — no explicit `rt`
  needed on each branch.
- **Map iteration via tuple destructure**: `lp (k, v) in m`.
- **Mutable params**: `fn f(mu n) { n += 1; n }`.
- **New map combos**: `{str:i64}` and `{str:str}` (in addition to
  `{i64:i64}`).

### Builtins (always in scope)

- I/O & process: `read_file`, `write_file`, `read_line`, `args`, `exit`,
  `get_env`.
- String ops: `contains`, `starts_with`, `ends_with`, `index_of`,
  `to_upper`, `to_lower`, `trim`, `substring`, `replace`, `split`,
  `repeat`, `pad_left`, `pad_right`, `chr`, `ord`.
- Conversions: `int_to_str`, `str_to_int`, `float_to_str`, `str_to_float`,
  `str_to_bool`, `format(fmt, ...)`.
- Containers: `push`, `pop`, `sort`, `reverse`, `slice`, `keys`, `values`,
  `join`; polymorphic `contains` / `index_of` over str + arrays.
- Higher-order: `map`, `filter`, `reduce` for `[i64]` and `[str]`.
- Math: `abs_i64`, `abs_f64`, `sign`, `clamp`; libm `sqrt`/`pow`/`sin`/
  `cos`/`tan`/`log`/`log2`/`log10`/`exp`/`floor`/`ceil` via
  `im "std/math.ail"` (libm linked unconditionally).
- Regex: `regex_match`, `regex_find` (POSIX extended).

### Stdlib

- `std/json.ail` — flat JSON parser (`parse_flat_obj_str` →
  `{str:str}`, `parse_flat_obj_int` → `{str:i64}`) written entirely in
  AiLang.
- `std/math.ail` expanded with libm wrappers.

### Backend

- LLVM IR text backend exposed via `--backend ir` (parallel codegen path
  to the C transpile; currently covers `println(literal)` + implicit
  main, grows incrementally).

## [0.1.0] — 2026-05-26

First public release. The compiler runs the full
lex → parse → sema → codegen → `clang -O2` pipeline; **42 automated tests
pass and 12 end-to-end example programs compile and run correctly**.

### Language

- Functions with implicit return of the trailing expression
- `if` / `el` with braceless single-statement bodies
- Unified `lp` loop — same keyword for `for-in` ranges and `while` conditions
- `mt` match with tuple patterns
- `|>` pipe operator
- Variable bindings via `mu? ident := expr`, type-inferred
- Compound assignment: `+=`, `-=`, `*=`, `/=`, `%=`
- Implicit `main` — top-level statements auto-wrap into one
- Optional type annotations (default `i64`)
- Unified `+` for arithmetic and string concatenation
- Arrays (`[i64]`, `[str]`) and maps (`{i64:i64}`) on Boehm GC
- Modules via `im "path"` with cycle-safe import resolution
- Direct FFI to libc via `ex fn …`
- Generics syntax (currently monomorphic over `i64`)

### Tooling

- `ailangc` CLI: `tokens`, `parse`, `compile`, `run`, plus `--emit-c` and
  experimental `--backend ir`
- Boehm GC auto-discovery via `$BDW_GC_PREFIX` → `pkg-config` → `brew`
- ariadne-powered colored diagnostics with caret pointers
- AiLang skill at [`skill/SKILL.md`](skill/SKILL.md) for Claude Code

### Distribution

- Pre-built binaries on the GitHub release page:
  `ailangc-macos-aarch64`, `ailangc-windows-x86_64.exe`
- One-line installers ([`install.sh`](install.sh) for macOS/Linux,
  [`install.ps1`](install.ps1) for Windows)
- GitHub Actions release workflow builds all four platforms (macOS arm64
  + x86_64, Linux x86_64, Windows x86_64) on `v*.*.*` tag push starting
  with the next release

[Unreleased]: https://github.com/Ray-T-r/AiLang/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/Ray-T-r/AiLang/releases/tag/v0.2.0
[0.1.0]: https://github.com/Ray-T-r/AiLang/releases/tag/v0.1.0

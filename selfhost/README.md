# selfhost — AiLang compiler, written in AiLang

A bootstrap compiler for AiLang, **written in AiLang itself**. This is the
self-hosting milestone: the language compiling (a subset of) itself, through the
same lex → parse → C-codegen → `clang` → native-binary pipeline the Rust
compiler uses, but with every stage authored in `.ail`.

It exists because the language recently gained the capabilities a compiler's
core data structures need — **recursive ADTs** (the AST node), **arrays of
structs** (the token stream), **`{str:Sym}` maps** (symbol tables), and
**`ex fn system`** (to invoke `clang`). This directory is the proof those
capabilities compose into a real compiler.

## Status

**✅ STRICT FIXPOINT — the compiler compiles its own source.** `main.ail` is no
longer just a subset toy: it compiles `main.ail`. The three-stage proof holds —

```
stage1 = ailangc(main.ail)        # bootstrap, built by the Rust compiler
stage2 = stage1(main.ail)         # first self-compile
stage3 = stage2(main.ail)
diff stage2.c stage3.c            # EMPTY — byte-identical (3290 lines)
```

so the compiler is its own fixpoint (the strongest self-hosting standard). The
self-built stage-2 compiler is *also* re-checked against every sample, so
"byte-identical" can't pass by both stages being equally broken. Reproduce:

```bash
bash selfhost/verify.sh    # proof A: 12 samples == ailangc; proof B: stage2 == stage3
```

Each feature was grown incrementally and verified against the real `ailangc` (the
Rust compiler on `$PATH`).

| stage | file | status |
|-------|------|--------|
| lexer | `lexer.ail` | ✅ done — standalone token dump (v0 subset) |
| parser (recursive `Expr` AST) | `parser.ail` | ✅ done — standalone tree-eval (v0 subset) |
| **the actual compiler** | `main.ail` | ✅ **self-hosting** — compiles itself to a byte-identical fixpoint |
| end-to-end verify | `verify.sh` | ✅ done — sample fidelity **+ strict-fixpoint** check |

`main.ail` is where the live development happens; `lexer.ail`/`parser.ail` are
the original standalone v0 harnesses kept for illustration.

`lexer.ail` and `parser.ail` are standalone, independently-runnable harnesses
(`ailangc run selfhost/lexer.ail` dumps tokens; `…/parser.ail` evaluates the
parsed tree). `main.ail` is the actual compiler — it re-bundles the lexer +
parser (the bootstrap has no cross-file `im` wired for these yet) and adds
codegen + the clang driver.

## The source subset

A subset, but a **self-complete** one: it contains exactly what `main.ail` itself
uses, which is why the compiler can compile its own source. On top of the earlier
slices (functions, control flow, structs, arrays, strings) it now has the data and
control machinery a compiler needs:

- **`{str:V}` maps** (`{}` / `m[k]` / `m[k]=v` / `has` / `m[k] ?? d`) — monomorphic
  `map_ss`/`map_si` **open-addressing** runtime (same FNV-1a / linear-probe layout as
  `ailangc`, so iteration order matches), **reference-semantic** so writes through a
  `*Syms` pointer field persist.
- **`*T` pointers** + `&x` + `->` — mutable parser state (`fn adv(p:*P)`).
- **`en` enums + `mt` match** — recursive ADTs with **heap-boxed** self-referential
  fields (the `Expr`/`Stmt` AST), `mt` as a value (statement-expression).
- **`if` as an expression** (`x := if c { a } el { b }`) and **tail returns** —
  the last expression of a function (or `if`/`mt` branch) is its return value.
- **`+=`**, `;` statement separators, `br`/`ct` (break/continue), nullary variant
  constructors, declaration hoisting (so a name reused across sibling blocks maps
  to one C local).
- **builtins**: `substring`, `str_to_int`, `int_to_str`, `to_str`, `len`, `push`,
  `has`, `slice` (`[lo,hi)`), `reverse`, `sort` (i64/str), `keys`, `values`,
  `read_file`, `write_file`, `args`, `exit`, `println`.
- **string interpolation** `"hi ${name}, ${n * n}"` — desugars to a `scat` chain;
  each `${…}` hole is stringified with `to_str`.
- **map iteration** `lp (k, v) in m { … }` plus `keys(m)` / `values(m)` and whole-map
  `println(m)` — hash-slot order, identical to `ailangc`'s (open-addressing, so they
  match byte-for-byte).
- **`!T` error propagation**: `-> !i64` results, `ok(x)` / `err_i64("…")`, the `e?`
  propagation operator, and `unwrap` / `is_ok` / `is_err` / `err_msg`. Each `!T`
  lowers to a monomorphic `res_<T>` tagged struct.
- **f64 floats**: `3.14` literals, `+ - * /` and comparisons, printed with `%g`
  (matching AiLang — `5.0`→`5`, `12.56636`→`12.5664`).
- **closures**: `fn(x) x*2` lambdas to `map` / `filter` / `reduce`, INLINED into
  the generated loop so captured outer vars stay in scope (no env/fn-pointer).
- **generics**: `fn id<T>(x:T) -> T`, monomorphized by INLINING each call with the
  params bound to the call's concrete types (primitives, str, structs, enums) —
  the same trick ailangc uses with `__auto_type`.
- **`im "path"`** imports — a driver pre-pass inlines the imported file's source
  (relative to the importer, with a cycle guard) before compilation.
- **C interop**: **`ex [\"lib\"] fn`** externs (e.g. `ex fn system` to invoke
  `clang`; `ex "z" fn zlibVersion() -> str` links `-lz`) and **`cinc "h"`** to pull
  in a C header. Link flags ride a `// @links:` marker the driver reads back.

Earlier slices, still here:

- **`[T]` arrays**: literals `[a, b, …]` and empty `[]`, `xs[i]` indexing,
  `xs[i] = v` index-assignment, `push(xs, e)` / `len(xs)`, and `lp x in xs`
  iteration. The element type comes from an annotation (`mu xs:[Tok] := []`) or
  is inferred from a `push`. Codegen emits a monomorphic C array runtime
  (`arr_<T>` struct + new/push/get/len) per element type — `[i64]`, `[str]`, and
  one per declared struct.
- **`st Name { f:T; … }` structs** + `Name(args)` constructors + `x.field`
  access + struct-typed params and returns. Codegen tracks a *type name* per
  binding/function/field ("i64" / "str" / a struct name / "[T]" for arrays),
  emits `typedef struct {…} s_Name;` + `mk_Name(…)`, and infers each fn's return
  type from its first `rt`. This is a small type system, not just int/str.

- `fn name(p1, p2, …) { … }` function declarations + calls + recursion
- `rt`, `if … { … } el if … { … } el { … }`, `lp <expr> { … }` (while)
- `mu name := <expr>` / `name := <expr>` (declare) and `name = <expr>` (reassign)
- **string literals** `"…"`, str-typed variables, `println` of strings, and
  **string concatenation** `a + b` (faithful to AiLang: `+` joins strings but
  does *not* auto-coerce an int — wrap ints in `to_str(n)`). Codegen tracks each
  name's value-kind to pick `const char*` vs `int64_t` and `%s` vs `%lld`, and
  routes string `+` through a runtime `scat`/`i2s` C prelude.
- **bool / logic**: `true` `false` `&&` `||` `!`
- `println(<expr>)`; identifiers; integer literals; `//` line comments
- operators `+ - * / %`, comparisons `< > <= >= == !=`, logic `&& || !`, unary
  `! -`, with full precedence (`or < and < cmp < add < mul < unary`)
- top-level statements form an implicit `main()`

**The road to self-compilation is now walked** — pointers, maps, enums+match and
the rest above all landed, which is what let `main.ail` (~1640 lines) compile
itself to a byte-identical fixpoint. It remains a *subset* compiler: it compiles
`main.ail` and the samples, not arbitrary AiLang (no string interpolation,
ternary, `slice`/`sort`, map iteration, generics, …). The Rust compiler is
~10,600 lines and is still the reference oracle `verify.sh` diffs against.

Examples the compiler compiles to native binaries matching `ailangc run`:

```
fn fib(n) {
  if n < 2 { rt n }
  rt fib(n - 1) + fib(n - 2)
}
println(fib(20))            // 6765
```

```
fn fizzbuzz(n) {
  mu i := 1
  lp i <= n {
    if i % 15 == 0      { println("FizzBuzz") }
    el if i % 3 == 0    { println("Fizz") }
    el if i % 5 == 0    { println("Buzz") }
    el                  { println(i) }
    i = i + 1
  }
  rt 0
}
x := fizzbuzz(20)
```

The AST is `en Expr { Num, Var, Bin, Call(fname, args:[Expr]), Bad }` +
`en Stmt { SDecl, SAssign, SReturn, SPrint, SIf(cond, body:[Stmt]),
SLoop(cond, body:[Stmt]), SExpr }` + `st Func { name; params:[str]; body:[Stmt] }`
— recursive enums, arrays of enums, and structs holding them. A per-function
`{str:i64}` symbol set drives declare-once-then-assign so `:=`/`=` lower to
correct C (`int64_t x = …` vs `x = …`).

Growth path: `el` / else-if, string literals + types, structs/enums in the
*compiled* language — ultimately toward compiling the bootstrap compiler with
itself. The architecture (recursive AST, `[Token]` stream, `*P` parser state,
symbol set) is chosen so these slot in without a rewrite.

## Representations

- **Token** — `st Token { kind: i64; text: str; pos: i64 }`. `kind` is a small
  integer tag (see `tok_*` constants in `lexer.ail`); `text` is the lexeme.
- **Token stream** — `[Token]`, built by `push` in the lexer loop.
- **AST** — `en Expr { Num(v:i64), Var(name:str), Bin(op:i64, l:Expr, r:Expr), Bad }`,
  the recursive ADT. `op` is the operator's char code (`+` = 43, …).
- **Parser state** — `st P { toks: [Token]; pos: i64 }`, mutated in place through
  a `*P` pointer (`fn advance(p:*P)`), since AiLang fns return a single value.
- **Symbol table** — `{str:i64}`, name → 1 once declared; drives undefined-name
  detection and one-time C local declaration.

## Known rough edges

- **Empty `[]` literal ignored the declared element type** — `mu xs:[Token] := []`
  miscompiled to `[i64]`. **Fixed in the compiler** (sema now records the
  annotation onto the literal and back-propagates the element type from a later
  `push`). The committed bootstrap still seeds a sentinel element so it keeps
  building on an **older PATH `ailangc`**; once the fixed compiler is installed,
  the sentinel and `pos = 1` start can go. Same story for `slice`/`reverse` over
  `[Struct]` — now supported in-compiler.
- Tuples / multi-return **are** supported now — `fn divmod(a,b) -> (i64, i64)`
  returns a tuple, and `q, r := divmod(17, 5)` destructures it (a `_` slot
  ignores a value). Implemented in both `ailangc` and the bootstrap; a tuple
  lowers to a monomorphic `tup_<suffix>` C struct. `main.ail` itself still
  threads parser state through a `*P` struct — it predates tuples and doesn't
  dogfood them, which is also what keeps the strict fixpoint trivially intact.
- `mt` arm bodies are single expressions → multi-step arms use a helper fn or a
  `{ … }` block expression.
- `args()` returns user args only (no program name): the input file is `av[0]`,
  the output path `av[1]`.

## Running

```bash
# compile the bootstrap compiler itself with the real ailangc:
ailangc compile selfhost/main.ail -o /tmp/ailc0

# use it to compile a sample program to a native binary:
/tmp/ailc0 examples-selfhost/arith.ail        # writes + clang-links a binary
```

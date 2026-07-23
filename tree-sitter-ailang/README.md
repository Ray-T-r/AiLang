# tree-sitter-ailang

A [tree-sitter](https://tree-sitter.github.io) grammar for **AiLang** (`.ail`) —
the basis for syntax highlighting in editors (Neovim native, VS Code via a
generated TextMate grammar), on GitHub (via `github-linguist`), in docs, and in
the playground.

## Status

The grammar (`grammar.js`) and highlight queries (`queries/highlights.scm`) were
authored from the language's keyword/operator spec in
[`skill/SKILL.md`](../skill/SKILL.md). **It has not yet been run through the
tree-sitter CLI in this repo** — validate and tune any precedence conflicts with:

```bash
npm install
npx tree-sitter generate      # builds the parser; reports any conflicts to resolve
npx tree-sitter test          # once test/corpus/*.txt fixtures are added
npx tree-sitter parse ../examples-selfhost/shortc.ail   # eyeball a real parse
```

AiLang is deliberately small (≈2-char keywords, a fixed operator set, `"${expr}"`
interpolation, `//` comments), so the grammar is compact. The main thing to
verify after `generate` is binary-operator precedence (encoded in
`binary_expression`) and the `if`/`el` and `lp` forms.

## What's covered

- All keywords: `fn lp mt rt el if en st mu im ex cl vt tr cinc csrc br ct in as super`
- Definitions: functions (+ generics `<T: Bound>`), `st`/`en`/`cl`/`tr`, `im … as`, `ex` externs
- Types: primitives, `[T]`, `{K:V}`, `*T`, `!T`, tuples, `fn(..)->R`, `Name<..>`
- Statements: `:=`/`mu`, assignment + compound, `lp` (4 forms), `if`/`el`, `mt` (+ guards), `rt`/`br`/`ct`
- Expressions: binary/unary, calls, UFCS field access, indexing, lambdas, `?` try, `&` address-of
- Literals: int, float, `"…${interp}…"` strings with escapes, `b"…"` bytes, bools

## Next steps

1. `tree-sitter generate` + resolve conflicts.
2. Add `test/corpus/*.txt` from `examples-selfhost/*.ail`.
3. Publish to npm + submit to `github-linguist` (needs a sample corpus, which
   `examples-selfhost/` provides).
4. Wire into the playground's CodeMirror/Monaco editor.

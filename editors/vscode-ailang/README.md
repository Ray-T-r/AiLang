# AiLang for VS Code

Syntax highlighting, file icons, and on-save diagnostics for the
[AiLang](https://github.com/Ray-T-r/AiLang) programming language (`.ail`).

## Features

- **TextMate grammar** covering every keyword, primitive type, operator, and
  builtin function.
- **File icon** — a soft `A` mark in the explorer for `.ail` files.
- **Diagnostics** — runs `ailangc` on save/open and surfaces compiler errors
  and warnings as red/yellow squiggles with `ariadne`-accurate locations.
- **Code Runner support** — default executor map so `.ail` files run with
  `ailangc run <file>` via the
  [Code Runner](https://marketplace.visualstudio.com/items?itemName=formulahendry.code-runner)
  extension (if installed).
- **Editor defaults** — 2-space indent, sane bracket / quote auto-closing,
  block-comment toggle.

## Install (local, from source)

```bash
ln -sfn /path/to/AiLang/editors/vscode-ailang \
        ~/.vscode/extensions/ailang.vscode-ailang-0.2.0
```

Then `Cmd+Shift+P → Developer: Reload Window`.

## Settings

| Setting                       | Default     | Description                                                                 |
|------------------------------|-------------|-----------------------------------------------------------------------------|
| `ailang.compilerPath`        | `"ailangc"` | Path to the compiler. Use an absolute path if `ailangc` is not on `$PATH`. |
| `ailang.diagnostics.enable`  | `true`      | Run `ailangc` on save/open and show squiggles.                              |

## Diagnostics — how it works

On every save and open of an `.ail` file, the extension runs:

```
ailangc compile <file> --emit-c
```

…in the file's directory, parses `ariadne`'s text output (stripping ANSI),
and publishes each `Error:` / `Warning:` as a VS Code Diagnostic with the
file:line:col reported by the compiler. The squiggle length comes from the
backtick-quoted identifier in the message (e.g. <code>undefined name \`foo\`</code>
underlines `foo`); falls back to end-of-line if there's no identifier.

Note: this also writes a `<file>.c` next to the source as a side effect of
`--emit-c`. That's the same `.c` you'd get from running compile manually,
so the project's `.gitignore` already covers `/examples/*.c`.

If you want zero side effects, set `ailang.diagnostics.enable` to `false`.

## Code Runner

If you have
[Code Runner](https://marketplace.visualstudio.com/items?itemName=formulahendry.code-runner)
installed, this extension ships a default executor:

```jsonc
"code-runner.executorMap": {
  "ailang": "ailangc run $fullFileName"
}
```

Hit `Ctrl+Alt+N` (or click the ▶ button) in an `.ail` editor → output goes
to the Output panel.

If the default isn't picked up (Code Runner's merge logic can be finicky
when you already have your own `executorMap`), add it to your `settings.json`
manually:

```jsonc
"code-runner.executorMap": {
  "ailang": "ailangc run $fullFileName"
}
```

## File layout

```
editors/vscode-ailang/
├── package.json
├── language-configuration.json
├── icons/
│   └── ailang.svg
├── syntaxes/
│   └── ailang.tmLanguage.json
├── src/
│   └── extension.js                  # diagnostics provider (plain JS)
└── README.md
```

No build step; the extension is plain JavaScript, no `npm install` needed.

## Updating

If you add a new keyword, builtin, or operator to the compiler, update
the corresponding alternation in `syntaxes/ailang.tmLanguage.json`:

| Compiler change                                  | Grammar edit                                  |
|--------------------------------------------------|-----------------------------------------------|
| New keyword in [`ailang-lexer/src/lib.rs`](../../crates/ailang-lexer/src/lib.rs) | `keywords` repository pattern               |
| New primitive type                               | `types` repository pattern                    |
| New builtin function in codegen prelude          | `builtins` alternation                        |
| New multi-char operator                          | the right `operators` sub-pattern             |

After editing any file, **`Developer: Reload Window`** in VS Code is enough
to pick up the change (because the install is a symlink).

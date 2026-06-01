# selfhost/seed — the bootstrap seed

`ailc.c` is **not hand-written**. It is `selfhost/main.ail` compiled to C **by
the AiLang self-hosted compiler itself** — the 6841-line byte-identical
fixpoint that `verify.sh` proves (stage2.c == stage3.c).

## Why it exists

It's the **bootstrap seed**: a frozen, platform-independent snapshot that lets
anyone rebuild the AiLang compiler **without the Rust `ailangc`** — or any other
prior AiLang binary. All you need is `clang` + Boehm GC:

```bash
bash selfhost/bootstrap.sh
```

That compiles `ailc.c` → a native `ailc` compiler, uses it to recompile
`main.ail` from source, and checks the result reproduces this seed
byte-for-byte (the fixpoint). This is the standard way self-hosting languages
break their dependency on the original (here, Rust) compiler.

A committed C snapshot — not a native binary — is deliberate: C is portable
(any platform's clang builds it), reproducible, and reviewable in a diff; a
checked-in binary would be macOS-arm64-only and opaque.

## Regenerating after changing main.ail

The seed must be refreshed whenever `main.ail` changes, or `bootstrap.sh`'s
fixpoint check will report a mismatch. With a working `ailc` (or via Rust):

```bash
# from an already-bootstrapped ./selfhost/ailc:
./ailc main.ail /tmp/s && cp /tmp/s.c seed/ailc.c

# or seeding from the Rust compiler:
ailangc compile main.ail -o /tmp/a && /tmp/a main.ail /tmp/s && cp /tmp/s.c seed/ailc.c
```

Then re-run `bash selfhost/bootstrap.sh` to confirm the fixpoint holds.

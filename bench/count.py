#!/usr/bin/env python3
"""Count BPE tokens across the bench/ programs.

Uses both cl100k_base (GPT-4 / GPT-3.5) and o200k_base (GPT-4o, similar to
the tokenizers used by recent Claude/GPT models) to get a representative
picture.
"""
import os
import sys
from pathlib import Path

import tiktoken

ENCODINGS = {
    "cl100k": tiktoken.get_encoding("cl100k_base"),
    "o200k":  tiktoken.get_encoding("o200k_base"),
}

EXT_BY_LANG = {
    "ailang": ".ail",
    "python": ".py",
    "rust":   ".rs",
    "go":     ".go",
    "js":     ".js",
    "java":   ".java",
    "c":      ".c",
}

PROGRAMS = ["fib", "fizzbuzz", "greet", "sum"]
LANGS = ["ailang", "python", "go", "js", "rust", "java", "c"]


def count(text: str, enc):
    return len(enc.encode(text))


def find(lang_dir: Path, program: str, ext: str) -> Path | None:
    # Java uses CamelCase filenames.
    candidates = [
        lang_dir / f"{program}{ext}",
        lang_dir / f"{program.capitalize()}{ext}",
        lang_dir / f"{program.replace('zbuzz', 'zBuzz').capitalize()}{ext}",
    ]
    for c in candidates:
        if c.exists():
            return c
    return None


def main():
    root = Path(__file__).parent
    for enc_name, enc in ENCODINGS.items():
        print(f"\n=== Tokenizer: {enc_name} ===\n")
        # Header
        header = f"{'program':<10}" + "".join(f"{l:>10}" for l in LANGS)
        print(header)
        print("-" * len(header))
        # Per-program rows
        totals = {l: 0 for l in LANGS}
        for prog in PROGRAMS:
            row = f"{prog:<10}"
            for lang in LANGS:
                ext = EXT_BY_LANG[lang]
                p = find(root / lang, prog, ext)
                if p is None:
                    row += f"{'-':>10}"
                    continue
                text = p.read_text()
                n = count(text, enc)
                totals[lang] += n
                row += f"{n:>10}"
            print(row)
        # Totals row
        print("-" * len(header))
        total_row = f"{'TOTAL':<10}"
        for lang in LANGS:
            total_row += f"{totals[lang]:>10}"
        print(total_row)
        # Ratio row (vs ailang)
        base = totals["ailang"]
        ratio_row = f"{'vs AL':<10}"
        for lang in LANGS:
            r = totals[lang] / base if base else 0
            ratio_row += f"{r:>10.2f}"
        print(ratio_row)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Compare AiLang v2 (with token-saver features) against all 7 languages."""
from pathlib import Path
import tiktoken

ENC = tiktoken.get_encoding("cl100k_base")

EXT_BY_LANG = {
    "ailang_v2": ".ail",
    "python":    ".py",
    "rust":      ".rs",
    "go":        ".go",
    "js":        ".js",
    "java":      ".java",
    "c":         ".c",
}
PROGRAMS = ["fib", "fizzbuzz", "greet", "sum"]
LANGS = ["ailang_v2", "python", "js", "rust", "go", "java", "c"]


def n(path):
    return len(ENC.encode(Path(path).read_text()))


def find(lang_dir: Path, program: str, ext: str):
    for cand in (
        lang_dir / f"{program}{ext}",
        lang_dir / f"{program.capitalize()}{ext}",
        lang_dir / f"{program.replace('zbuzz','zBuzz').capitalize()}{ext}",
    ):
        if cand.exists():
            return cand
    return None


root = Path(__file__).parent

header = f"{'program':<10}" + "".join(f"{l:>11}" for l in LANGS)
print(header)
print("-" * len(header))

totals = {l: 0 for l in LANGS}
for prog in PROGRAMS:
    row = f"{prog:<10}"
    for lang in LANGS:
        ext = EXT_BY_LANG[lang]
        p = find(root / lang, prog, ext)
        if p is None:
            row += f"{'-':>11}"; continue
        c = n(p); totals[lang] += c
        row += f"{c:>11}"
    print(row)

print("-" * len(header))
row = f"{'TOTAL':<10}"
for lang in LANGS:
    row += f"{totals[lang]:>11}"
print(row)

base = totals["ailang_v2"]
row = f"{'vs AL':<10}"
for lang in LANGS:
    row += f"{totals[lang]/base:>10.2f}x"
print(row)

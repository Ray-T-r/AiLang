#!/usr/bin/env python3
from pathlib import Path
import tiktoken

ENC = tiktoken.get_encoding("cl100k_base")
ROOT = Path(__file__).parent / "perf"

PROGS = ["wordcount", "jsonapi", "primes"]
LANGS = [
    ("AiLang", "ailang", ".ail"),
    ("Python", "python", ".py"),
    ("JavaScript", "js", ".js"),
    ("Rust", "rust", ".rs"),
    ("Go", "go", ".go"),
    ("Java", "java", ".java"),
    ("C", "c", ".c"),
]

def count(path):
    return len(ENC.encode(Path(path).read_text()))

def fname(lang_dir, ext, prog):
    if lang_dir == "java":
        return f"{prog.capitalize()}.java"
    return f"{prog}{ext}"

print(f"{'program':<14}" + "".join(f"{l:>12}" for l, _, _ in LANGS))
print("-" * (14 + 12 * len(LANGS)))

totals = {l: 0 for l, _, _ in LANGS}
for prog in PROGS:
    print(f"{prog:<14}", end="")
    for label, dirname, ext in LANGS:
        f = ROOT / dirname / fname(dirname, ext, prog)
        if not f.exists():
            print(f"{'-':>12}", end="")
            continue
        n = count(f)
        totals[label] += n
        print(f"{n:>12}", end="")
    print()

print("-" * (14 + 12 * len(LANGS)))
print(f"{'TOTAL':<14}", end="")
for l, _, _ in LANGS:
    print(f"{totals[l]:>12}", end="")
print()

base = totals["AiLang"]
print(f"{'vs AiLang':<14}", end="")
for l, _, _ in LANGS:
    print(f"{totals[l]/base:>11.2f}x", end="")
print()

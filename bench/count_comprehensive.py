#!/usr/bin/env python3
from pathlib import Path
import tiktoken

ENC = tiktoken.get_encoding("cl100k_base")
ROOT = Path(__file__).parent

def nt(path):
    return len(ENC.encode(Path(path).read_text()))

def find_file(directory, base_name, ext):
    for cand in (
        directory / f"{base_name}{ext}",
        directory / f"{base_name.capitalize()}{ext}",
        directory / f"{base_name.replace('zbuzz','zBuzz').capitalize()}{ext}",
    ):
        if cand.exists():
            return cand
    return None

print("=" * 80)
print("section: small microstructure benchmarks (fib, fizzbuzz, greet, sum)")
print("tokenizer: cl100k_base (GPT-4)")
print("=" * 80)

LANG_DIRS = {
    "AiLang": "ailang_v2", "Python": "python", "JavaScript": "js",
    "Rust": "rust", "Go": "go", "Java": "java", "C": "c",
}
PROGS_SMALL = ["fib", "fizzbuzz", "greet", "sum"]
EXT = { "AiLang": ".ail", "Python": ".py", "JavaScript": ".js",
        "Rust": ".rs", "Go": ".go", "Java": ".java", "C": ".c" }

print(f"\n{'program':<12}", end="")
for l in LANG_DIRS:
    print(f"{l:>10}", end="")
print()

print("-" * (12 + 10 * len(LANG_DIRS)))
totals = {l: 0 for l in LANG_DIRS}
for prog in PROGS_SMALL:
    print(f"{prog:<12}", end="")
    for lang, dir_name in LANG_DIRS.items():
        ex = EXT[lang]
        if lang == "Java":
            ex = ".java"
        p = find_file(ROOT / dir_name, prog, ex)
        if p is None or not p.exists():
            print(f"{'-':>10}", end="")
            continue
        c = nt(p); totals[lang] += c
        print(f"{c:>10}", end="")
    print()

print("-" * (12 + 10 * len(LANG_DIRS)))
print(f"{'TOTAL':<12}", end="")
for l in LANG_DIRS:
    print(f"{totals[l]:>10}", end="")
print()

base = totals["AiLang"]
print(f"{'vs AiLang':<12}", end="")
for l in LANG_DIRS:
    print(f"{totals[l]/base:>9.2f}x", end="")
print()

print()
print("=" * 80)
print("section: generated C code vs hand-written C (token cost of AiLang backend output)")
print("tokenizer: cl100k_base (GPT-4)")
print("=" * 80)

GEN_LABELS = {"ailang_v2": "AiLang(ail)", "ailang_v2_c": "AiLang→C", "c": "Hand C"}
print(f"\n{'program':<12}{'AiLangSrc':>12}{'Gen C':>12}{'Hand C':>12}{'Gen/Hand':>10}")
print("-" * 58)
for prog in PROGS_SMALL:
    ail_src = ROOT / "ailang_v2" / f"{prog}.ail"
    gen_c = ROOT / "ailang_v2" / f"{prog}.c"
    hand_c = ROOT / "c" / f"{prog}.c"
    a = nt(ail_src); g = nt(gen_c); h = nt(hand_c) if hand_c.exists() else 0
    print(f"{prog:<12}{a:>12}{g:>12}{h:>12}{g/h:>9.2f}x" if h else f"{prog:<12}{a:>12}{g:>12}{'-':>12}{'-':>10}")

print()
print("=" * 80)
print("section: performance benchmarks (fib40, primes) — source and generated C")
print("tokenizer: cl100k_base (GPT-4)")
print("=" * 80)

PERF_DIRS = {
    "ailang": ".ail", "python": ".py", "js": ".js",
    "rust": ".rs", "go": ".go", "java": ".java", "c": ".c",
}
PERF_LABEL = {"ailang": "AiLang", "python": "Python", "js": "JavaScript",
              "rust": "Rust", "go": "Go", "java": "Java", "c": "C"}

PERF_PROGS = ["fib40", "primes", "sum", "strs"]

for pprog in PERF_PROGS:
    print(f"\n--- {pprog} ---")
    print(f"{'language':<14}{'tokens':>10}{'vs C src':>10}")
    print("-" * 34)
    c_base = 0
    results = {}
    for lang_dir, ext in PERF_DIRS.items():
        d = ROOT / "perf" / lang_dir
        if lang_dir == "java":
            f = d / f"{pprog.capitalize()}.java"
        else:
            f = d / f"{pprog}{ext}"
        if not f.exists():
            continue
        results[lang_dir] = nt(f)
    if "c" in results:
        c_base = results["c"]
    for lang_dir, count in sorted(results.items(), key=lambda x: x[1]):
        label = PERF_LABEL[lang_dir]
        if c_base > 0:
            print(f"{label:<14}{count:>10}{count/c_base:>9.2f}x")
        else:
            print(f"{label:<14}{count:>10}")

    gen_c = ROOT / "perf" / "ailang" / f"{pprog}.c"
    if gen_c.exists():
        gc = nt(gen_c)
        print(f"{'AiLang→C':<14}{gc:>10}", end="")
        if c_base > 0:
            print(f"{gc/c_base:>9.2f}x")
        else:
            print()

print()
print("=" * 80)
print("section: AiLang source vs generated C (compression ratio)")
print("=" * 80)
print(f"{'program':<12}{'AiLang(.ail)':>15}{'Gen C':>10}{'ratio':>10}")
print("-" * 47)
all_ail_progs = [
    (ROOT / "ailang_v2" / f"{p}.ail", ROOT / "ailang_v2" / f"{p}.c", p)
    for p in PROGS_SMALL
] + [
    (ROOT / "perf" / "ailang" / f"{p}.ail", ROOT / "perf" / "ailang" / f"{p}.c", p)
    for p in PERF_PROGS
]
for ail_f, c_f, name in all_ail_progs:
    if not ail_f.exists() or not c_f.exists():
        continue
    a = nt(ail_f); c = nt(c_f)
    print(f"{name:<12}{a:>15}{c:>10}{c/a:>9.2f}x")

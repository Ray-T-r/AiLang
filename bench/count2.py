#!/usr/bin/env python3
"""Compare current AiLang vs proposed improvements vs Python."""
from pathlib import Path
import tiktoken

ENC = tiktoken.get_encoding("cl100k_base")
PROGRAMS = ["fib", "fizzbuzz", "greet", "sum"]

def n(path):
    return len(ENC.encode(Path(path).read_text()))

root = Path(__file__).parent

print(f"{'program':<10}{'AiLang v1':>12}{'AiLang v2':>12}{'Python':>10}{'v2 vs Py':>10}{'v2 vs v1':>10}")
print("-" * 64)

tot_v1 = tot_v2 = tot_py = 0
for p in PROGRAMS:
    v1 = n(root / f"ailang/{p}.ail")
    v2 = n(root / f"ailang_v2/{p}.ail")
    py = n(root / f"python/{p}.py")
    tot_v1 += v1; tot_v2 += v2; tot_py += py
    diff_v1 = f"{v2/v1:.2f}x"
    diff_py = f"{v2/py:.2f}x"
    print(f"{p:<10}{v1:>12}{v2:>12}{py:>10}{diff_py:>10}{diff_v1:>10}")

print("-" * 64)
print(f"{'TOTAL':<10}{tot_v1:>12}{tot_v2:>12}{tot_py:>10}{tot_v2/tot_py:>9.2f}x{tot_v2/tot_v1:>9.2f}x")

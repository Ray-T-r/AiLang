#!/usr/bin/env python3
"""Build study/tasks/tasks.json from the pre-registered task corpus.

Each task carries a natural-language spec (what the agent reads), a Python
reference solution (the ORACLE that defines expected output), and a set of
sample + hidden input cases. Expected outputs are produced by running the
reference on each input, so the corpus is self-consistent by construction.

The reference solutions are also written out to study/tasks/reference/py/<id>.py
so the grader self-test can re-run them as standalone programs.

Run:  python3 study/harness/build_tasks.py
Deterministic, no network, no third-party deps.
"""
import json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
STUDY = os.path.dirname(HERE)
TASKS_DIR = os.path.join(STUDY, "tasks")
PYREF_DIR = os.path.join(TASKS_DIR, "reference", "py")

# I/O contract shared by every task and stated in each spec:
#   the program reads ALL of standard input and writes its answer to standard
#   output. Grading strips trailing newlines from both sides before comparing.

TASKS = [
    {
        "id": "rle_encode", "category": "string", "title": "Run-length encoding",
        "spec": (
            "Read all of standard input. The input is a single line of lowercase "
            "ASCII letters (it may be empty). Write the run-length encoding: for "
            "each maximal run of identical characters, output the character "
            "followed by the decimal length of that run, all concatenated, on one "
            "line. Example: input 'aaabbc' -> output 'a3b2c1'. Empty input -> empty line."
        ),
        "py": (
            "import sys\n"
            "data=sys.stdin.read()\n"
            "line=data.split('\\n')[0] if data else ''\n"
            "out=[]; i=0\n"
            "while i<len(line):\n"
            "    j=i\n"
            "    while j<len(line) and line[j]==line[i]: j+=1\n"
            "    out.append(line[i]+str(j-i)); i=j\n"
            "print(''.join(out))\n"
        ),
        "samples": ["aaabbc", "wwwwww"],
        "hidden": ["", "abcd", "zzzzzzzzzza", "a"],
    },
    {
        "id": "caesar", "category": "string", "title": "Caesar shift",
        "spec": (
            "The first line of input is an integer K (0..25). The second line is a "
            "string of lowercase letters. Output that string with every letter "
            "shifted forward by K positions in the alphabet, wrapping z->a. "
            "Example: K=1, 'abc' -> 'bcd'."
        ),
        "py": (
            "import sys\n"
            "d=sys.stdin.read().split('\\n')\n"
            "k=int(d[0]); s=d[1] if len(d)>1 else ''\n"
            "print(''.join(chr((ord(c)-97+k)%26+97) for c in s))\n"
        ),
        "samples": ["1\nabc", "25\nxyz"],
        "hidden": ["0\nhello", "13\nthequickbrownfox", "3\nzzz"],
    },
    {
        "id": "word_freq", "category": "string", "title": "Most frequent word",
        "spec": (
            "The input is one or more lines of words separated by whitespace. "
            "Count word frequencies (case-sensitive). Output the single most "
            "frequent word; if several words tie for the maximum frequency, output "
            "the lexicographically smallest of them. One line of output."
        ),
        "py": (
            "import sys\n"
            "from collections import Counter\n"
            "c=Counter(sys.stdin.read().split())\n"
            "print(sorted(c.items(), key=lambda kv:(-kv[1], kv[0]))[0][0])\n"
        ),
        "samples": ["a b a c b a", "the the cat the dog cat"],
        "hidden": ["x y z", "apple apple banana banana", "one"],
    },
    {
        "id": "balanced", "category": "string", "title": "Balanced brackets",
        "spec": (
            "The input is a single line containing only the characters ()[]{}. "
            "Output YES if the brackets are balanced and correctly nested, "
            "otherwise NO. An empty line is balanced (YES)."
        ),
        "py": (
            "import sys\n"
            "s=sys.stdin.read().split('\\n')[0]\n"
            "st=[]; pair={')':'(',']':'[','}':'{'}; ok=True\n"
            "for c in s:\n"
            "    if c in '([{': st.append(c)\n"
            "    elif c in ')]}':\n"
            "        if not st or st[-1]!=pair[c]: ok=False; break\n"
            "        st.pop()\n"
            "print('YES' if ok and not st else 'NO')\n"
        ),
        "samples": ["()[]{}", "([{}])"],
        "hidden": ["(]", "(((", "", "{[()()]}"],
    },
    {
        "id": "roman_to_int", "category": "parse", "title": "Roman numeral to integer",
        "spec": (
            "The input is a single line with a valid Roman numeral (uppercase, "
            "value 1..3999). Output its integer value."
        ),
        "py": (
            "import sys\n"
            "s=sys.stdin.read().split('\\n')[0]\n"
            "v={'I':1,'V':5,'X':10,'L':50,'C':100,'D':500,'M':1000}; t=0\n"
            "for i,c in enumerate(s):\n"
            "    if i+1<len(s) and v[c]<v[s[i+1]]: t-=v[c]\n"
            "    else: t+=v[c]\n"
            "print(t)\n"
        ),
        "samples": ["III", "MCMXCIV"],
        "hidden": ["IV", "XLII", "MMXXIV", "DCCCLXXXVIII"],
    },
    {
        "id": "expr_eval", "category": "parse", "title": "Arithmetic expression evaluator",
        "spec": (
            "The input is a single line: an arithmetic expression over non-negative "
            "integers with binary operators + - * /, parentheses ( ), and optional "
            "spaces. Use standard precedence (* and / bind tighter than + and -) and "
            "left-to-right association within equal precedence. Division is integer "
            "division truncating toward zero; assume no division by zero. Output the "
            "integer result."
        ),
        "py": (
            "import sys\n"
            "s=sys.stdin.read().split('\\n')[0]; toks=[]; i=0\n"
            "while i<len(s):\n"
            "    c=s[i]\n"
            "    if c==' ': i+=1; continue\n"
            "    if c.isdigit():\n"
            "        j=i\n"
            "        while j<len(s) and s[j].isdigit(): j+=1\n"
            "        toks.append(s[i:j]); i=j\n"
            "    else: toks.append(c); i+=1\n"
            "pos=0\n"
            "def peek():\n"
            "    return toks[pos] if pos<len(toks) else None\n"
            "def trunc(a,b):\n"
            "    q=abs(a)//abs(b)\n"
            "    return -q if (a<0)!=(b<0) else q\n"
            "def expr():\n"
            "    global pos; v=term()\n"
            "    while peek() in ('+','-'):\n"
            "        op=toks[pos]; pos+=1; r=term(); v=v+r if op=='+' else v-r\n"
            "    return v\n"
            "def term():\n"
            "    global pos; v=fac()\n"
            "    while peek() in ('*','/'):\n"
            "        op=toks[pos]; pos+=1; r=fac(); v=v*r if op=='*' else trunc(v,r)\n"
            "    return v\n"
            "def fac():\n"
            "    global pos; t=toks[pos]\n"
            "    if t=='(':\n"
            "        pos+=1; v=expr(); pos+=1; return v\n"
            "    pos+=1; return int(t)\n"
            "print(expr())\n"
        ),
        "samples": ["2+3*4", "(1+2)*3"],
        "hidden": ["10-2*3", "100/7", "(2+3)*(4-1)", "7-3-2"],
    },
    {
        "id": "two_sum", "category": "algo", "title": "Two sum (indices)",
        "spec": (
            "The first line is an integer target. The second line is space-separated "
            "integers (length >= 2). Exactly one pair of distinct indices sums to the "
            "target. Output the two 0-based indices, smaller first, space-separated."
        ),
        "py": (
            "import sys\n"
            "d=sys.stdin.read().split('\\n')\n"
            "t=int(d[0]); a=list(map(int,d[1].split())); seen={}\n"
            "for i,x in enumerate(a):\n"
            "    if t-x in seen: print(seen[t-x], i); break\n"
            "    seen[x]=i\n"
        ),
        "samples": ["9\n2 7 11 15", "6\n3 2 4"],
        "hidden": ["8\n1 2 3 4 5", "-1\n-3 4 1 2", "0\n0 5 0"],
    },
    {
        "id": "merge_intervals", "category": "algo", "title": "Merge intervals",
        "spec": (
            "The first line is an integer N. Each of the next N lines has two integers "
            "'a b' with a<=b. Merge intervals that overlap or touch (touching means "
            "the next start <= current end). Output the merged intervals sorted by "
            "start, one per line as 'a b'."
        ),
        "py": (
            "import sys\n"
            "d=sys.stdin.read().split('\\n'); n=int(d[0]); iv=[]\n"
            "for i in range(1,n+1):\n"
            "    a,b=map(int,d[i].split()); iv.append((a,b))\n"
            "iv.sort(); res=[]\n"
            "for a,b in iv:\n"
            "    if res and a<=res[-1][1]: res[-1]=(res[-1][0], max(res[-1][1],b))\n"
            "    else: res.append((a,b))\n"
            "print('\\n'.join(f'{a} {b}' for a,b in res))\n"
        ),
        "samples": ["3\n1 3\n2 6\n8 10", "2\n1 4\n5 6"],
        "hidden": ["1\n5 7", "3\n1 4\n4 5\n6 8", "4\n1 10\n2 3\n4 5\n11 12"],
    },
    {
        "id": "group_anagrams", "category": "algo", "title": "Group anagrams",
        "spec": (
            "The input is words separated by whitespace. Group words that are "
            "anagrams of one another. Within each group sort the words "
            "lexicographically; sort the groups by their first word "
            "lexicographically. Output one group per line, words space-separated."
        ),
        "py": (
            "import sys\n"
            "from collections import defaultdict\n"
            "g=defaultdict(list)\n"
            "for x in sys.stdin.read().split(): g[''.join(sorted(x))].append(x)\n"
            "groups=sorted((sorted(v) for v in g.values()), key=lambda v:v[0])\n"
            "print('\\n'.join(' '.join(v) for v in groups))\n"
        ),
        "samples": ["eat tea tan ate nat bat", "abc bca cab"],
        "hidden": ["a", "listen silent enlist", "x y z"],
    },
    {
        "id": "search_insert", "category": "algo", "title": "Binary search insert position",
        "spec": (
            "The first line is space-separated, non-decreasing integers (may be an "
            "empty line). The second line is an integer target. Output the leftmost "
            "0-based index at which target could be inserted to keep the array sorted "
            "(equivalently, the count of elements strictly less than target)."
        ),
        "py": (
            "import sys\n"
            "d=sys.stdin.read().split('\\n')\n"
            "a=list(map(int,d[0].split())) if d[0].strip() else []\n"
            "t=int(d[1]); lo,hi=0,len(a)\n"
            "while lo<hi:\n"
            "    m=(lo+hi)//2\n"
            "    if a[m]<t: lo=m+1\n"
            "    else: hi=m\n"
            "print(lo)\n"
        ),
        "samples": ["1 3 5 6\n5", "1 3 5 6\n2"],
        "hidden": ["1 3 5 6\n7", "1 3 5 6\n0", "\n4", "2 2 2\n2"],
    },
    {
        "id": "bigint_add", "category": "numeric", "title": "Big integer addition",
        "spec": (
            "Two lines, each a non-negative integer that may be very large (hundreds "
            "of digits, no leading zeros except the value 0 itself). Output their sum "
            "as a decimal integer with no leading zeros. NOTE: the values can exceed "
            "64-bit range."
        ),
        "py": (
            "import sys\n"
            "d=sys.stdin.read().split('\\n')\n"
            "print(int(d[0])+int(d[1]))\n"
        ),
        "samples": ["2\n3", "99\n1"],
        "hidden": [
            "12345678901234567890\n98765432109876543210",
            "0\n0",
            "999999999999999999999999\n1",
        ],
    },
    {
        "id": "base_convert", "category": "numeric", "title": "Base conversion",
        "spec": (
            "Three lines: (1) a number written in base B1 using digits 0-9 and "
            "uppercase A-Z, (2) the source base B1 (2..36), (3) the target base B2 "
            "(2..36). Output the same value written in base B2 using digits 0-9 and "
            "uppercase A-Z, with no leading zeros (the value 0 outputs '0')."
        ),
        "py": (
            "import sys\n"
            "d=sys.stdin.read().split('\\n')\n"
            "n=int(d[0].strip(), int(d[1])); b2=int(d[2])\n"
            "digs='0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'\n"
            "if n==0: print('0')\n"
            "else:\n"
            "    out=''\n"
            "    while n>0: out=digs[n%b2]+out; n//=b2\n"
            "    print(out)\n"
        ),
        "samples": ["FF\n16\n2", "10\n2\n10"],
        "hidden": ["0\n10\n2", "Z\n36\n10", "255\n10\n16"],
    },
    {
        "id": "prime_count", "category": "numeric", "title": "Count primes <= N",
        "spec": (
            "The input is a single integer N (0..1000000). Output how many prime "
            "numbers p satisfy 2 <= p <= N."
        ),
        "py": (
            "import sys\n"
            "n=int(sys.stdin.read().split('\\n')[0])\n"
            "if n<2: print(0)\n"
            "else:\n"
            "    s=bytearray([1])*(n+1); s[0]=s[1]=0; i=2\n"
            "    while i*i<=n:\n"
            "        if s[i]: s[i*i::i]=bytearray(len(s[i*i::i]))\n"
            "        i+=1\n"
            "    print(sum(s))\n"
        ),
        "samples": ["10", "2"],
        "hidden": ["1", "100", "0", "30"],
    },
    {
        "id": "csv_col_sum", "category": "io", "title": "Sum a CSV column",
        "spec": (
            "The first line is an integer C (a 0-based column index). The remaining "
            "lines are CSV rows (simple comma-separated, no quoting). Column C of "
            "every non-empty row holds an integer. Output the sum of column C over "
            "all non-empty rows."
        ),
        "py": (
            "import sys\n"
            "d=sys.stdin.read().split('\\n'); c=int(d[0]); tot=0\n"
            "for line in d[1:]:\n"
            "    if line.strip()=='': continue\n"
            "    tot+=int(line.split(',')[c])\n"
            "print(tot)\n"
        ),
        "samples": ["1\na,10,x\nb,20,y", "0\n5\n7\n8"],
        "hidden": ["2\np,q,3\nr,s,4", "1\nonly,42"],
    },
    {
        "id": "matrix_transpose", "category": "io", "title": "Transpose a matrix",
        "spec": (
            "The first line is two integers 'R C' (rows, columns). The next R lines "
            "each contain C space-separated integers. Output the transpose: C lines "
            "of R space-separated integers."
        ),
        "py": (
            "import sys\n"
            "d=sys.stdin.read().split('\\n'); r,c=map(int,d[0].split())\n"
            "m=[list(map(int,d[1+i].split())) for i in range(r)]\n"
            "for j in range(c):\n"
            "    print(' '.join(str(m[i][j]) for i in range(r)))\n"
        ),
        "samples": ["2 3\n1 2 3\n4 5 6", "1 1\n9"],
        "hidden": ["3 2\n1 2\n3 4\n5 6", "2 2\n1 0\n0 1"],
    },
]

# Pilot subset: validated independently against AiLang reference solutions.
PILOT = ["rle_encode", "two_sum", "bigint_add"]


def run_py(src_path, stdin):
    p = subprocess.run([sys.executable, src_path], input=stdin,
                       capture_output=True, text=True, timeout=30)
    if p.returncode != 0:
        raise RuntimeError(f"reference {src_path} exited {p.returncode}: {p.stderr}")
    return p.stdout.rstrip("\n")


def main():
    os.makedirs(PYREF_DIR, exist_ok=True)
    ids = set()
    out_tasks = []
    for t in TASKS:
        assert t["id"] not in ids, f"duplicate id {t['id']}"
        ids.add(t["id"])
        ref_path = os.path.join(PYREF_DIR, t["id"] + ".py")
        with open(ref_path, "w") as f:
            f.write(t["py"])
        cases = {"samples": [], "hidden": []}
        for kind in ("samples", "hidden"):
            for stdin in t[kind]:
                cases[kind].append({"in": stdin, "out": run_py(ref_path, stdin)})
        out_tasks.append({
            "id": t["id"], "category": t["category"], "title": t["title"],
            "spec": t["spec"], "pilot": t["id"] in PILOT,
            "samples": cases["samples"], "hidden": cases["hidden"],
        })
    doc = {
        "version": "1.0",
        "io_contract": "Program reads ALL of stdin and writes the answer to stdout. "
                       "Grading strips trailing newlines from both sides before comparing.",
        "oracle": "Expected outputs are defined by the Python reference in "
                  "tasks/reference/py/<id>.py.",
        "n_tasks": len(out_tasks),
        "pilot": PILOT,
        "tasks": out_tasks,
    }
    out_path = os.path.join(TASKS_DIR, "tasks.json")
    with open(out_path, "w") as f:
        json.dump(doc, f, indent=2, ensure_ascii=False)
        f.write("\n")
    n_cases = sum(len(t["samples"]) + len(t["hidden"]) for t in out_tasks)
    print(f"wrote {out_path}: {len(out_tasks)} tasks, {n_cases} cases")
    print(f"wrote {len(out_tasks)} python references to {PYREF_DIR}")


if __name__ == "__main__":
    main()

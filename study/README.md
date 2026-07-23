# Study — does AiLang's terse syntax actually save an LLM tokens?

AiLang's original headline was "the fewest source tokens." Measured on four toy
programs, that edge is **1.05× over Python** — within noise. This study tests
the claim that actually matters: when an autonomous agent writes a *working*
program through a full check→fix loop, does AiLang spend **fewer total tokens**
than Python, once you count the AiLang context pack (`SKILL.md`) and the retries
an out-of-distribution language induces?

The design is pre-registered in [`methodology.md`](methodology.md). We report
the result whichever way it falls.

## Layout

```
study/
  methodology.md            pre-registered design (commit before measuring)
  tasks/
    tasks.json              15 tasks, 79 cases (generated; the corpus)
    reference/py/*.py        Python reference = the expected-output oracle
    reference/ail/*.ail      AiLang references for the 3 pilot tasks (cross-check)
  harness/
    build_tasks.py          regenerate tasks.json from the references
    runner.py               objective compile/run/grade primitives (both langs)
    selftest.py             OFFLINE corpus + grader validation (no API)
    run_study.py            the autonomous agent loop (live API, or --mock)
  results/                  raw per-run records + summaries (generated)
```

## Requirements

- **Python 3** (standard library only — no third-party deps).
- **A current `ailc`** that supports `check`/`run`/`--json`. An `ailc` installed
  before the agent-native CLI landed will NOT work. Build a fresh one and point
  the harness at it:
  ```bash
  bash selfhost/bootstrap.sh
  export AILC="$PWD/selfhost/ailc"
  export AILANG_STD="$PWD/std"
  ```
  (`runner.py` auto-uses `selfhost/ailc` when present, but `bootstrap.sh` and
  `verify.sh` create/delete it, so setting `AILC` explicitly is safest.)
- *(Optional, recommended for the fairest run)* **`pyright`** on PATH, so
  Python's static-check step is a type check matching AiLang's `ailc check`
  rather than a syntax-only `py_compile`. See methodology, "Static-check
  asymmetry."
- **To run live:** `export ANTHROPIC_API_KEY=...`

## Run it

**1. Validate the corpus + grader offline (no API, no key):**
```bash
python3 study/harness/selftest.py
```
Confirms every Python reference passes its own cases and that each pilot task's
AiLang reference compiles, runs, and reproduces the same expected outputs.

**2. Smoke-test the agent loop offline (no API):**
```bash
python3 study/harness/run_study.py --mock --tasks pilot --langs ailang,python
```
Drives the full loop using the reference solutions instead of the model —
validates code extraction, the check step, sample feedback, hidden grading, and
metric recording without spending tokens.

**3. The real study (live API):**
```bash
export ANTHROPIC_API_KEY=...
python3 study/harness/run_study.py \
    --tasks all --langs ailang,python --trials 3 --rounds 5 \
    --model claude-sonnet-4-6
```
Writes per-run records to `results/raw/<model>/` and an aggregate to
`results/summary_<model>.json`. Start small (`--tasks pilot --trials 1`) to
confirm cost, then scale.

## Status

- ✅ Corpus (15 tasks / 79 cases) built and self-consistent.
- ✅ Grader + both-language compile/run/grade path validated offline
  (`selftest.py` green, including the 24-digit `bigint_add` cross-check).
- ✅ Agent loop wiring validated offline (`run_study.py --mock` green).
- ⏳ **Live measurement not yet run** — needs `ANTHROPIC_API_KEY`. The mock run
  already previews the central tension: AiLang carries a ~9k-token context pack
  per round that Python does not.

## Honesty notes

- Expected outputs are defined by the Python reference oracle; a spec/oracle
  mismatch would mark a correct agent solution wrong. Specs were written to
  match the references; the 3-task AiLang cross-check guards the machinery.
- Tokens are reported **uncached** (the context pack is re-sent each round —
  worst case for AiLang). Prompt caching would narrow the gap.
- Charging `SKILL.md` to AiLang is deliberate: it is the real cost of an
  out-of-distribution language, and measuring it is the entire point.

# Pre-registered methodology — does AiLang's terse syntax actually save an LLM tokens?

> **Pre-registration.** This file fixes the design *before* any measurement run.
> It should be committed to git ahead of the first `run_study.py` invocation
> against the live API, and not edited to fit the results. Result files land in
> `results/` and reference this document.

## Research question

When an autonomous LLM agent writes a *working* program through a full
check→fix loop, does AiLang's deliberately terse syntax **reduce the total
tokens spent** relative to Python — once you count (a) the tokens of the AiLang
context pack the agent needs (`SKILL.md`) and (b) the retries an
out-of-distribution language induces?

This is the question the project's headline "fewest source tokens" claim was
never tested against: that claim measured **finished source length** on four
toy programs (AiLang 130 vs Python 136 tokens = 1.05×). It did not measure what
an agent actually *spends to get there*.

## Hypotheses

- **H1 (the project's implicit thesis):** median total-tokens-to-correct is
  lower for AiLang than for Python.
- **H0 (null):** AiLang is not lower (equal, or higher).

We report the result **whichever way it falls.** A finding that terseness costs
an agent more than it saves — because out-of-distribution retries and the
context pack dominate the few characters saved — is a real, publishable result.

## Design

Within-task paired comparison. For each task we run an identical autonomous
loop in AiLang and in Python, `T` trials each, same model, same temperature,
same round cap. No human ever edits the agent's code (see *Autonomous arm*).

The loop (implemented in `harness/run_study.py`):

```
generate  ->  static check  ->  run on the visible SAMPLE cases
          ->  on failure, feed the error back and regenerate (<= --rounds)
final program  ->  graded on the HIDDEN cases (never shown to the agent)
```

## Task corpus (`tasks/tasks.json`, 15 tasks, 79 cases)

Built by `harness/build_tasks.py`. Selection rules, fixed in advance:

- **Drawn from the standard programming-exercise canon** (string processing,
  parsing, classic algorithms, numeric/edge cases, simple I/O) — *not* authored
  to flatter AiLang's `shortc.ail` sweet spot (HTTP/DB/JSON services).
- **Deliberately weighted toward AiLang's weak areas**: string/character work,
  parsing, data structures, and at least one task (`bigint_add`) that exceeds
  64-bit range, forcing manual digit arithmetic that a terse syntax cannot
  shorten. Categories: string (4), parse (2), algo (4), numeric (3), io (2).
- **Uniform I/O contract**: every program reads all of stdin and writes to
  stdout, so one objective grader serves both languages.
- **The Python reference is the oracle.** Expected outputs are produced by
  running `tasks/reference/py/<id>.py`; the spec text describes that behaviour.
  Three pilot tasks additionally have AiLang references
  (`tasks/reference/ail/`) that independently reproduce the same outputs — the
  cross-check validated by `harness/selftest.py`.

## Fairness controls (pre-committed)

1. **Matched loop.** Both languages get the same check + run-on-samples
   feedback each round, the same `--rounds` cap, and the same `--model` /
   `--temperature`.
2. **The context pack is counted, honestly.** AiLang's system prompt carries
   the full `SKILL.md` (~9k input tokens) — the unavoidable cost of using an
   out-of-distribution language. Python gets only a one-line house note,
   because being in-distribution genuinely needs no pack. Both are counted.
   Charging `SKILL.md` to AiLang is the point, not a handicap.
3. **Identical hidden cases.** The agent only ever sees the samples; grading is
   on the hidden cases. Same cases for both languages.
4. **Autonomous arm only.** The harness never edits the agent's output. This is
   deliberate: the project author is the world's foremost AiLang expert, so any
   run where *he* silently course-corrects measures best-case expert
   babysitting, not the cost an ordinary agent/operator pays. The honest number
   is the no-intervention one.

## Metrics

Per `(task, language, trial)`:

- **`total_tokens`** = Σ over every API call of `input_tokens + output_tokens`
  (the primary metric, for runs that reach a correct solution).
- **`output_tokens`** = Σ output only — isolates the "fewer characters to
  write" claim from the context-pack cost.
- **`first_check_clean`** — did the first attempt pass the static check.
- **`correct`** — did the final program pass all hidden cases within the cap.
- **`rounds_used`**, **`wall_ms`** (secondary).

Aggregated per `(task, language)` as median + correctness rate
(`harness/run_study.py --> results/summary_<tag>.json`).

## Token-accounting honesty

We report **uncached** tokens: `SKILL.md` is re-sent each round, so its cost is
counted every turn — the worst case for AiLang. Prompt caching would amortize
the pack across rounds; we note this and may report a cached variant, but the
headline number is uncached so it cannot be accused of hiding the pack cost.

## Known confounds (stated up front)

- **Model identity & training data.** Results are specific to the model used
  and the moment: as AiLang corpus grows (or doesn't) in future training sets,
  the out-of-distribution tax changes. Pin `--model`; report it.
- **Static-check asymmetry.** AiLang's `ailc check` is a *type* check; Python's
  is `pyright` if installed, else `py_compile` (syntax only). When `pyright` is
  absent, AiLang's loop catches more before running — an advantage *for*
  AiLang, so it cannot manufacture an H0 (anti-AiLang) result. Install `pyright`
  for the fairest run; the harness uses it automatically when present.
- **Tooling currency.** The study requires a current `ailc` that supports
  `check`/`run`/`--json` (an old PATH binary predating the agent-native CLI
  will not). See `README.md`.

## Falsification

If median `total_tokens` to a correct solution is **≥** Python's on a majority
of tasks, the "fewer tokens for an LLM" thesis is refuted *for this setting*,
and we say so plainly. If AiLang wins on some task categories and loses on
others, we report the breakdown rather than a single headline.

## Analysis plan

1. Per-task median `total_tokens`, AiLang vs Python; count tasks where AiLang is
   lower (sign test across the 15 tasks).
2. Same for `output_tokens` (the isolated terseness effect).
3. `first_check_clean` and `correct` rates per language — the out-of-distribution
   error tax, made visible.
4. Report the full table; no task is dropped after the fact.

## Reproducibility

Fixed `--model`, `--temperature` (default 0.0), fixed corpus, `T` trials.
Raw per-run records in `results/raw/<tag>/`, aggregates in
`results/summary_<tag>.json`. The corpus + grader are validated offline by
`harness/selftest.py` with no API access.

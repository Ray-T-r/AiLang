# AiLang Ablation Plan

Validation strategy for the aggressive token-reduction decisions adopted in v0.1.
Run after M5 is complete (parser + sema + codegen + GC + FFI).

## 1. Standard Task Set (20 tasks)

Tasks must span the full surface area — not just arithmetic. Each task is a one-paragraph natural-language spec.

| # | Task | Coverage |
|---|------|----------|
| 1  | FizzBuzz                            | `lp` range loop, `mt` |
| 2  | Sum of array                        | `lp` for-in, accumulator |
| 3  | Fibonacci (recursive)               | Function, recursion |
| 4  | Fibonacci (iterative)               | `lp` while, mutable |
| 5  | Reverse a string                    | String ops |
| 6  | Find max in array                   | If, comparison |
| 7  | Quicksort                           | Recursion + `|>` pipe + lambda |
| 8  | Binary search                       | While + comparison |
| 9  | Word frequency (map)                | Map literal, indexing |
| 10 | Read file, count lines              | I/O, string split |
| 11 | Parse integer with error            | `!T` result handling |
| 12 | Optional unwrap with default        | `?T` + `??` coalesce |
| 13 | Struct definition + method          | `st` + impl |
| 14 | Call libc `printf`                  | `ex` FFI |
| 15 | Call libc `qsort` with callback     | Function pointer FFI |
| 16 | Bubble sort                         | Nested `lp` |
| 17 | Tree node + DFS                     | Struct + recursion |
| 18 | Linked list reversal                | Mutable references |
| 19 | String contains substring           | String ops |
| 20 | Producer of 2D grid + print         | Nested loops + format |

Store specs at `spec/eval-tasks/*.md`. Each task has:
- `task.md` — natural-language spec given to the LLM
- `reference.ail` — hand-written reference solution
- `input.txt` (optional) — stdin
- `expected.txt` — expected stdout

## 2. Evaluation Harness

```
tools/eval/run.py
  for task in spec/eval-tasks/*:
    for model in [claude-opus-4-7, gpt-?]:
      for trial in 1..5:
        code = model.generate(task.md, system_prompt=AILANG_SPEC)
        result = compile_and_run(code, task.input)
        record(task, model, trial, {
          compiles: bool,
          executes: bool,
          output_matches: bool,
          token_count: int,
          ast_similarity_to_others: float,
        })
```

Metrics aggregated per (task, model):
- **Compile pass rate** = compiles / total
- **Execution pass rate** = output_matches / total
- **Avg token count** (model output)
- **Generation stability** = mean pairwise AST similarity across 5 trials

## 3. Acceptance Thresholds

The aggressive design "wins" if all three hold:

- **Compile pass rate ≥ 80%**
- **Execution pass rate ≥ 70%**
- **Avg token reduction vs Rust reference ≥ 30%**

Compute reference Rust token count via `tiktoken` (cl100k_base) on hand-written reference Rust solutions.

## 4. Rollback Order (if thresholds not met)

Rollback most-suspect rules first. Re-run the full eval after each rollback.

1. **Remove brace-less single-statement blocks**
   - Before: `if x>0 print(x)`
   - After: `if x>0 { print(x) }`
   - Hypothesis: ambiguous nested-if-else trips LLMs.

2. **`mt` → `match`**
   - Hypothesis: 2-letter `mt` is unseen in training data, LLM hallucinates `match`.

3. **`el`/`rt` → `else`/`return`**
   - Hypothesis: same as above, less severe.

4. **`lp` → split back into `for` and `while`**
   - Hypothesis: unified loop keyword forces LLM to disambiguate intent at parse time.

5. **Implicit `return` of last expression → require `rt`**
   - Hypothesis: LLMs trained on C/Java/Go expect explicit `return`.

Stop rolling back once thresholds are met. Document final rule set in `spec/ablation-results.md`.

## 5. What Counts as "Reasonable" Generation

The LLM is given:
- The AiLang spec (this design doc, abbreviated to <2000 tokens)
- 3 in-context examples of well-formed AiLang
- The task spec

No additional hints, no error feedback loop. Zero-shot capability of the spec itself is what we're measuring — that's what real users will hit.

## 6. Open Questions to Resolve Pre-Eval

- [ ] Which LLM tokenizer to use as the authoritative count? Recommend `tiktoken cl100k_base` for cross-model comparability.
- [ ] Should the spec given to the LLM be the formal EBNF or a tutorial-style overview? Recommend tutorial — more token-efficient *for the system prompt* and likely better for generation quality.
- [ ] How many trials per (task, model)? 5 is a starting point; bump to 10 if variance is high.

## 7. Logging

Per-trial record to `eval-results/{date}/{model}/{task}/trial-{n}.json` with: code, compile output, run output, token counts, timestamps.

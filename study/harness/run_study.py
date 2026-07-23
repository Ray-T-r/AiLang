#!/usr/bin/env python3
"""Drive the autonomous agent loop for the token-cost study.

For each (task, language, trial) it runs a fixed, fully-autonomous loop — NO
human intervention — and records the total tokens spent to reach a working
program, plus first-pass check-clean rate and final correctness:

  generate  ->  static check (ailc check --json / pyright|py_compile)
            ->  run on the visible SAMPLE cases
            ->  if any fail, feed the failure back and regenerate (<= --rounds)
  then grade the final program on the HIDDEN cases.

Fairness (see ../methodology.md):
  * Both languages get the same check + run-on-samples feedback loop.
  * AiLang's system prompt carries the SKILL.md context pack (a real, counted
    cost of an out-of-distribution language); Python gets a short house note.
  * Identical hidden cases; the agent only ever sees the samples.

Zero third-party deps — talks to the Anthropic Messages API over urllib.
Set ANTHROPIC_API_KEY to run live, or pass --mock to validate the loop wiring
offline using the reference solutions.

Examples:
  python3 run_study.py --mock --tasks rle_encode,two_sum,bigint_add --langs ailang,python
  python3 run_study.py --tasks all --langs ailang,python --trials 3 --rounds 5 \
      --model claude-sonnet-4-6
"""
import argparse, json, os, re, statistics, sys, tempfile, time, urllib.request
import runner

API_URL = "https://api.anthropic.com/v1/messages"
API_VERSION = "2023-06-01"
DEEPSEEK_URL = "https://api.deepseek.com/chat/completions"
# Cache-read price relative to full-price input, per provider (for effective cost).
# Anthropic cache read ~0.1x; DeepSeek cache hit ~0.25x of a miss.
CACHE_READ_MULT = {"anthropic": 0.1, "deepseek": 0.25}
HERE = os.path.dirname(os.path.abspath(__file__))
SKILL_PATH = os.path.join(runner.REPO, "skill", "SKILL.md")
RESULTS = os.path.join(runner.REPO, "study", "results")

with open(SKILL_PATH) as f:
    SKILL = f.read()

CONTRACT = (
    "Write a complete, self-contained program that reads ALL of standard input "
    "and writes the answer to standard output, exactly as the task specifies. "
    "No prompts, no extra text — only the specified output. Return the program "
    "in a single fenced code block and nothing else."
)


def system_prompt(lang):
    if lang == "ailang":
        return (SKILL + "\n\n# Harness rules\n" + CONTRACT +
                " Write AiLang (.ail). Top-level statements are the implicit "
                "main; read input with read_stdin().")
    return ("You are an expert Python 3 programmer.\n\n# Harness rules\n" +
            CONTRACT + " Write Python 3. Read input with sys.stdin.read().")


def user_prompt(task):
    samples = "\n\n".join(
        f"Example {i+1}:\nINPUT:\n{c['in']}\nOUTPUT:\n{c['out']}"
        for i, c in enumerate(task["samples"]))
    return f"# Task: {task['title']}\n\n{task['spec']}\n\n{samples}"


CODE_RE = re.compile(r"```[a-zA-Z0-9_+.-]*\n(.*?)```", re.S)


def extract_code(text):
    blocks = CODE_RE.findall(text)
    if blocks:
        return blocks[-1].strip("\n")
    return text.strip()


def _rejects_sampling(model):
    # temperature/top_p/top_k are removed (HTTP 400) on Opus 4.7+/4.8 and the
    # Fable/Mythos 5 family. Sonnet 4.6 and Opus 4.6 still accept them, so the
    # pre-registered temperature=0.0 control holds for the headline sonnet run.
    m = model.lower()
    return (m.startswith("claude-opus-4-7") or m.startswith("claude-opus-4-8")
            or m.startswith("claude-fable") or m.startswith("claude-mythos"))


def call_anthropic(model, system, messages, max_tokens, temperature, api_key,
                   cache=False):
    # Prompt caching is GA on current models — no anthropic-beta header needed.
    # Caching the ~9k SKILL pack on the system block measures the owner's real
    # daily cost (pack paid once via cache reads) instead of the worst-case
    # uncached re-send the pre-registered headline reports.
    sys_field = ([{"type": "text", "text": system,
                   "cache_control": {"type": "ephemeral"}}] if cache else system)
    body = {"model": model, "max_tokens": max_tokens,
            "system": sys_field, "messages": messages}
    if not _rejects_sampling(model):
        body["temperature"] = temperature
    req = urllib.request.Request(
        API_URL, data=json.dumps(body).encode(),
        headers={"x-api-key": api_key, "anthropic-version": API_VERSION,
                 "content-type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=180) as resp:
        data = json.load(resp)
    text = "".join(b.get("text", "") for b in data.get("content", [])
                   if b.get("type") == "text")
    u = data.get("usage", {})
    return (text, u.get("input_tokens", 0), u.get("output_tokens", 0),
            u.get("cache_read_input_tokens", 0),
            u.get("cache_creation_input_tokens", 0))


def call_deepseek(model, system, messages, max_tokens, temperature, api_key,
                  cache=False):
    # DeepSeek is OpenAI-compatible: the system prompt is the first message, not
    # a separate field. Context caching is AUTOMATIC (no cache_control); usage
    # reports prompt_cache_hit_tokens / prompt_cache_miss_tokens, so the cached
    # variant comes for free — the `cache` flag is a no-op here.
    msgs = [{"role": "system", "content": system}] + messages
    body = {"model": model, "max_tokens": max_tokens,
            "temperature": temperature, "messages": msgs, "stream": False}
    req = urllib.request.Request(
        DEEPSEEK_URL, data=json.dumps(body).encode(),
        headers={"Authorization": "Bearer " + api_key,
                 "content-type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=180) as resp:
        data = json.load(resp)
    text = (data["choices"][0]["message"].get("content") or "")
    u = data.get("usage", {})
    prompt = u.get("prompt_tokens", 0)
    hit = u.get("prompt_cache_hit_tokens")
    if hit is None:
        hit = (u.get("prompt_tokens_details") or {}).get("cached_tokens", 0)
    miss = u.get("prompt_cache_miss_tokens")
    if miss is None:
        miss = max(0, prompt - hit)
    # miss = full-price input; hit = cheap cached input; no separate write premium.
    return (text, miss, u.get("completion_tokens", 0), hit, 0)


def mock_provider(lang, task, *_):
    """Return the reference solution as if the model produced it (offline)."""
    ref = os.path.join(runner.REPO, "study", "tasks", "reference",
                       runner.EXT[lang], task["id"] + "." + runner.EXT[lang])
    if not os.path.exists(ref):
        raise FileNotFoundError(f"no {lang} reference for {task['id']} (mock)")
    with open(ref) as f:
        src = f.read()
    fence = "ail" if lang == "ailang" else "python"
    text = f"```{fence}\n{src}```"
    return (text, max(1, len(SKILL) // 4 if lang == "ailang" else 50),
            len(src) // 4, 0, 0)


def run_one(task, lang, trial, args, provider, api_key, workdir):
    messages = [{"role": "user", "content": user_prompt(task)}]
    sysprompt = system_prompt(lang)
    in_tot = out_tot = cr_tot = cc_tot = 0
    rounds_used = 0
    first_check_clean = None
    samples_ok = False
    final_src = None
    t0 = time.time()
    for r in range(args.rounds):
        rounds_used = r + 1
        if provider is mock_provider:
            text, it, ot, cr, cc = mock_provider(lang, task)
        else:
            text, it, ot, cr, cc = provider(
                args.model, sysprompt, messages, args.max_tokens,
                args.temperature, api_key, args.cache)
        in_tot += it
        out_tot += ot
        cr_tot += cr
        cc_tot += cc
        code = extract_code(text)
        messages.append({"role": "assistant", "content": text})
        src = os.path.join(workdir, f"sol.{runner.EXT[lang]}")
        with open(src, "w") as f:
            f.write(code + ("\n" if not code.endswith("\n") else ""))
        clean, diag = runner.check(lang, src)
        if first_check_clean is None:
            first_check_clean = clean
        if not clean:
            messages.append({"role": "user", "content":
                "Static check reported errors:\n" + diag +
                "\nReturn the COMPLETE corrected program in a single code block."})
            continue
        res = runner.grade(lang, src, task["samples"], workdir=workdir)
        final_src = src
        if res["compiled"] and res["passed"] == res["total"]:
            samples_ok = True
            break
        fb = res["fails"][0] if res["fails"] else None
        if fb is None:  # compiled but failed before running any case
            messages.append({"role": "user", "content":
                "Your program failed to run on the samples (" +
                res.get("compile_log", "")[:300] +
                "). Return the COMPLETE corrected program in a single code block."})
            continue
        messages.append({"role": "user", "content":
            f"Your program compiled but a sample case failed.\nINPUT:\n{fb['in']}\n"
            f"EXPECTED:\n{fb['expect']}\nGOT:\n{fb['got']}\n"
            f"Return the COMPLETE corrected program in a single code block."})
    if final_src:
        hidden = runner.grade(lang, final_src, task["hidden"], workdir=workdir)
    else:
        hidden = {"compiled": False, "passed": 0, "total": len(task["hidden"])}
    return {
        "task": task["id"], "category": task["category"], "lang": lang,
        "trial": trial, "model": args.model, "rounds_used": rounds_used,
        "first_check_clean": bool(first_check_clean),
        "samples_passed": samples_ok,
        "hidden_passed": hidden["passed"], "hidden_total": hidden["total"],
        "correct": hidden.get("compiled", False) and hidden["passed"] == hidden["total"],
        "input_tokens": in_tot, "output_tokens": out_tot,
        "cache_read_input_tokens": cr_tot,
        "cache_creation_input_tokens": cc_tot,
        # Raw token throughput: full-price input + cached input + writes + output.
        # input_tokens is the full-price remainder only, so cache_read/creation are
        # added back here. For an uncached run (cr==cc==0) this is in+out as before.
        "total_tokens": in_tot + cr_tot + cc_tot + out_tot,
        # Cache-billed equivalent in full-price-token units: writes ~1.25x, reads
        # at the provider's discount (CACHE_READ_MULT). Equals total_tokens when uncached.
        "effective_total_tokens": in_tot + out_tot + round(
            cc_tot * 1.25 + cr_tot * CACHE_READ_MULT.get(args.provider, 0.1)),
        "wall_ms": int((time.time() - t0) * 1000),
    }


def aggregate(records):
    by = {}
    for rec in records:
        by.setdefault((rec["task"], rec["lang"]), []).append(rec)
    rows = []
    for (task, lang), recs in sorted(by.items()):
        correct = [r for r in recs if r["correct"]]
        basis = correct or recs
        toks = [r["total_tokens"] for r in basis]
        eff = [r.get("effective_total_tokens", r["total_tokens"]) for r in basis]
        rows.append({
            "task": task, "lang": lang, "trials": len(recs),
            "correct_rate": round(len(correct) / len(recs), 3),
            "first_check_clean_rate": round(
                sum(r["first_check_clean"] for r in recs) / len(recs), 3),
            "median_total_tokens": int(statistics.median(toks)),
            "median_effective_total_tokens": int(statistics.median(eff)),
            "median_output_tokens": int(statistics.median(
                [r["output_tokens"] for r in basis])),
            "median_rounds": statistics.median([r["rounds_used"] for r in recs]),
        })
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tasks", default="all", help="comma ids or 'all' or 'pilot'")
    ap.add_argument("--langs", default="ailang,python")
    ap.add_argument("--trials", type=int, default=3)
    ap.add_argument("--rounds", type=int, default=5)
    ap.add_argument("--provider", choices=["anthropic", "deepseek"], default=None,
                    help="LLM provider (default: auto from whichever API key is set)")
    ap.add_argument("--model", default=None,
                    help="model id (default: provider default — "
                         "claude-sonnet-4-6 / deepseek-chat)")
    ap.add_argument("--max-tokens", type=int, default=4096, dest="max_tokens")
    ap.add_argument("--temperature", type=float, default=0.0)
    ap.add_argument("--cache", action="store_true",
                    help="cache the SKILL pack on the system block (cached-cost "
                         "variant); reports effective_total_tokens. Off = the "
                         "pre-registered uncached headline.")
    ap.add_argument("--mock", action="store_true", help="offline wiring test")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    doc = runner.load_tasks()
    all_tasks = doc["tasks"]
    if args.tasks == "all":
        sel = all_tasks
    elif args.tasks == "pilot":
        sel = [t for t in all_tasks if t["id"] in doc["pilot"]]
    else:
        want = set(args.tasks.split(","))
        sel = [t for t in all_tasks if t["id"] in want]
    langs = args.langs.split(",")

    PROVIDERS = {  # provider -> (env key, default model, call fn)
        "anthropic": ("ANTHROPIC_API_KEY", "claude-sonnet-4-6", call_anthropic),
        "deepseek": ("DEEPSEEK_API_KEY", "deepseek-chat", call_deepseek),
    }
    if args.provider is None:  # auto-detect from whichever key is present
        if os.environ.get("ANTHROPIC_API_KEY"):
            args.provider = "anthropic"
        elif os.environ.get("DEEPSEEK_API_KEY"):
            args.provider = "deepseek"
        else:
            args.provider = "anthropic"
    env_key, default_model, provider_fn = PROVIDERS[args.provider]
    if args.model is None:
        args.model = default_model
    api_key = os.environ.get(env_key)
    provider = mock_provider if args.mock else provider_fn
    if not args.mock and not api_key:
        print(f"ERROR: set {env_key} (provider={args.provider}), or pass --mock.",
              file=sys.stderr)
        return 2

    tag = args.out or ("mock" if args.mock else
                       args.model.replace("/", "-") + ("-cached" if args.cache else ""))
    raw_dir = os.path.join(RESULTS, "raw", tag)
    os.makedirs(raw_dir, exist_ok=True)
    records = []
    for task in sel:
        for lang in langs:
            if args.mock and lang == "ailang" and not task.get("pilot"):
                continue  # no AiLang reference outside the pilot
            for trial in range(args.trials):
                wd = tempfile.mkdtemp(prefix="ail_study_")
                try:
                    rec = run_one(task, lang, trial, args, provider, api_key, wd)
                finally:
                    import shutil
                    shutil.rmtree(wd, ignore_errors=True)
                records.append(rec)
                with open(os.path.join(raw_dir,
                          f"{task['id']}_{lang}_{trial}.json"), "w") as f:
                    json.dump(rec, f, indent=2, ensure_ascii=False)
                print(f"  {task['id']:<18} {lang:<7} t{trial} "
                      f"correct={rec['correct']} tokens={rec['total_tokens']} "
                      f"rounds={rec['rounds_used']}")

    summary = {"tag": tag, "provider": args.provider, "model": args.model,
               "mock": args.mock, "cache": args.cache, "trials": args.trials,
               "rounds": args.rounds, "n_records": len(records),
               "rows": aggregate(records)}
    sum_path = os.path.join(RESULTS, f"summary_{tag}.json")
    with open(sum_path, "w") as f:
        json.dump(summary, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(f"\nwrote {len(records)} records to {raw_dir}")
    print(f"wrote summary to {sum_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

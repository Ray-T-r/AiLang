#!/usr/bin/env python3
"""Objective compile / run / grade primitives shared by the study.

Used by selftest.py (offline corpus + grader validation) and run_study.py (the
live agent loop). No third-party dependencies.

Environment:
  AILC         path to the AiLang compiler (default: 'ailc' on PATH)
  AILANG_STD   std/ directory for `im "std/..."` (default: repo std/)
  STUDY_TIMEOUT seconds per compile/run (default: 30)
"""
import json, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))  # study/harness -> study -> repo


def _resolve_ailc():
    # Prefer an explicit AILC, then the freshly-bootstrapped repo binary, then
    # PATH. NOTE: an old `ailc` on PATH may predate `check`/`run`/`--json`; the
    # study needs a current build (see study/README.md).
    if os.environ.get("AILC"):
        return os.environ["AILC"]
    local = os.path.join(REPO, "selfhost", "ailc")
    if os.path.exists(local):
        return local
    return "ailc"


AILC = _resolve_ailc()
AILANG_STD = os.environ.get("AILANG_STD", os.path.join(REPO, "std"))
TIMEOUT = int(os.environ.get("STUDY_TIMEOUT", "30"))
_PYRIGHT = shutil.which("pyright")

LANGS = ("ailang", "python")
EXT = {"ailang": "ail", "python": "py"}


def _env():
    e = dict(os.environ)
    e["AILANG_STD"] = AILANG_STD
    return e


def _run(cmd, stdin="", timeout=TIMEOUT):
    try:
        p = subprocess.run(cmd, input=stdin, capture_output=True, text=True,
                           timeout=timeout, env=_env())
        return {"stdout": p.stdout, "stderr": p.stderr, "exit": p.returncode,
                "timed_out": False}
    except subprocess.TimeoutExpired:
        return {"stdout": "", "stderr": "timeout", "exit": -1, "timed_out": True}


def check(lang, src_path):
    """Static check only (no run). Returns (clean: bool, diagnostics: str).

    AiLang: `ailc check --json` (a real type check).
    Python: pyright if available, else `py_compile` (syntax check). The
    asymmetry is documented in methodology.md.
    """
    if lang == "ailang":
        r = _run([AILC, "check", "--json", src_path])
        return r["exit"] == 0, (r["stdout"] + r["stderr"]).strip()
    if _PYRIGHT:
        r = _run([_PYRIGHT, "--outputjson", src_path])
        return r["exit"] == 0, r["stdout"].strip()
    r = _run([sys.executable, "-m", "py_compile", src_path])
    return r["exit"] == 0, r["stderr"].strip()


def prepare(lang, src_path, workdir):
    """Make the program runnable. Returns (ok, artifact, log).

    AiLang compiles to a native binary; Python uses the source directly.
    """
    if lang == "python":
        return True, src_path, ""
    out = os.path.join(workdir, "prog")
    r = _run([AILC, src_path, out])
    if r["exit"] != 0 or not os.path.exists(out):
        return False, None, (r["stdout"] + r["stderr"]).strip()
    return True, out, ""


def run_artifact(lang, artifact, stdin):
    cmd = [artifact] if lang == "ailang" else [sys.executable, artifact]
    return _run(cmd, stdin=stdin)


def _norm(s):
    return s.rstrip("\n")


def grade(lang, src_path, cases, workdir=None):
    """Compile (if needed) and run src against cases. Returns a dict:
       {compiled, passed, total, fails:[{idx,in,expect,got}]}
    A case passes when normalized stdout == normalized expected.
    """
    own_tmp = workdir is None
    if own_tmp:
        workdir = tempfile.mkdtemp(prefix="ail_grade_")
    try:
        ok, artifact, log = prepare(lang, src_path, workdir)
        if not ok:
            return {"compiled": False, "passed": 0, "total": len(cases),
                    "fails": [], "compile_log": log}
        passed, fails = 0, []
        for idx, c in enumerate(cases):
            r = run_artifact(lang, artifact, c["in"])
            got = _norm(r["stdout"])
            if not r["timed_out"] and r["exit"] == 0 and got == _norm(c["out"]):
                passed += 1
            else:
                fails.append({"idx": idx, "in": c["in"], "expect": c["out"],
                              "got": got, "stderr": r["stderr"][:400],
                              "exit": r["exit"], "timed_out": r["timed_out"]})
        return {"compiled": True, "passed": passed, "total": len(cases),
                "fails": fails, "compile_log": ""}
    finally:
        if own_tmp:
            shutil.rmtree(workdir, ignore_errors=True)


def load_tasks(path=None):
    path = path or os.path.join(REPO, "study", "tasks", "tasks.json")
    with open(path) as f:
        return json.load(f)


if __name__ == "__main__":
    # quick manual: python3 runner.py <lang> <src.(ail|py)> <task_id> [samples|hidden|all]
    lang, src, tid = sys.argv[1], sys.argv[2], sys.argv[3]
    which = sys.argv[4] if len(sys.argv) > 4 else "all"
    doc = load_tasks()
    task = next(t for t in doc["tasks"] if t["id"] == tid)
    cases = (task["samples"] if which in ("samples", "all") else []) + \
            (task["hidden"] if which in ("hidden", "all") else [])
    res = grade(lang, src, cases)
    print(json.dumps(res, indent=2, ensure_ascii=False))
    sys.exit(0 if res.get("compiled") and res["passed"] == res["total"] else 1)

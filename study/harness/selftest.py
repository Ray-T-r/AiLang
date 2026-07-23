#!/usr/bin/env python3
"""Offline validation of the study corpus and the grader — NO network, NO API.

Two checks:
  1. Corpus consistency: every Python reference passes every case of its task.
     (Trivially true since the oracle defines the outputs, but it exercises the
     full grade() path and confirms determinism + the comparison logic.)
  2. AiLang grader path: each pilot task's AiLang reference compiles, runs, and
     reproduces the SAME expected outputs as the Python oracle — an independent
     cross-check that ailc + the grader machinery work end to end.

Exit 0 iff both checks fully pass. Run: python3 study/harness/selftest.py
"""
import json, os, sys
import runner

HERE = os.path.dirname(os.path.abspath(__file__))
AIL_REF = os.path.join(runner.REPO, "study", "tasks", "reference", "ail")
PY_REF = os.path.join(runner.REPO, "study", "tasks", "reference", "py")


def main():
    doc = runner.load_tasks()
    tasks = doc["tasks"]
    fails = 0

    print(f"== corpus consistency: {len(tasks)} Python references ==")
    for t in tasks:
        cases = t["samples"] + t["hidden"]
        src = os.path.join(PY_REF, t["id"] + ".py")
        res = runner.grade("python", src, cases)
        ok = res["compiled"] and res["passed"] == res["total"]
        print(f"  [{'OK ' if ok else 'FAIL'}] {t['id']:<18} "
              f"{res['passed']}/{res['total']}")
        if not ok:
            fails += 1
            for fc in res["fails"][:3]:
                print(f"      in={fc['in']!r} expect={fc['expect']!r} "
                      f"got={fc['got']!r} stderr={fc.get('stderr','')!r}")

    pilot = doc["pilot"]
    print(f"\n== AiLang grader path: {len(pilot)} pilot references "
          f"(AILC={runner.AILC}) ==")
    by_id = {t["id"]: t for t in tasks}
    for tid in pilot:
        t = by_id[tid]
        cases = t["samples"] + t["hidden"]
        src = os.path.join(AIL_REF, tid + ".ail")
        if not os.path.exists(src):
            print(f"  [FAIL] {tid:<18} missing {src}")
            fails += 1
            continue
        res = runner.grade("ailang", src, cases)
        ok = res["compiled"] and res["passed"] == res["total"]
        status = "OK " if ok else ("NOCOMPILE" if not res["compiled"] else "FAIL")
        print(f"  [{status}] {tid:<18} {res['passed']}/{res['total']}")
        if not res["compiled"]:
            print(f"      compile_log: {res['compile_log'][:600]}")
            fails += 1
        elif not ok:
            fails += 1
            for fc in res["fails"][:5]:
                print(f"      in={fc['in']!r} expect={fc['expect']!r} "
                      f"got={fc['got']!r} stderr={fc.get('stderr','')!r}")

    print()
    if fails:
        print(f"SELFTEST FAILED: {fails} task(s) failed")
        return 1
    print("SELFTEST PASSED: corpus consistent + AiLang grader path validated")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env bash
# selfhost/verify.sh — prove the AiLang-written compiler self-hosts.
#
# Two proofs, in increasing strength:
#   A. Sample fidelity — the bootstrap (built by the real ailangc) compiles each
#      examples-selfhost/*.ail to a native binary whose output matches `ailangc`.
#   B. STRICT FIXPOINT — the bootstrap compiles *its own source* (main.ail), and
#      that self-built compiler compiles main.ail again; the two emitted C files
#      must be byte-identical (stage2 == stage3). The compiler is its own
#      fixpoint. The stage-2 compiler is also re-checked against every sample, so
#      "byte-identical" can't pass by both stages being equally broken.
#
# Exit 0 iff every sample matches AND the fixpoint holds. Run from the repo root:
#   bash selfhost/verify.sh
set -euo pipefail

cd "$(dirname "$0")/.."
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "==> building the bootstrap compiler (selfhost/main.ail) with ailangc"
ailangc compile selfhost/main.ail -o "$TMP/ailc0" | tail -1

# Run a command with a hard wall-clock cap so a buggy sample can never hang the
# suite (`timeout` isn't always present on macOS; perl's alarm is portable).
guard() { perl -e 'alarm shift; exec @ARGV' "$@"; }

# Compile every sample with $1 and diff its output against the reference ailangc.
check_samples() {
  local cc="$1" label="$2" rc=0
  for src in examples-selfhost/*.ail; do
    local name; name="$(basename "$src" .ail)"
    guard 12 "$cc" "$src" "$TMP/$name" >/dev/null 2>&1
    guard 8 "$TMP/$name" > "$TMP/$name.self.out" 2>&1 || true
    guard 8 ailangc run "$src" > "$TMP/$name.ref.out" 2>&1 || true
    if diff -q "$TMP/$name.self.out" "$TMP/$name.ref.out" >/dev/null; then
      echo "    ok   $name  ($label == ailangc)"
    else
      echo "    FAIL $name"; diff "$TMP/$name.self.out" "$TMP/$name.ref.out" || true; rc=1
    fi
  done
  return "$rc"
}

fail=0
echo "==> A. sample fidelity (bootstrap output == ailangc)"
check_samples "$TMP/ailc0" "self-hosted output" || fail=1

echo "==> B. strict fixpoint (stage2 == stage3)"
# stage1 = ailic0 (= ailangc(main.ail)); stage2 = stage1(main.ail); stage3 = stage2(main.ail)
guard 60 "$TMP/ailc0"  selfhost/main.ail "$TMP/stage2" >/dev/null 2>&1
guard 60 "$TMP/stage2" selfhost/main.ail "$TMP/stage3" >/dev/null 2>&1
if [ -f "$TMP/stage2.c" ] && [ -f "$TMP/stage3.c" ] && diff -q "$TMP/stage2.c" "$TMP/stage3.c" >/dev/null; then
  echo "    ok   stage2.c == stage3.c  ($(wc -l < "$TMP/stage2.c" | tr -d ' ') lines, byte-identical)"
else
  echo "    FAIL fixpoint not reached"; diff "$TMP/stage2.c" "$TMP/stage3.c" 2>&1 | head -20 || true; fail=1
fi
echo "    -- regressing samples through the self-compiled stage-2 compiler --"
check_samples "$TMP/stage2" "stage2 output" || fail=1

if [ "$fail" -eq 0 ]; then
  echo "==> ✅ self-hosting verified: samples match AND stage2 == stage3 (strict fixpoint)"
else
  echo "==> ❌ verification failed"
fi
exit "$fail"

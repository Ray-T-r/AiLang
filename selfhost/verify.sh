#!/usr/bin/env bash
# selfhost/verify.sh — prove the AiLang self-hosted compiler is correct, with
# NO external reference compiler (no Rust `ailangc`). Two self-contained proofs:
#
#   A. Sample fidelity — the seed-built `ailc` compiles each
#      examples-selfhost/*.ail to a native binary whose stdout matches the
#      committed examples-selfhost/expected/<name>.out fixture. Those fixtures
#      are frozen known-good output (byte-for-byte equal to the Rust `ailangc`
#      at the time the repo was split), so this is a regression guard against
#      that reference.
#
#   B. Strict fixpoint — `ailc` compiling main.ail reproduces the committed
#      seed (seed/ailc.c) byte-for-byte. Run by bootstrap.sh, which also builds
#      `ailc` from that seed in the first place — so the compiler is its own
#      fixed point with zero Rust in the loop.
#
# Exit 0 iff every sample matches its fixture AND the fixpoint holds.
#   bash selfhost/verify.sh
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"

# The std/ library lives at the repo root; point AILANG_STD at it so the
# stdlib.ail sample's `im "std/…"` imports (and std/math auto-imports) resolve —
# the same env var the installer sets. main.ail imports only src/, so this does
# not affect the bootstrap or the fixpoint.
export AILANG_STD="$ROOT"

echo "==> building ./ailc from the seed + checking the fixpoint (bootstrap.sh)"
bash selfhost/bootstrap.sh
AILC="$ROOT/selfhost/ailc"

# Hard wall-clock cap so a buggy sample can never hang the suite (macOS lacks a
# reliable `timeout`; perl's alarm is portable).
guard() { perl -e 'alarm shift; exec @ARGV' "$@"; }

echo "==> A. sample fidelity (ailc output == committed fixtures)"
fail=0; n=0
for src in examples-selfhost/*.ail; do
  name="$(basename "$src" .ail)"
  exp="examples-selfhost/expected/$name.out"
  [ -f "$exp" ] || { echo "    skip $name (no fixture)"; continue; }
  guard 30 "$AILC" "$src" "/tmp/vf_$name" >/dev/null 2>&1 || true
  # A committed examples-selfhost/<name>.in, when present, is fed as stdin —
  # lets samples that read stdin (read_stdin/read_line) be deterministic.
  if [ -f "examples-selfhost/$name.in" ]; then
    guard 10 "/tmp/vf_$name" <"examples-selfhost/$name.in" >"/tmp/vf_$name.out" 2>&1 || true
  else
    guard 10 "/tmp/vf_$name" >"/tmp/vf_$name.out" 2>&1 || true
  fi
  if diff -q "/tmp/vf_$name.out" "$exp" >/dev/null 2>&1; then
    echo "    ok   $name"; n=$((n+1))
  else
    echo "    FAIL $name"; diff "$exp" "/tmp/vf_$name.out" 2>&1 | head -6; fail=1
  fi
  rm -f "/tmp/vf_$name" "/tmp/vf_$name.out" "/tmp/vf_$name.c"
done

echo "==> C. type checker catches mistakes (negative tests)"
AILC="$AILC" bash selfhost/tests/neg/neg.sh || fail=1

rm -f selfhost/ailc
if [ "$fail" -eq 0 ]; then
  echo "==> ✅ self-hosting verified: $n samples match their fixtures AND the seed is the strict fixpoint of main.ail (no Rust involved)"
else
  echo "==> ❌ verification failed"; exit 1
fi

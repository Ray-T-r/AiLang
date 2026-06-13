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

# Loopback samples (sock_echo/http_loopback/ws_echo/web_loopback) bind
# 127.0.0.1 on this base port + a small per-sample offset; PID-derived so
# concurrent verify runs don't collide. Samples never print the port.
export AILANG_TEST_PORT=$(( 20000 + ($$ % 20000) ))

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
  [ -f "$exp" ] || { echo "    FAIL $name (missing fixture expected/$name.out — helper modules belong in examples-selfhost/lib/)"; fail=1; continue; }
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

echo "==> B. compile-only std-module stubs (compile + link, never executed)"
have_mysql=0
pkg-config --exists mysqlclient 2>/dev/null && have_mysql=1
pkg-config --exists libmariadb 2>/dev/null && have_mysql=1
[ -d "$(brew --prefix mysql-client 2>/dev/null || true)" ] && have_mysql=1
for src in selfhost/tests/compileonly/*.ail; do
  name="$(basename "$src" .ail)"
  if [ "$name" = "mysql" ] && [ "$have_mysql" -eq 0 ]; then
    echo "    skip mysql (libmysqlclient not installed)"; continue
  fi
  if guard 60 "$AILC" "$src" "/tmp/co_$name" >/dev/null 2>&1; then
    echo "    ok   $name (compiles + links)"
  else
    echo "    FAIL $name (did not compile)"; fail=1
  fi
  rm -f "/tmp/co_$name" "/tmp/co_$name.c"
done

echo "==> C. type checker catches mistakes (negative tests)"
AILC="$AILC" bash selfhost/tests/neg/neg.sh || fail=1

echo "==> D. CLI guards (bad flags / missing input never write junk)"
cli_ok=1
"$AILC" --help >/dev/null 2>&1 || { echo "    FAIL: --help exited nonzero"; cli_ok=0; }
"$AILC" --version >/dev/null 2>&1 || { echo "    FAIL: --version exited nonzero"; cli_ok=0; }
cli_out="$("$AILC" --bogus-flag 2>&1)" && { echo "    FAIL: --bogus-flag exited 0"; cli_ok=0; } || true
printf '%s' "$cli_out" | grep -q "unknown flag" || { echo "    FAIL: wrong message for an unknown flag"; cli_ok=0; }
if "$AILC" "/nonexistent_$$.ail" "/tmp/cli_x_$$" >/dev/null 2>&1; then
  echo "    FAIL: missing input exited 0"; cli_ok=0
fi
if ls ./--bogus-flag* ./-bogus* "/tmp/cli_x_$$"* >/dev/null 2>&1; then
  echo "    FAIL: junk files were created"; cli_ok=0
fi
# `run` = compile + execute in one step: its stdout must equal the sample's
# committed fixture, and no compiler chatter ("compiled …") may leak. `check` =
# parse + type-check only: prints "ok" + exits 0 on a clean program, and exits
# nonzero on a real type error (the agent's fast inner loop).
rsamp=""
for cand in fib hello fizzbuzz arrays sum; do
  if [ -f "examples-selfhost/$cand.ail" ] && [ ! -f "examples-selfhost/$cand.in" ] && [ -f "examples-selfhost/expected/$cand.out" ]; then rsamp="$cand"; break; fi
done
if [ -n "$rsamp" ]; then
  run_out="$(guard 30 "$AILC" run "examples-selfhost/$rsamp.ail" 2>&1)" || { echo "    FAIL: 'run $rsamp' exited nonzero"; cli_ok=0; }
  [ "$run_out" = "$(cat "examples-selfhost/expected/$rsamp.out")" ] || { echo "    FAIL: 'run $rsamp' stdout != fixture"; cli_ok=0; }
  printf '%s' "$run_out" | grep -q "compiled " && { echo "    FAIL: 'run' leaked compiler chatter"; cli_ok=0; } || true
  guard 30 "$AILC" check "examples-selfhost/$rsamp.ail" 2>&1 | grep -q "^ok$" || { echo "    FAIL: 'check $rsamp' did not print ok on a clean program"; cli_ok=0; }
fi
neg_one="selfhost/tests/neg/aggmismatch.ail"
[ -f "$neg_one" ] || neg_one="$(ls selfhost/tests/neg/*.ail 2>/dev/null | head -1)"
if [ -n "$neg_one" ] && "$AILC" check "$neg_one" >/dev/null 2>&1; then
  echo "    FAIL: 'check' exited 0 on a known type error ($neg_one)"; cli_ok=0
fi
"$AILC" run   >/dev/null 2>&1 && { echo "    FAIL: 'run' with no input exited 0"; cli_ok=0; } || true
"$AILC" check >/dev/null 2>&1 && { echo "    FAIL: 'check' with no input exited 0"; cli_ok=0; } || true
# --json: machine-readable JSONL for agent consumers. A clean check is the
# object {"severity":"ok"}; an error carries a code + nonzero exit.
if [ -n "$rsamp" ]; then
  "$AILC" check --json "examples-selfhost/$rsamp.ail" 2>&1 | grep -q '{"severity":"ok"}' || { echo "    FAIL: 'check --json' on a clean program is not {\"severity\":\"ok\"}"; cli_ok=0; }
fi
if [ -n "$neg_one" ]; then
  jout="$("$AILC" check --json "$neg_one" 2>&1)" && { echo "    FAIL: 'check --json' exited 0 on a type error"; cli_ok=0; } || true
  printf '%s' "$jout" | grep -q '"severity":"error"' || { echo "    FAIL: 'check --json' emitted no error object"; cli_ok=0; }
  printf '%s' "$jout" | grep -q '"code":"AIL'        || { echo "    FAIL: 'check --json' error carried no code"; cli_ok=0; }
fi
# assert(cond,msg) + `ailc test`: a passing assert exits 0, a failing one aborts
# nonzero, and the runner tallies pass/fail. Also proves a program WITH assert
# compiles + links + runs (the gated prelude helper is reached).
vt_pass="/tmp/vt_pass_$$.ail"; vt_fail="/tmp/vt_fail_$$.ail"
printf 'assert(1+1==2, "ok")\nprintln("done")\n' > "$vt_pass"
printf 'assert(1==2, "boom")\n'                  > "$vt_fail"
"$AILC" test "$vt_pass" >/dev/null 2>&1 || { echo "    FAIL: 'test' on a passing assert exited nonzero"; cli_ok=0; }
"$AILC" test "$vt_fail" >/dev/null 2>&1 && { echo "    FAIL: 'test' on a failing assert exited 0"; cli_ok=0; } || true
tsum="$("$AILC" test "$vt_pass" "$vt_fail" 2>/dev/null || true)"   # nonzero exit (1 test fails) is expected; capture, then assert
printf '%s' "$tsum" | grep -q "1 passed, 1 failed" || { echo "    FAIL: 'test' summary wrong"; cli_ok=0; }
rm -f "$vt_pass" "$vt_fail"
[ "$cli_ok" -eq 1 ] && echo "    ok   CLI guards" || fail=1

rm -f selfhost/ailc
if [ "$fail" -eq 0 ]; then
  echo "==> ✅ self-hosting verified: $n samples match their fixtures AND the seed is the strict fixpoint of main.ail (no Rust involved)"
else
  echo "==> ❌ verification failed"; exit 1
fi

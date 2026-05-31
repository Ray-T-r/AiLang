#!/usr/bin/env bash
# bar1.sh — PARITY BAR 1: compile every examples/*.ail with the self-hosted
# compiler (selfhost/main.ail, built by ailangc) and diff its program output
# against the reference `ailangc run`.
#
# This is the headline parity metric for "the self-hosted compiler matches the
# Rust compiler on real programs". Many examples fail until later phases land
# (sockets/TLS/Postgres → Phase 2/3, {i64:i64} maps → Phase 4, match patterns /
# pipe / ternary → Phase 5); the count is expected to climb phase by phase.
#
# Buckets each example into: ok / compile-fail (self-hosted clang failed, almost
# always an unsupported builtin or feature) / output-diff (compiled but printed
# something different). Run from the repo root:  bash selfhost/bar1.sh
set -uo pipefail
cd "$(dirname "$0")/.."
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
guard() { perl -e 'alarm shift; exec @ARGV' "$@"; }

echo "==> building bootstrap (ailangc compile selfhost/main.ail)"
ailangc compile selfhost/main.ail -o "$TMP/ailc0" >/dev/null 2>&1 || { echo "bootstrap build FAILED"; exit 1; }

pass=0; cfail=0; dfail=0; total=0; passing=""; cfails=""; dfails=""
for src in examples/*.ail; do
  name="$(basename "$src" .ail)"
  total=$((total + 1))
  if ! guard 20 "$TMP/ailc0" "$src" "$TMP/$name" >/dev/null 2>&1; then
    cfail=$((cfail + 1)); cfails="$cfails $name"; continue
  fi
  guard 6 "$TMP/$name" > "$TMP/$name.self" 2>&1 || true
  guard 6 ailangc run "$src" > "$TMP/$name.ref" 2>&1 || true
  if diff -q "$TMP/$name.self" "$TMP/$name.ref" >/dev/null 2>&1; then
    pass=$((pass + 1)); passing="$passing $name"
  else
    dfail=$((dfail + 1)); dfails="$dfails $name"
  fi
done

echo ""
echo "Bar 1: $pass/$total examples match ailangc"
echo "  compile-fail ($cfail):$cfails"
echo "  output-diff  ($dfail):$dfails"
echo "  ok           ($pass):$passing"

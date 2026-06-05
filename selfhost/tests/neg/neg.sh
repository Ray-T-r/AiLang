#!/usr/bin/env bash
# selfhost/tests/neg/neg.sh — prove the conservative type checker CATCHES real
# mistakes. Each *.ail here plants exactly one confident type error and carries an
# `// EXPECT: <substring>` line; the compiler must exit non-zero AND print that
# substring. This dir is NOT scanned by verify.sh (those programs must compile),
# so failing programs live here instead.
#
# Usage:  AILC=/path/to/ailc bash selfhost/tests/neg/neg.sh
#         (defaults to ./selfhost/ailc)
set -u
cd "$(dirname "$0")/../../.."          # repo root
AILC="${AILC:-./selfhost/ailc}"
[ -x "$AILC" ] || { echo "no ailc at $AILC (set AILC=...)"; exit 2; }

fail=0; n=0
for src in selfhost/tests/neg/*.ail; do
  name="$(basename "$src" .ail)"
  want="$(sed -n 's;^// EXPECT: ;;p' "$src" | head -1)"
  out="$("$AILC" "$src" "/tmp/neg_$name" 2>&1)"; rc=$?
  rm -f "/tmp/neg_$name"
  if [ "$rc" -eq 0 ]; then
    echo "  FAIL $name — compiled OK, expected a type error"; fail=1
  elif printf '%s' "$out" | grep -qF "$want"; then
    echo "  ok   $name — caught: $want"; n=$((n+1))
  else
    echo "  FAIL $name — wrong/no message; wanted: $want"; echo "$out" | grep -iE "error" | head -2 | sed 's/^/        /'; fail=1
  fi
done
echo ""
if [ "$fail" -eq 0 ]; then echo "✅ negative tests: $n/$n caught"; else echo "❌ negative tests failed"; exit 1; fi

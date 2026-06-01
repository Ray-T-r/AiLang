#!/usr/bin/env bash
# selfhost/bootstrap.sh — rebuild the AiLang compiler from the checked-in C
# seed, with NO dependency on the Rust `ailangc` (or any prior AiLang binary).
#
#   seed/ailc.c                 frozen: main.ail self-compiled to C
#     │  clang
#     ▼
#   ailc                        a working, native AiLang compiler
#     │  ailc main.ail
#     ▼
#   ailc2                       the compiler, rebuilt from its own source
#     │  fixpoint check
#     ▼
#   ailc(main.ail).c == seed    proof the seed is main.ail's fixpoint
#
# Requirements: clang (or $CC) + Boehm GC (bdw-gc). No Rust toolchain needed.
# Usage:  bash selfhost/bootstrap.sh
set -euo pipefail
cd "$(dirname "$0")"

CC="${CC:-clang}"
SEED="seed/ailc.c"

# Link flags — mirror what main.ail's own clang driver assembles. Boehm GC is
# always needed (the runtime allocates via GC_MALLOC); OpenSSL and libpq are
# added when the seed references their symbols (the self-host prelude currently
# pulls both in). Each probe: pkg-config first, then Homebrew, then bare -l.
GCF="$(pkg-config --cflags --libs bdw-gc 2>/dev/null || true)"
if [ -z "$GCF" ]; then
  PREFIX="${BDW_GC_PREFIX:-$(brew --prefix bdw-gc 2>/dev/null || true)}"
  if [ -n "$PREFIX" ]; then GCF="-I$PREFIX/include -L$PREFIX/lib -lgc"; else GCF="-lgc"; fi
fi
LIBS="$GCF -lm"
if grep -q -e 'SSL_' -e 'SHA1(' "$SEED"; then
  LIBS="$LIBS $(pkg-config --cflags --libs openssl 2>/dev/null \
        || echo "-I$(brew --prefix openssl@3 2>/dev/null)/include -L$(brew --prefix openssl@3 2>/dev/null)/lib -lssl -lcrypto")"
fi
if grep -q -e 'PQconnectdb' -e 'PQexec' "$SEED"; then
  LIBS="$LIBS $(pkg-config --cflags --libs libpq 2>/dev/null \
        || echo "-I$(brew --prefix libpq 2>/dev/null)/include -L$(brew --prefix libpq 2>/dev/null)/lib -lpq")"
fi

echo "==> [1/3] compiling the C seed → ./ailc  (no Rust)"
# shellcheck disable=SC2086
"$CC" -O2 -w "$SEED" -o ailc $LIBS
echo "    ok — ./ailc is a native AiLang compiler"

echo "==> [2/3] recompiling main.ail with the seeded compiler"
# stderr is silenced: the self-host's own clang pass emits cosmetic
# -Wparentheses-equality noise on the generated C, not real errors.
./ailc main.ail ailc2 >/dev/null 2>&1
echo "    ok — ./ailc2 built from source by ./ailc"

echo "==> [3/3] fixpoint: does ailc(main.ail) reproduce the seed?"
./ailc main.ail _bootcheck >/dev/null 2>&1
if diff -q "$SEED" _bootcheck.c >/dev/null; then
  echo "    ✅ byte-identical — the seed IS the fixpoint of the current main.ail"
  rc=0
else
  echo "    ⚠️  differs — main.ail changed since the seed was frozen."
  echo "        refresh it with:  ./ailc main.ail /tmp/s && cp /tmp/s.c $SEED"
  rc=1
fi

rm -f _bootcheck _bootcheck.c ailc2 ailc2.c
echo "==> done. ./ailc is a Rust-free AiLang compiler (usage: ./ailc <in.ail> <out>)."
exit "$rc"

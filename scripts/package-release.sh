#!/usr/bin/env bash
# Build this host's self-hosted `ailc` from the C seed and stage the files to
# attach to a GitHub Release.
#
# Output: ./dist/
#   ailc-<os>-<arch>.tar.gz    this host's binary (single top-level `ailc`)
#   SKILL.md                   the Claude Code skill
#   install.sh                 macOS / Linux installer
#   install.ps1                Windows installer
#
# Run on each platform you want to release for, collect every host's
# dist/ail c-*.tar.gz, and upload them all to one GitHub Release together with
# SKILL.md + install.sh + install.ps1 (those are platform-independent — upload once).
#
# Requirements: clang (or $CC) + Boehm GC. OpenSSL + libpq are pulled in by the
# seed's prelude (bootstrap.sh handles the link flags).

set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

OS_NAME="$(uname -s)"; ARCH_NAME="$(uname -m)"
case "${OS_NAME}-${ARCH_NAME}" in
    Darwin-arm64)  STEM="ailc-macos-aarch64" ;;
    Darwin-x86_64) STEM="ailc-macos-x86_64" ;;
    Linux-x86_64)  STEM="ailc-linux-x86_64" ;;
    Linux-aarch64) STEM="ailc-linux-aarch64" ;;
    *) echo "unsupported host: ${OS_NAME} ${ARCH_NAME}" >&2; exit 1 ;;
esac

echo "==> Building ailc from the C seed (no Rust)"
bash selfhost/bootstrap.sh

echo "==> Staging dist/${STEM}.tar.gz"
mkdir -p dist
STAGE="$(mktemp -d)"
cp selfhost/ailc "${STAGE}/ailc"
chmod +x "${STAGE}/ailc"
tar -czf "dist/${STEM}.tar.gz" -C "${STAGE}" ailc
rm -rf "${STAGE}"

cp skill/SKILL.md dist/SKILL.md
cp install.sh     dist/install.sh
cp install.ps1    dist/install.ps1

echo "==> dist/ ready:"
ls -la dist/
echo
echo "Upload every dist/ailc-*.tar.gz from all hosts, plus SKILL.md +"
echo "install.sh + install.ps1 (once), to:"
echo "    https://github.com/Ray-T-r/AiLang/releases"

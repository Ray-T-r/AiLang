#!/usr/bin/env bash
# Build the current host's ailangc binary and stage the files you need
# to attach to a GitHub Release.
#
# Output: ./dist/
#   ailangc-<os>-<arch>       (this host's binary, renamed for the release)
#   SKILL.md                  (copied from skill/)
#   install.sh                (copied from repo root)
#   install.ps1               (copied from repo root)
#
# Run on each platform you want to release for (macOS arm64, macOS x86_64,
# Linux x86_64, Windows x86_64), then collect all the produced binaries
# into a single ./dist/ and upload everything to one GitHub Release.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# Detect platform → asset name (release format is a tar.gz containing
# a single `ailangc` binary at the top level — Finder won't double-click
# it open as TextEdit, and `install.sh` knows to extract it).
OS_NAME="$(uname -s)"
ARCH_NAME="$(uname -m)"
case "${OS_NAME}-${ARCH_NAME}" in
    Darwin-arm64)  STEM="ailangc-macos-aarch64" ;;
    Darwin-x86_64) STEM="ailangc-macos-x86_64" ;;
    Linux-x86_64)  STEM="ailangc-linux-x86_64" ;;
    Linux-aarch64) STEM="ailangc-linux-aarch64" ;;
    *) echo "unsupported host: ${OS_NAME} ${ARCH_NAME}" >&2; exit 1 ;;
esac
ASSET="${STEM}.tar.gz"

echo "==> Building release binary for ${STEM}"
cargo build --release -p ailangc

mkdir -p dist
# Stage the binary under its canonical name so the archive contains
# `ailangc` (not `ailangc-macos-aarch64`).
STAGE="$(mktemp -d)"
cp "target/release/ailangc" "${STAGE}/ailangc"
chmod +x "${STAGE}/ailangc"
tar -czf "dist/${ASSET}" -C "${STAGE}" ailangc
rm -rf "${STAGE}"

cp "skill/SKILL.md"  "dist/SKILL.md"
cp "install.sh"      "dist/install.sh"
cp "install.ps1"     "dist/install.ps1"

echo
echo "==> Staged for upload (./dist/):"
ls -la dist/
echo
echo "Next: create a GitHub Release at"
echo "    https://github.com/Ray-T-r/AiLang/releases/new"
echo "and attach every file in ./dist/ as a release asset."

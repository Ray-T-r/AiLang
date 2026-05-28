#!/usr/bin/env bash
# AiLang installer (macOS / Linux).
#
# Downloads the latest pre-built `ailangc` binary and the AiLang skill from
# GitHub Releases, installs them to:
#   - binary: ~/.local/bin/ailangc
#   - skill:  ~/.claude/skills/ailang/SKILL.md
# Adds ~/.local/bin to PATH (zshrc / bashrc) if missing.
#
# One-liner:
#   curl -fsSL https://github.com/Ray-T-r/AiLang/releases/latest/download/install.sh | bash
#
# Re-run any time — every step is idempotent.

set -euo pipefail

REPO="Ray-T-r/AiLang"
BASE_URL="https://github.com/${REPO}/releases/latest/download"
BIN_DIR="${HOME}/.local/bin"
BIN_PATH="${BIN_DIR}/ailangc"
SKILL_DIR="${HOME}/.claude/skills/ailang"
SKILL_PATH="${SKILL_DIR}/SKILL.md"

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m   ok\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m   !!\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mERROR\033[0m %s\n' "$*" >&2; exit 1; }

# -------- 1. detect platform --------
log "Detecting platform"
OS_NAME="$(uname -s)"
ARCH_NAME="$(uname -m)"
case "${OS_NAME}-${ARCH_NAME}" in
    Darwin-arm64)      ASSET="ailangc-macos-aarch64"  ;;
    Darwin-x86_64)     ASSET="ailangc-macos-x86_64"   ;;
    Linux-x86_64)      ASSET="ailangc-linux-x86_64"   ;;
    Linux-aarch64)     ASSET="ailangc-linux-aarch64"  ;;
    *) die "unsupported platform: ${OS_NAME} ${ARCH_NAME}. For Windows, use install.ps1." ;;
esac
ok "${OS_NAME} ${ARCH_NAME} → ${ASSET}"

# -------- 2. check runtime prerequisites --------
log "Checking runtime prerequisites"
command -v clang >/dev/null 2>&1 || command -v cc >/dev/null 2>&1 \
    || warn "no C compiler found. ailangc invokes clang at compile time — install Xcode CLT (macOS) or build-essential (Linux)."

if [[ "$OS_NAME" == "Darwin" ]]; then
    if ! pkg-config --exists bdw-gc 2>/dev/null \
            && [[ -z "${BDW_GC_PREFIX:-}" ]] \
            && ! brew --prefix bdw-gc >/dev/null 2>&1; then
        warn "Boehm GC not installed. Run: brew install bdw-gc pkg-config"
        warn "Without it, programs that use strings / arrays / maps will fail to link."
    fi
elif [[ "$OS_NAME" == "Linux" ]]; then
    if ! pkg-config --exists bdw-gc 2>/dev/null && [[ -z "${BDW_GC_PREFIX:-}" ]]; then
        warn "Boehm GC not installed. Debian/Ubuntu: sudo apt install libgc-dev pkg-config"
    fi
fi
ok "checked"

# -------- 3. download binary --------
log "Downloading ${ASSET}"
mkdir -p "$BIN_DIR"
TMP_BIN="$(mktemp -t ailangc.XXXXXX)"
trap 'rm -f "$TMP_BIN"' EXIT
if ! curl -fL --progress-bar -o "$TMP_BIN" "${BASE_URL}/${ASSET}"; then
    die "could not download ${BASE_URL}/${ASSET}. Has the release been published yet?"
fi
[[ -s "$TMP_BIN" ]] || die "downloaded file is empty"
install -m 755 "$TMP_BIN" "$BIN_PATH"
# Defensive: strip macOS Gatekeeper quarantine xattr if present. curl
# downloads normally don't get it, but if the user previously dragged a
# browser-downloaded copy onto themselves, this clears any leftover.
if [[ "$OS_NAME" == "Darwin" ]]; then
    xattr -d com.apple.quarantine "$BIN_PATH" 2>/dev/null || true
fi
ok "installed to $BIN_PATH"

# -------- 4. download skill --------
log "Downloading SKILL.md"
mkdir -p "$SKILL_DIR"
TMP_SKILL="$(mktemp -t ailang-skill.XXXXXX)"
trap 'rm -f "$TMP_BIN" "$TMP_SKILL"' EXIT
if curl -fL --progress-bar -o "$TMP_SKILL" "${BASE_URL}/SKILL.md"; then
    install -m 644 "$TMP_SKILL" "$SKILL_PATH"
    ok "skill installed to $SKILL_PATH"
else
    warn "could not download SKILL.md (binary is still installed)"
fi

# -------- 5. ensure ~/.local/bin on PATH --------
PATH_LINE='export PATH="$HOME/.local/bin:$PATH"'
case ":${PATH}:" in
    *":${BIN_DIR}:"*)
        ok "${BIN_DIR} already on PATH"
        ;;
    *)
        SHELL_NAME="$(basename "${SHELL:-bash}")"
        case "$SHELL_NAME" in
            zsh)  RC="${HOME}/.zshrc"  ;;
            bash) RC="${HOME}/.bashrc" ;;
            *)    RC="" ;;
        esac
        if [[ -n "$RC" ]]; then
            if [[ -f "$RC" ]] && grep -Fq "$PATH_LINE" "$RC"; then
                ok "PATH line already present in ${RC}"
            else
                printf '\n# Added by AiLang installer\n%s\n' "$PATH_LINE" >> "$RC"
                ok "appended PATH line to ${RC}"
            fi
            warn "restart your shell or run:  source ${RC}"
        else
            warn "unknown shell (${SHELL_NAME}). Add this to your shell rc:"
            warn "    ${PATH_LINE}"
        fi
        ;;
esac

cat <<EOF

Done. Try it:

    ailangc --version
    echo 'println("hello from ailang")' > /tmp/hi.ail && ailangc run /tmp/hi.ail

If \`ailangc\` is not found, restart your terminal first.
EOF

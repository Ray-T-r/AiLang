#!/usr/bin/env bash
# AiLang installer (macOS / Linux).
#
# Downloads the latest pre-built self-hosted `ailc` binary from GitHub
# Releases and installs it to ~/.local/bin/ailc. Adds that dir to $PATH
# (zshrc / bashrc) if missing.
#
# One-liner:
#   curl -fsSL https://github.com/Ray-T-r/AiLang/releases/latest/download/install.sh | bash
#
# Re-run any time — every step is idempotent.

set -euo pipefail

REPO="Ray-T-r/AiLang"
BASE_URL="https://github.com/${REPO}/releases/latest/download"
BIN_DIR="${HOME}/.local/bin"
BIN_PATH="${BIN_DIR}/ailc"

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m   ok\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m   !!\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mERROR\033[0m %s\n' "$*" >&2; exit 1; }

# -------- 1. detect platform --------
log "Detecting platform"
OS_NAME="$(uname -s)"
ARCH_NAME="$(uname -m)"
case "${OS_NAME}-${ARCH_NAME}" in
    Darwin-arm64)      ASSET="ailc-macos-aarch64.tar.gz"  ;;
    Darwin-x86_64)     ASSET="ailc-macos-x86_64.tar.gz"   ;;
    Linux-x86_64)      ASSET="ailc-linux-x86_64.tar.gz"   ;;
    Linux-aarch64)     ASSET="ailc-linux-aarch64.tar.gz"  ;;
    *) die "unsupported platform: ${OS_NAME} ${ARCH_NAME}." ;;
esac
ok "${OS_NAME} ${ARCH_NAME} → ${ASSET}"

# -------- 2. runtime prerequisites (all MANDATORY for the current seed) --------
# The self-hosted ailc invokes clang at compile time, and its runtime prelude
# unconditionally links Boehm GC + OpenSSL + libpq. Without those, both ailc
# itself and the programs it produces will fail to link.
log "Checking runtime prerequisites (clang + bdw-gc + OpenSSL + libpq)"
command -v clang >/dev/null 2>&1 || command -v cc >/dev/null 2>&1 \
    || warn "no C compiler found. ailc shells out to clang at compile time. Install Xcode CLT (macOS) or build-essential (Linux)."

missing=()
if [[ "$OS_NAME" == "Darwin" ]]; then
    pkg-config --exists bdw-gc 2>/dev/null \
        || [[ -n "${BDW_GC_PREFIX:-}" ]] \
        || brew --prefix bdw-gc >/dev/null 2>&1 \
        || missing+=("bdw-gc")
    pkg-config --exists openssl 2>/dev/null \
        || brew --prefix openssl@3 >/dev/null 2>&1 \
        || brew --prefix openssl >/dev/null 2>&1 \
        || missing+=("openssl@3")
    pkg-config --exists libpq 2>/dev/null \
        || brew --prefix libpq >/dev/null 2>&1 \
        || missing+=("libpq")
    if (( ${#missing[@]} )); then
        warn "missing libraries: ${missing[*]}"
        warn "install with:  brew install ${missing[*]} pkg-config"
    fi
elif [[ "$OS_NAME" == "Linux" ]]; then
    pkg-config --exists bdw-gc 2>/dev/null || [[ -n "${BDW_GC_PREFIX:-}" ]] || missing+=("libgc-dev")
    pkg-config --exists openssl 2>/dev/null || missing+=("libssl-dev")
    pkg-config --exists libpq 2>/dev/null || missing+=("libpq-dev")
    if (( ${#missing[@]} )); then
        warn "missing libraries: ${missing[*]}"
        warn "install with:  sudo apt-get install -y clang ${missing[*]} pkg-config"
    fi
fi
ok "checked"

# -------- 3. download + extract --------
log "Downloading ${ASSET}"
mkdir -p "$BIN_DIR"
TMP_DIR="$(mktemp -d -t ailc.XXXXXX)"
trap 'rm -rf "$TMP_DIR"' EXIT
TMP_ARCHIVE="${TMP_DIR}/${ASSET}"
if ! curl -fL --progress-bar -o "$TMP_ARCHIVE" "${BASE_URL}/${ASSET}"; then
    die "could not download ${BASE_URL}/${ASSET}. Has the release been published yet?"
fi
[[ -s "$TMP_ARCHIVE" ]] || die "downloaded file is empty"

log "Extracting"
tar -xzf "$TMP_ARCHIVE" -C "$TMP_DIR" \
    || die "could not extract ${TMP_ARCHIVE} (corrupt download?)"
[[ -f "${TMP_DIR}/ailc" ]] \
    || die "archive did not contain an 'ailc' binary at the top level"

install -m 755 "${TMP_DIR}/ailc" "$BIN_PATH"
# Strip the macOS Gatekeeper quarantine xattr if it's there. curl-installed
# binaries normally don't carry it, but clear defensively.
if [[ "$OS_NAME" == "Darwin" ]]; then
    xattr -d com.apple.quarantine "$BIN_PATH" 2>/dev/null || true
fi
ok "installed to $BIN_PATH"

# -------- 4. ensure ~/.local/bin on PATH --------
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

    echo 'println("hello from ailang")' > /tmp/hi.ail && ailc /tmp/hi.ail /tmp/hi && /tmp/hi

If \`ailc\` is not found, restart your terminal first.
EOF

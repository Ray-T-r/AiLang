#!/usr/bin/env bash
# AiLang installer (macOS / Linux).
#
# Downloads the latest pre-built self-hosted `ailc` from GitHub Releases,
# installs it to ~/.local/bin/ailc, adds that dir to $PATH, installs the
# AiLang skill for Claude Code, and — by default — installs the native
# libraries `ailc` needs (clang, Boehm GC, OpenSSL, libpq).
#
# One-liner:
#   curl -fsSL https://github.com/Ray-T-r/AiLang/releases/latest/download/install.sh | bash
#
# Flags / env:
#   --no-deps         skip auto-installing native libraries (just warn)
#   --no-skill        skip installing the Claude Code skill
#   AILANG_NO_DEPS=1  same as --no-deps
#
# Re-run any time — every step is idempotent.

set -euo pipefail

REPO="Ray-T-r/AiLang"
BASE_URL="https://github.com/${REPO}/releases/latest/download"
BIN_DIR="${HOME}/.local/bin"
BIN_PATH="${BIN_DIR}/ailc"
SKILL_DIR="${HOME}/.claude/skills/ailang"
SKILL_PATH="${SKILL_DIR}/SKILL.md"
STD_ROOT="${HOME}/.local/share/ailang"     # holds std/ ; AILANG_STD points here
STD_ASSET="ailc-std.tar.gz"

INSTALL_DEPS=1
INSTALL_SKILL=1
[[ "${AILANG_NO_DEPS:-}" == "1" ]] && INSTALL_DEPS=0
for arg in "$@"; do
    case "$arg" in
        --no-deps)  INSTALL_DEPS=0 ;;
        --no-skill) INSTALL_SKILL=0 ;;
    esac
done

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m   ok\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m   !!\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mERROR\033[0m %s\n' "$*" >&2; exit 1; }

# `curl | bash` feeds the script on stdin, so an interactive sudo password
# prompt can't be answered. Use sudo only when it won't need to ask.
SUDO=""
if [[ "$(id -u)" -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
        SUDO="sudo"
    fi
fi

# -------- 1. detect platform --------
log "Detecting platform"
OS_NAME="$(uname -s)"
ARCH_NAME="$(uname -m)"
case "${OS_NAME}-${ARCH_NAME}" in
    Darwin-arm64)      ASSET="ailc-macos-aarch64.tar.gz"  ;;
    Darwin-x86_64)     ASSET="ailc-macos-x86_64.tar.gz"   ;;
    Linux-x86_64)      ASSET="ailc-linux-x86_64.tar.gz"   ;;
    Linux-aarch64)     ASSET="ailc-linux-aarch64.tar.gz"  ;;
    *) die "unsupported platform: ${OS_NAME} ${ARCH_NAME}. For Windows, use install.ps1." ;;
esac
ok "${OS_NAME} ${ARCH_NAME} → ${ASSET}"

# -------- 2. native dependencies --------
# `ailc` shells out to clang at compile time, and the runtime prelude links
# Boehm GC + OpenSSL + libpq unconditionally. All four are required.
need_cc() { command -v clang >/dev/null 2>&1 || command -v cc >/dev/null 2>&1; }
have_gc()  { pkg-config --exists bdw-gc 2>/dev/null || [[ -n "${BDW_GC_PREFIX:-}" ]]; }
have_ssl() { pkg-config --exists openssl 2>/dev/null; }
have_pq()  { pkg-config --exists libpq 2>/dev/null; }

install_deps_macos() {
    command -v brew >/dev/null 2>&1 || {
        warn "Homebrew not found — can't auto-install. Install from https://brew.sh then re-run,"
        warn "or: install bdw-gc, openssl, libpq, pkg-config manually."
        return
    }
    local pkgs=()
    have_gc  || pkgs+=("bdw-gc")
    have_ssl || brew --prefix openssl@3 >/dev/null 2>&1 || pkgs+=("openssl@3")
    have_pq  || brew --prefix libpq >/dev/null 2>&1 || pkgs+=("libpq")
    pkg-config --version >/dev/null 2>&1 || pkgs+=("pkg-config")
    need_cc || warn "no clang/cc — install the Xcode Command Line Tools: xcode-select --install"
    if (( ${#pkgs[@]} )); then
        log "Installing native deps via Homebrew: ${pkgs[*]}"
        brew install "${pkgs[@]}"
        ok "brew deps installed"
    else
        ok "native deps already present"
    fi
}

install_deps_linux() {
    # Map the four deps onto whichever package manager is present.
    local mgr="" inst=() pkgs=()
    if command -v apt-get >/dev/null 2>&1; then
        mgr="apt-get"; inst=($SUDO apt-get install -y)
        pkgs=(clang libgc-dev libssl-dev libpq-dev pkg-config)
    elif command -v dnf >/dev/null 2>&1; then
        mgr="dnf"; inst=($SUDO dnf install -y)
        pkgs=(clang gc-devel openssl-devel libpq-devel pkgconf-pkg-config)
    elif command -v pacman >/dev/null 2>&1; then
        mgr="pacman"; inst=($SUDO pacman -S --noconfirm)
        pkgs=(clang gc openssl postgresql-libs pkgconf)
    else
        warn "no apt-get / dnf / pacman found — install clang + bdw-gc + OpenSSL + libpq manually."
        return
    fi
    # Skip if everything's already there.
    if need_cc && have_gc && have_ssl && have_pq; then
        ok "native deps already present"
        return
    fi
    if [[ "$(id -u)" -ne 0 && -z "$SUDO" ]]; then
        warn "native deps missing and passwordless sudo unavailable. Run this yourself:"
        warn "    sudo ${inst[*]:1} ${pkgs[*]}"
        return
    fi
    log "Installing native deps via ${mgr}: ${pkgs[*]}"
    if [[ "$mgr" == "apt-get" ]]; then $SUDO apt-get update -qq || true; fi
    "${inst[@]}" "${pkgs[@]}" && ok "${mgr} deps installed" \
        || warn "dep install failed — install manually: ${inst[*]} ${pkgs[*]}"
}

if (( INSTALL_DEPS )); then
    log "Checking / installing native dependencies (clang + bdw-gc + OpenSSL + libpq)"
    case "$OS_NAME" in
        Darwin) install_deps_macos ;;
        Linux)  install_deps_linux ;;
    esac
else
    log "Skipping dependency install (--no-deps). You need clang + bdw-gc + OpenSSL + libpq."
    need_cc  || warn "missing: a C compiler (clang/cc)"
    have_gc  || warn "missing: Boehm GC (bdw-gc)"
    have_ssl || warn "missing: OpenSSL"
    have_pq  || warn "missing: libpq"
fi

# -------- 3. download + extract ailc --------
log "Downloading ${ASSET}"
mkdir -p "$BIN_DIR"
TMP_DIR="$(mktemp -d -t ailc.XXXXXX)"
trap 'rm -rf "$TMP_DIR"' EXIT
TMP_ARCHIVE="${TMP_DIR}/${ASSET}"
if ! curl -fL --progress-bar -o "$TMP_ARCHIVE" "${BASE_URL}/${ASSET}"; then
    die "could not download ${BASE_URL}/${ASSET}. Has the release been published for this platform yet?"
fi
[[ -s "$TMP_ARCHIVE" ]] || die "downloaded file is empty"

log "Extracting"
tar -xzf "$TMP_ARCHIVE" -C "$TMP_DIR" || die "could not extract ${TMP_ARCHIVE} (corrupt download?)"
[[ -f "${TMP_DIR}/ailc" ]] || die "archive did not contain an 'ailc' binary at the top level"

install -m 755 "${TMP_DIR}/ailc" "$BIN_PATH"
if [[ "$OS_NAME" == "Darwin" ]]; then
    xattr -d com.apple.quarantine "$BIN_PATH" 2>/dev/null || true
fi
ok "installed to $BIN_PATH"

# -------- 3b. standard library (so `im "std/..."` works from anywhere) --------
# The compiler falls back to $AILANG_STD when an import isn't found next to the
# source; we drop std/ here and export AILANG_STD below.
log "Installing the standard library"
if curl -fL -s -o "${TMP_DIR}/${STD_ASSET}" "${BASE_URL}/${STD_ASSET}"; then
    rm -rf "${STD_ROOT}/std"; mkdir -p "$STD_ROOT"
    if tar -xzf "${TMP_DIR}/${STD_ASSET}" -C "$STD_ROOT" && [[ -f "${STD_ROOT}/std/time.ail" ]]; then
        ok "std library → ${STD_ROOT}/std"
    else
        warn "could not extract the std library (im \"std/...\" may not work)"
    fi
else
    warn "could not download the std library (im \"std/...\" won't work until installed)"
fi

# -------- 4. install the Claude Code skill --------
if (( INSTALL_SKILL )); then
    log "Installing AiLang skill for Claude Code"
    mkdir -p "$SKILL_DIR"
    if curl -fL -s -o "${TMP_DIR}/SKILL.md" "${BASE_URL}/SKILL.md"; then
        install -m 644 "${TMP_DIR}/SKILL.md" "$SKILL_PATH"
        ok "skill → ${SKILL_PATH}"
    else
        warn "could not download SKILL.md (ailc itself is installed fine)"
    fi
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

# -------- 5b. AILANG_STD (so the compiler finds the std library) --------
export AILANG_STD="${STD_ROOT}"
STD_LINE="export AILANG_STD=\"${STD_ROOT}\""
SHELL_NAME="$(basename "${SHELL:-bash}")"
case "$SHELL_NAME" in zsh) RC2="${HOME}/.zshrc" ;; bash) RC2="${HOME}/.bashrc" ;; *) RC2="" ;; esac
if [[ -n "$RC2" ]]; then
    if [[ -f "$RC2" ]] && grep -Fq 'AILANG_STD=' "$RC2"; then
        ok "AILANG_STD already set in ${RC2}"
    else
        printf '# Added by AiLang installer\n%s\n' "$STD_LINE" >> "$RC2"
        ok "appended AILANG_STD to ${RC2}"
    fi
else
    warn "set this in your shell rc:  ${STD_LINE}"
fi

cat <<EOF

Done. Try it:

    echo 'println("hello from ailang")' > /tmp/hi.ail && ailc /tmp/hi.ail /tmp/hi && /tmp/hi

If \`ailc\` is not found, restart your terminal first.
EOF

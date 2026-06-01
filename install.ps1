# AiLang installer (Windows / PowerShell) — via WSL2, with native-feeling shims.
#
# The AiLang compiler's runtime is POSIX-only (BSD sockets, fork(), POSIX
# regex), so there is no native Windows build. On Windows, AiLang runs inside
# WSL2 — but this installer adds `ailc` / `ailrun` shims to your Windows PATH
# that transparently forward into WSL (with path translation), so you can run
# them from any Windows terminal without thinking about WSL:
#
#     ailrun foo.ail            # compile + run, output in your Windows terminal
#     ailc   foo.ail out        # produce a binary (Linux ELF, runnable via WSL)
#
# What it does:
#   1. ensures WSL2 + a Linux distro is available (installs it if missing),
#   2. runs the Linux installer inside WSL (installs ailc + deps + skill there),
#   3. installs ailc/ailrun shims on the Windows PATH (forward to WSL),
#   4. drops the AiLang skill on the Windows side too, for Windows-native
#      Claude Code.
#
# One-liner:
#   iwr -useb https://github.com/Ray-T-r/AiLang/releases/latest/download/install.ps1 | iex
#
# Flags:
#   -NoSkill   skip installing the Claude Code skill on the Windows side

[CmdletBinding()]
param(
    [switch]$NoSkill
)
$ErrorActionPreference = 'Stop'

$Repo      = 'Ray-T-r/AiLang'
$BaseUrl   = "https://github.com/$Repo/releases/latest/download"
$SkillDir  = Join-Path $env:USERPROFILE '.claude\skills\ailang'
$SkillPath = Join-Path $SkillDir 'SKILL.md'
$ShimDir   = Join-Path $env:LOCALAPPDATA 'Programs\ailc'

function Log  ($m) { Write-Host "==> $m" -ForegroundColor Cyan }
function OK   ($m) { Write-Host "   ok $m" -ForegroundColor Green }
function Warn ($m) { Write-Host "   !! $m" -ForegroundColor Yellow }
function Die  ($m) { Write-Host "ERROR $m" -ForegroundColor Red; exit 1 }

Log 'AiLang on Windows runs inside WSL2, fronted by native ailc/ailrun shims.'

# -------- 1. is WSL usable (installed + a distro with bash)? --------
$wslReady = $false
if (Get-Command wsl -ErrorAction SilentlyContinue) {
    try {
        $probe = (wsl -e bash -c "echo __ailang_wsl_ok__") 2>$null
        if ($probe -match '__ailang_wsl_ok__') { $wslReady = $true }
    } catch { }
}

if (-not $wslReady) {
    Warn 'WSL2 with a Linux distro is not ready yet.'
    Log  'Installing it (needs administrator rights and usually a reboot): wsl --install -d Ubuntu'
    try { wsl --install -d Ubuntu } catch { Warn "wsl --install failed: $_" }
    Write-Host @"

Next steps:
  1. Reboot if Windows asks you to.
  2. Launch "Ubuntu" once from the Start menu and create your Linux username/password.
  3. Re-run this installer:
       iwr -useb $BaseUrl/install.ps1 | iex

"@ -ForegroundColor Yellow
    exit 0
}
OK 'WSL is ready'

# -------- 2. run the Linux installer inside WSL --------
# `sudo -v` warms the sudo credential cache (prompts once on the console if a
# password is needed) so the Linux install.sh can auto-install deps.
Log 'Installing ailc inside WSL (native deps + the skill, in WSL)'
wsl -e bash -lc "sudo -v && curl -fsSL $BaseUrl/install.sh | bash"
if ($LASTEXITCODE -ne 0) {
    Warn "the in-WSL install returned exit code $LASTEXITCODE — check the output above."
} else {
    OK 'ailc installed inside WSL'
}

# -------- 3. native-feeling shims on the Windows PATH --------
Log 'Installing ailc / ailrun shims on the Windows PATH'
$wslHome = (wsl -e bash -lc 'echo $HOME').Trim()
if (-not $wslHome) { $wslHome = '/root' }

# 3a. forwarder scripts inside WSL (do cwd + path translation, then run ailc).
$winrun = @"
#!/usr/bin/env bash
export PATH="`$HOME/.local/bin:`$PATH"
cwd="`$1"; shift
cd "`$(wslpath -u "`$cwd" 2>/dev/null)" 2>/dev/null || true
args=()
for a in "`$@"; do case "`$a" in [A-Za-z]:\\*|*\\*) args+=("`$(wslpath -u "`$a")");; *) args+=("`$a");; esac; done
exec ailc "`${args[@]}"
"@
$ailrun = @"
#!/usr/bin/env bash
export PATH="`$HOME/.local/bin:`$PATH"
cwd="`$1"; shift
cd "`$(wslpath -u "`$cwd" 2>/dev/null)" 2>/dev/null || true
[ `$# -ge 1 ] || { echo "usage: ailrun <file.ail> [args...]" >&2; exit 2; }
src="`$1"; shift
case "`$src" in [A-Za-z]:\\*|*\\*) src="`$(wslpath -u "`$src")";; esac
out="`$(mktemp)"
if ! ailc "`$src" "`$out" >/tmp/ailrun.err 2>&1; then cat /tmp/ailrun.err >&2; exit 1; fi
"`$out" "`$@"
"@
function Push-ToWsl([string]$content, [string]$dest) {
    $b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($content -replace "`r`n","`n")))
    wsl -e bash -lc "echo $b64 | base64 -d > '$dest' && chmod +x '$dest'"
}
Push-ToWsl $winrun "$wslHome/.local/bin/ailc-winrun"
Push-ToWsl $ailrun "$wslHome/.local/bin/ailrun-win"

# 3b. Windows .cmd shims that call the WSL forwarders (no nested-shell quoting).
New-Item -ItemType Directory -Force -Path $ShimDir | Out-Null
[IO.File]::WriteAllText((Join-Path $ShimDir 'ailc.cmd'),   "@echo off`r`nwsl -e $wslHome/.local/bin/ailc-winrun `"%CD%`" %*`r`n")
[IO.File]::WriteAllText((Join-Path $ShimDir 'ailrun.cmd'), "@echo off`r`nwsl -e $wslHome/.local/bin/ailrun-win `"%CD%`" %*`r`n")
OK "shims -> $ShimDir (ailc, ailrun)"

# 3c. ensure the shim dir is on the user PATH.
$up = [Environment]::GetEnvironmentVariable('Path','User'); if (-not $up) { $up = '' }
if (($up -split ';') -notcontains $ShimDir) {
    [Environment]::SetEnvironmentVariable('Path', ($up.TrimEnd(';') + ';' + $ShimDir), 'User')
    OK "added $ShimDir to user PATH"
    Warn 'open a NEW terminal for PATH changes to take effect.'
} else {
    OK 'shim dir already on PATH'
}

# -------- 4. Windows-side skill (for Windows-native Claude Code) --------
if (-not $NoSkill) {
    Log 'Installing the AiLang skill on the Windows side (for Windows-native Claude Code)'
    New-Item -ItemType Directory -Force -Path $SkillDir | Out-Null
    try {
        Invoke-WebRequest -Uri "$BaseUrl/SKILL.md" -OutFile $SkillPath -UseBasicParsing
        OK "skill -> $SkillPath"
    } catch {
        Warn "could not download SKILL.md for the Windows side: $_"
    }
}

Write-Host @"

Done. Open a NEW terminal (so PATH refreshes), then use AiLang like a native tool:

    echo 'println("hello")' > hi.ail
    ailrun hi.ail              # compile + run, output right here
    ailc   hi.ail myprog       # produce a binary (run it with: wsl ./myprog)

Both commands forward into WSL transparently — no need to type `wsl` yourself.
"@ -ForegroundColor Green

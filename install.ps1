# AiLang installer (Windows / PowerShell) — via WSL2.
#
# The AiLang compiler's runtime is POSIX-only (BSD sockets, fork(), POSIX
# regex), so there is no native Windows build. On Windows, AiLang runs inside
# WSL2 (Windows Subsystem for Linux). This script:
#   1. ensures WSL2 + a Linux distro is available (installs it if missing),
#   2. runs the Linux installer inside WSL (installs ailc + deps + skill there),
#   3. drops the AiLang skill on the Windows side too, for Windows-native
#      Claude Code.
#
# One-liner:
#   iwr -useb https://github.com/Ray-T-r/AiLang/releases/latest/download/install.ps1 | iex
#
# Flags:
#   -NoSkill   skip installing the Claude Code skill on the Windows side
#
# After install, use ailc from WSL:  wsl ailc foo.ail foo ; wsl ./foo
# (or open a WSL shell and run ailc directly).

[CmdletBinding()]
param(
    [switch]$NoSkill
)
$ErrorActionPreference = 'Stop'

$Repo      = 'Ray-T-r/AiLang'
$BaseUrl   = "https://github.com/$Repo/releases/latest/download"
$SkillDir  = Join-Path $env:USERPROFILE '.claude\skills\ailang'
$SkillPath = Join-Path $SkillDir 'SKILL.md'

function Log  ($m) { Write-Host "==> $m" -ForegroundColor Cyan }
function OK   ($m) { Write-Host "   ok $m" -ForegroundColor Green }
function Warn ($m) { Write-Host "   !! $m" -ForegroundColor Yellow }
function Die  ($m) { Write-Host "ERROR $m" -ForegroundColor Red; exit 1 }

Log 'AiLang on Windows runs inside WSL2 (the compiler runtime is POSIX-only).'

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
    if (Get-Command wsl -ErrorAction SilentlyContinue) {
        Log 'Installing the default distro: wsl --install -d Ubuntu'
        Log '(This needs administrator rights and usually a reboot.)'
        try { wsl --install -d Ubuntu } catch { Warn "wsl --install failed: $_" }
    } else {
        Log 'Installing WSL2 + Ubuntu: wsl --install'
        Log '(This needs administrator rights and a reboot.)'
        try { wsl --install } catch { Warn "wsl --install failed: $_" }
    }
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
# password is needed) so the Linux install.sh can auto-install apt/dnf/pacman
# deps non-interactively. Then ailc + the WSL-side skill are installed.
Log 'Installing ailc inside WSL (this also installs native deps + the skill in WSL)'
$inner = "sudo -v && curl -fsSL $BaseUrl/install.sh | bash"
wsl -e bash -lc "$inner"
if ($LASTEXITCODE -ne 0) {
    Warn "the in-WSL install returned exit code $LASTEXITCODE — check the output above."
} else {
    OK 'ailc installed inside WSL'
}

# -------- 3. Windows-side skill (for Windows-native Claude Code) --------
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

Done. AiLang lives inside WSL. Use it from PowerShell:

    wsl bash -lc 'echo ''println("hi from ailang")'' > /tmp/hi.ail && ailc /tmp/hi.ail /tmp/hi && /tmp/hi'

or open a WSL shell (just run `wsl`) and use `ailc` directly:

    ailc prog.ail prog && ./prog

If `ailc` isn't found inside WSL, open a fresh WSL shell so PATH refreshes.
"@ -ForegroundColor Green

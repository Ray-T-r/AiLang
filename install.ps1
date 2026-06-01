# AiLang installer (Windows / PowerShell).
#
# Downloads the latest pre-built self-hosted ailc.exe + the AiLang skill from
# GitHub Releases, installs them, adds ailc to the user PATH, and — by default —
# installs the native toolchain ailc needs via MSYS2 (clang + Boehm GC +
# OpenSSL + libpq, in the mingw64 environment).
#
# One-liner:
#   iwr -useb https://github.com/Ray-T-r/AiLang/releases/latest/download/install.ps1 | iex
#
# Flags:
#   -NoDeps    skip the MSYS2 toolchain install (just check / warn)
#   -NoSkill   skip installing the Claude Code skill
#
# Re-run any time — every step is idempotent.

[CmdletBinding()]
param(
    [switch]$NoDeps,
    [switch]$NoSkill
)
$ErrorActionPreference = 'Stop'

$Repo       = 'Ray-T-r/AiLang'
$BaseUrl    = "https://github.com/$Repo/releases/latest/download"
$InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\ailc'
$BinPath    = Join-Path $InstallDir 'ailc.exe'
$SkillDir   = Join-Path $env:USERPROFILE '.claude\skills\ailang'
$SkillPath  = Join-Path $SkillDir 'SKILL.md'
# MSYS2 mingw64 — where clang + the DLLs live. Override with $env:MSYS2_ROOT.
$Msys2Root  = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { 'C:\msys64' }
$Mingw64Bin = Join-Path $Msys2Root 'mingw64\bin'

function Log  ($m) { Write-Host "==> $m" -ForegroundColor Cyan }
function OK   ($m) { Write-Host "   ok $m" -ForegroundColor Green }
function Warn ($m) { Write-Host "   !! $m" -ForegroundColor Yellow }
function Die  ($m) { Write-Host "ERROR $m" -ForegroundColor Red; exit 1 }

# -------- 1. detect architecture --------
Log 'Detecting platform'
switch ($env:PROCESSOR_ARCHITECTURE) {
    'AMD64' { $asset = 'ailc-windows-x86_64.zip' }
    'ARM64' { $asset = 'ailc-windows-aarch64.zip' }
    default { Die "unsupported architecture: $env:PROCESSOR_ARCHITECTURE" }
}
OK "Windows $env:PROCESSOR_ARCHITECTURE -> $asset"

# -------- 2. native toolchain (MSYS2 mingw64) --------
# ailc shells out to clang at compile time, and every program it builds links
# Boehm GC + OpenSSL + libpq. On Windows these come from MSYS2's mingw64 env.
$pacman = Join-Path $Msys2Root 'usr\bin\pacman.exe'
$pkgs = @(
    'mingw-w64-x86_64-clang',
    'mingw-w64-x86_64-gc',
    'mingw-w64-x86_64-openssl',
    'mingw-w64-x86_64-postgresql',  # provides libpq
    'mingw-w64-x86_64-pkgconf'
)
if (-not $NoDeps) {
    Log 'Checking / installing native toolchain via MSYS2'
    if (Test-Path $pacman) {
        Log "pacman found at $pacman — installing: $($pkgs -join ', ')"
        & $pacman -S --needed --noconfirm @pkgs
        if ($LASTEXITCODE -eq 0) { OK 'MSYS2 toolchain installed' }
        else { Warn "pacman exited $LASTEXITCODE — install manually in the MSYS2 shell:`n      pacman -S --needed $($pkgs -join ' ')" }
    }
    elseif (Get-Command winget -ErrorAction SilentlyContinue) {
        Warn "MSYS2 not found at $Msys2Root. Installing it via winget (then re-run this script):"
        Log  'winget install --id MSYS2.MSYS2 -e'
        winget install --id MSYS2.MSYS2 -e --accept-source-agreements --accept-package-agreements
        Warn "MSYS2 installed. Re-run this installer to pull the mingw64 packages, or run in the MSYS2 shell:"
        Warn "    pacman -S --needed $($pkgs -join ' ')"
    }
    else {
        Warn "MSYS2 not found and winget unavailable. Install MSYS2 from https://www.msys2.org/,"
        Warn "then in the MSYS2 shell run:  pacman -S --needed $($pkgs -join ' ')"
    }
}
else {
    Log 'Skipping toolchain install (-NoDeps). You need MSYS2 mingw64 clang + gc + openssl + libpq.'
}

# -------- 3. download + extract ailc.exe --------
Log "Downloading $asset"
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
$tmpDir = New-Item -ItemType Directory -Path (Join-Path $env:TEMP "ailc-install-$([guid]::NewGuid())")
try {
    $tmpZip = Join-Path $tmpDir $asset
    try {
        Invoke-WebRequest -Uri "$BaseUrl/$asset" -OutFile $tmpZip -UseBasicParsing
    } catch {
        Die "could not download $BaseUrl/$asset. Has the release been published for this platform yet? ($_)"
    }
    if ((Get-Item $tmpZip).Length -eq 0) { Die 'downloaded file is empty' }

    Log 'Extracting'
    Expand-Archive -Path $tmpZip -DestinationPath $tmpDir -Force
    $extracted = Join-Path $tmpDir 'ailc.exe'
    if (-not (Test-Path $extracted)) { Die "archive did not contain an 'ailc.exe' at the top level" }
    Move-Item -Force $extracted $BinPath
    OK "installed to $BinPath"

    # -------- 4. skill --------
    if (-not $NoSkill) {
        Log 'Installing AiLang skill for Claude Code'
        New-Item -ItemType Directory -Force -Path $SkillDir | Out-Null
        try {
            Invoke-WebRequest -Uri "$BaseUrl/SKILL.md" -OutFile $SkillPath -UseBasicParsing
            OK "skill -> $SkillPath"
        } catch {
            Warn "could not download SKILL.md (ailc itself is installed fine): $_"
        }
    }
} finally {
    Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue
}

# -------- 5. PATH (install dir + mingw64 bin so clang & the DLLs are found) --------
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$segments = if ($userPath) { $userPath -split ';' } else { @() }
$toAdd = @()
if ($segments -notcontains $InstallDir) { $toAdd += $InstallDir }
if ((Test-Path $Mingw64Bin) -and ($segments -notcontains $Mingw64Bin)) { $toAdd += $Mingw64Bin }
if ($toAdd.Count) {
    $newPath = (@($userPath) + $toAdd | Where-Object { $_ }) -join ';'
    [Environment]::SetEnvironmentVariable('Path', $newPath, 'User')
    OK "added to user PATH: $($toAdd -join ', ')"
    Warn 'open a NEW terminal for PATH changes to take effect.'
} else {
    OK 'PATH already contains the install dir (and mingw64 bin if present)'
}

Write-Host @"

Done. Try it (in a NEW terminal so PATH refreshes):

    'println("hello from ailang")' | Out-File -Encoding ascii `$env:TEMP\hi.ail
    ailc `$env:TEMP\hi.ail `$env:TEMP\hi.exe ; & `$env:TEMP\hi.exe

ailc needs the MSYS2 mingw64 bin on PATH (clang + the GC/OpenSSL/libpq DLLs).
This script added it if MSYS2 was at $Msys2Root.
"@ -ForegroundColor Green

# AiLang installer (Windows / PowerShell).
#
# Downloads the latest pre-built ailangc.exe and the AiLang skill from
# GitHub Releases, installs them to:
#   - binary: %LOCALAPPDATA%\Programs\ailangc\ailangc.exe
#   - skill:  %USERPROFILE%\.claude\skills\ailang\SKILL.md
# Adds the install dir to the user PATH (via setx) if missing.
#
# One-liner:
#   iwr -useb https://github.com/Ray-T-r/AiLang/releases/latest/download/install.ps1 | iex
#
# Re-run any time — every step is idempotent.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$Repo      = 'Ray-T-r/AiLang'
$BaseUrl   = "https://github.com/$Repo/releases/latest/download"
$InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\ailangc'
$BinPath    = Join-Path $InstallDir 'ailangc.exe'
$SkillDir   = Join-Path $env:USERPROFILE '.claude\skills\ailang'
$SkillPath  = Join-Path $SkillDir 'SKILL.md'

function Log    ($m) { Write-Host "==> $m" -ForegroundColor Cyan }
function OK     ($m) { Write-Host "   ok $m" -ForegroundColor Green }
function Warn   ($m) { Write-Host "   !! $m" -ForegroundColor Yellow }
function Die    ($m) { Write-Host "ERROR $m" -ForegroundColor Red; exit 1 }

# -------- 1. detect architecture --------
Log 'Detecting platform'
$arch = $env:PROCESSOR_ARCHITECTURE
switch ($arch) {
    'AMD64' { $asset = 'ailangc-windows-x86_64.zip' }
    'ARM64' { $asset = 'ailangc-windows-aarch64.zip' }
    default { Die "unsupported architecture: $arch" }
}
OK "Windows $arch → $asset"

# -------- 2. runtime prereq check --------
Log 'Checking runtime prerequisites'
$cc = Get-Command clang -ErrorAction SilentlyContinue
if (-not $cc) {
    $cc = Get-Command cl -ErrorAction SilentlyContinue
}
if (-not $cc) {
    Warn 'no C compiler (clang or MSVC cl) found on PATH.'
    Warn 'ailangc invokes a C compiler at compile time. Install LLVM (https://llvm.org/) or Visual Studio Build Tools first.'
} else {
    OK "found $($cc.Source)"
}
Warn 'Optional native deps (only if you use the matching stdlib):'
Warn '  - std/pg.ail   → libpq      (https://www.postgresql.org/download/windows/)'
Warn '  - std/tls.ail  → OpenSSL    (https://slproweb.com/products/Win32OpenSSL.html)'

# -------- 3. download + extract binary --------
Log "Downloading $asset"
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
$tmpDir = New-Item -ItemType Directory -Path (Join-Path $env:TEMP "ailangc-install-$([guid]::NewGuid())")
try {
    $tmpZip = Join-Path $tmpDir $asset
    try {
        Invoke-WebRequest -Uri "$BaseUrl/$asset" -OutFile $tmpZip -UseBasicParsing
    } catch {
        Die "could not download $BaseUrl/$asset. Has the release been published yet? ($_)"
    }
    if ((Get-Item $tmpZip).Length -eq 0) { Die 'downloaded file is empty' }

    Log "Extracting"
    Expand-Archive -Path $tmpZip -DestinationPath $tmpDir -Force
    $extracted = Join-Path $tmpDir 'ailangc.exe'
    if (-not (Test-Path $extracted)) {
        Die "archive did not contain an 'ailangc.exe' at the top level"
    }
    Move-Item -Force $extracted $BinPath
    OK "installed to $BinPath"
} finally {
    Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue
}

# -------- 4. download skill --------
Log 'Downloading SKILL.md'
New-Item -ItemType Directory -Force -Path $SkillDir | Out-Null
$tmpSkill = New-TemporaryFile
try {
    Invoke-WebRequest -Uri "$BaseUrl/SKILL.md" -OutFile $tmpSkill -UseBasicParsing
    Move-Item -Force $tmpSkill $SkillPath
    OK "skill installed to $SkillPath"
} catch {
    Warn "could not download SKILL.md (binary still installed): $_"
    Remove-Item -Force $tmpSkill -ErrorAction SilentlyContinue
}

# -------- 5. ensure install dir on user PATH --------
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$pathSegments = if ($userPath) { $userPath -split ';' } else { @() }
if ($pathSegments -contains $InstallDir) {
    OK "$InstallDir already on user PATH"
} else {
    $newPath = if ($userPath) { "$userPath;$InstallDir" } else { $InstallDir }
    [Environment]::SetEnvironmentVariable('Path', $newPath, 'User')
    OK "added $InstallDir to user PATH"
    Warn 'open a new terminal for PATH changes to take effect.'
}

Write-Host @"

Done. Try it (in a NEW terminal so PATH refreshes):

    ailangc --version
    'println("hello from ailang")' | Out-File -Encoding utf8 $env:TEMP\hi.ail
    ailangc run $env:TEMP\hi.ail

"@ -ForegroundColor Green

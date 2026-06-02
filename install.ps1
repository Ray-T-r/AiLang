# AiLang installer (Windows / PowerShell) — NATIVE, no WSL.
#
# AiLang now has a native Windows compiler: `ailc.exe` runs directly on Windows
# and produces self-contained native `.exe` files. No WSL, no reboot, no Ubuntu
# user. (`ailc` emits C and builds it with clang, so it needs the small MSYS2
# mingw64 toolchain — clang + Boehm GC — which this installer sets up for you.)
#
# What it does:
#   1. ensures the mingw64 C toolchain (clang + Boehm GC) `ailc` compiles with,
#      installing MSYS2 via winget if it's missing,
#   2. downloads the native ailc.exe to %LOCALAPPDATA%\Programs\ailc,
#   3. adds that + mingw64\bin to your PATH,
#   4. installs the AiLang skill for Claude Code,
#   5. compiles + runs a hello program to prove it works.
#
# One-liner:
#   iwr -useb https://github.com/Ray-T-r/AiLang/releases/latest/download/install.ps1 | iex
#
# Flags / params:
#   -NoSkill          skip installing the Claude Code skill
#   -NoToolchain      don't check/install MSYS2 (you provide clang + gc yourself)
#   -ExeSource <p>    install ailc.exe from a local path or custom URL instead of
#                     the GitHub release (offline / self-built installs, CI)
#   -StdSource <p>    same, for the standard library bundle (ailc-std.tar.gz)
#
# Note: programs using the networking/regex stdlib (sockets/HTTP/TLS/Postgres/
# Redis/regex) are POSIX-only and won't build natively — run those under WSL.
# Re-run any time; every step is idempotent.

[CmdletBinding()]
param([switch]$NoSkill, [switch]$NoToolchain, [string]$ExeSource, [string]$StdSource)
$ErrorActionPreference = 'Stop'

$Repo      = 'Ray-T-r/AiLang'
$BaseUrl   = "https://github.com/$Repo/releases/latest/download"
$ExeAsset  = 'ailc-windows-x86_64.exe'
$StdAsset  = 'ailc-std.tar.gz'
$SkillDir  = Join-Path $env:USERPROFILE '.claude\skills\ailang'
$SkillPath = Join-Path $SkillDir 'SKILL.md'
$BinDir    = Join-Path $env:LOCALAPPDATA 'Programs\ailc'
$ExePath   = Join-Path $BinDir 'ailc.exe'
$Msys2Root = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { 'C:\msys64' }
$Mingw     = Join-Path $Msys2Root 'mingw64'
$MingwBin  = Join-Path $Mingw 'bin'

function Log  ($m) { Write-Host "==> $m" -ForegroundColor Cyan }
function OK   ($m) { Write-Host "   ok $m" -ForegroundColor Green }
function Warn ($m) { Write-Host "   !! $m" -ForegroundColor Yellow }
function Die  ($m) { Write-Host "ERROR $m" -ForegroundColor Red; exit 1 }

Log 'Installing native AiLang for Windows (ailc.exe — no WSL).'

# -------- 1. mingw64 toolchain (clang + Boehm GC) that ailc compiles with ------
$haveClang = Test-Path (Join-Path $MingwBin 'clang.exe')
$haveGc    = (Test-Path (Join-Path $Mingw 'include\gc.h')) -and `
             ((Test-Path (Join-Path $Mingw 'lib\libgc.a')) -or (Test-Path (Join-Path $Mingw 'lib\libgc.dll.a')))

if ($NoToolchain) {
    Log 'Skipping toolchain check (-NoToolchain). Ensure clang + Boehm GC are available.'
} elseif ($haveClang -and $haveGc) {
    OK "mingw64 toolchain present ($MingwBin)"
} else {
    Log 'Setting up the mingw64 toolchain (clang + Boehm GC) via MSYS2'
    $bash = Join-Path $Msys2Root 'usr\bin\bash.exe'
    if (-not (Test-Path $bash)) {
        if (Get-Command winget -ErrorAction SilentlyContinue) {
            Log 'Installing MSYS2 (winget install MSYS2.MSYS2)'
            winget install -e --id MSYS2.MSYS2 --accept-source-agreements --accept-package-agreements
        } else {
            Die "MSYS2 not found and winget is unavailable. Install MSYS2 from https://www.msys2.org, then re-run."
        }
    }
    if (-not (Test-Path $bash)) { Die "MSYS2 bash not found at $bash after install. Install MSYS2 manually, then re-run." }
    Log 'Installing mingw-w64-x86_64-clang + mingw-w64-x86_64-gc (pacman)'
    & $bash -lc 'pacman -Sy --noconfirm; pacman -S --needed --noconfirm mingw-w64-x86_64-clang mingw-w64-x86_64-gc'
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $MingwBin 'clang.exe'))) {
        Warn 'pacman did not complete (mirror/network?). Open "MSYS2 MINGW64" and run:'
        Warn '    pacman -S --needed mingw-w64-x86_64-clang mingw-w64-x86_64-gc'
        Warn '  (in mainland China, set a fast mirror first:'
        Warn "     echo 'Server = https://mirrors.tuna.tsinghua.edu.cn/msys2/mingw/x86_64' | Out-File -Encoding ascii $Msys2Root\etc\pacman.d\mirrorlist.mingw64 )"
        Die  'toolchain incomplete — install clang + gc as above, then re-run.'
    }
    OK 'mingw64 toolchain installed'
}

# -------- 2. download (or copy) the native ailc.exe ---------------------------
New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
if ($ExeSource) {
    if (Test-Path $ExeSource) {
        Log "Installing ailc.exe from local source: $ExeSource"
        Copy-Item -Force -LiteralPath $ExeSource -Destination $ExePath
    } else {
        Log "Downloading ailc.exe from $ExeSource"
        Invoke-WebRequest -Uri $ExeSource -OutFile $ExePath -UseBasicParsing
    }
} else {
    Log "Downloading $ExeAsset"
    try { Invoke-WebRequest -Uri "$BaseUrl/$ExeAsset" -OutFile $ExePath -UseBasicParsing }
    catch { Die "could not download $BaseUrl/$ExeAsset ($_). Has the Windows binary been published to the release yet?" }
}
if (-not (Test-Path $ExePath) -or (Get-Item $ExePath).Length -lt 1000) { Die "ailc.exe is missing or too small after install." }
OK "ailc.exe -> $ExePath"

# -------- 2b. standard library (so `im "std/..."` works from anywhere) --------
# The compiler falls back to $AILANG_STD when an import isn't found next to the
# source, so we drop std/ here and point AILANG_STD at this dir.
$StdTmp = Join-Path $env:TEMP 'ailc-std.tar.gz'
$gotStd = $false
try {
    if ($StdSource -and (Test-Path $StdSource)) { Copy-Item -Force -LiteralPath $StdSource -Destination $StdTmp; $gotStd = $true }
    elseif ($StdSource) { Invoke-WebRequest -Uri $StdSource -OutFile $StdTmp -UseBasicParsing; $gotStd = $true }
    else { Invoke-WebRequest -Uri "$BaseUrl/$StdAsset" -OutFile $StdTmp -UseBasicParsing; $gotStd = $true }
} catch { Warn "could not fetch the std library ($_) — `im `"std/...`"` won't work until it's installed." }
if ($gotStd) {
    $StdDir = Join-Path $BinDir 'std'
    if (Test-Path $StdDir) { Remove-Item -Recurse -Force $StdDir }
    tar -xzf $StdTmp -C $BinDir          # bundle contains std/ → extracts to $BinDir\std
    Remove-Item -Force $StdTmp -ErrorAction SilentlyContinue
    if (Test-Path (Join-Path $StdDir 'time.ail')) {
        [Environment]::SetEnvironmentVariable('AILANG_STD', $BinDir, 'User')
        $env:AILANG_STD = $BinDir
        OK "std library -> $StdDir   (AILANG_STD=$BinDir)"
    } else { Warn "std bundle extracted but std\time.ail is missing — check the archive layout." }
}

# `ailc` is the only command, matching the macOS/Linux install. Remove helper
# shims left by older installers (the WSL ailc.cmd/ailexe, and the ailrun wrapper).
foreach ($old in 'ailc.cmd','ailexe.cmd','ailexe-impl.ps1','ailrun.cmd','ailrun-impl.ps1') {
    $p = Join-Path $BinDir $old
    if (Test-Path $p) { Remove-Item -Force $p; OK "removed stale $old (ailc.exe is the only command)" }
}

# -------- 3. PATH: ailc dir + mingw64\bin (ailc shells out to clang) ----------
$up = [Environment]::GetEnvironmentVariable('Path','User'); if (-not $up) { $up = '' }
$parts = $up -split ';'
$added = @()
foreach ($d in @($BinDir, $MingwBin)) {
    if ($parts -notcontains $d) { $up = $up.TrimEnd(';') + ';' + $d; $added += $d }
}
if ($added.Count) {
    [Environment]::SetEnvironmentVariable('Path', $up, 'User')
    foreach ($d in $added) { OK "added to PATH: $d" }
    Warn 'open a NEW terminal for the PATH change to take effect.'
} else { OK 'PATH already has the ailc + mingw64 dirs' }

# -------- 4. the Claude Code skill -------------------------------------------
if (-not $NoSkill) {
    Log 'Installing the AiLang skill for Claude Code'
    New-Item -ItemType Directory -Force -Path $SkillDir | Out-Null
    try { Invoke-WebRequest -Uri "$BaseUrl/SKILL.md" -OutFile $SkillPath -UseBasicParsing; OK "skill -> $SkillPath" }
    catch { Warn "could not download SKILL.md (ailc itself is installed fine): $_" }
}

# -------- 5. verify: compile + run a hello program ---------------------------
Log 'Verifying: compiling and running a hello program'
$env:Path = "$BinDir;$MingwBin;$env:Path"   # this process only; user PATH already set above
$tmp = Join-Path $env:TEMP ('ailhello_' + [IO.Path]::GetRandomFileName().Replace('.',''))
$ail = "$tmp.ail"
[IO.File]::WriteAllText($ail, "println(`"hello from native ailc on windows`")`n", (New-Object Text.UTF8Encoding($false)))
$exitOk = $false
try {
    & $ExePath $ail $tmp | Out-Null
    if ((Test-Path "$tmp.exe")) {
        $out = (& "$tmp.exe" | Out-String).Trim()
        if ($out -match 'hello from native ailc on windows') { OK "ran: $out"; $exitOk = $true }
        else { Warn "compiled, but unexpected output: $out" }
    } else { Warn 'compile produced no .exe — is clang on PATH? (open a new terminal and retry)' }
} catch { Warn "verification step failed: $_" }
finally { Remove-Item -Force $ail, "$tmp.exe" -ErrorAction SilentlyContinue }

Write-Host @"

Done. Open a NEW terminal (so PATH refreshes), then:

    echo 'println("hello")' > hi.ail
    ailc hi.ail hi          # build a native, self-contained Windows hi.exe
    .\hi.exe                # run it

ailc produces a standalone .exe (no WSL, no DLLs beyond Windows' own). Programs
that use the networking/regex stdlib (sockets/HTTP/TLS/Postgres/Redis/regex) are
POSIX-only — build and run those under WSL.
"@ -ForegroundColor Green

# AiLang installer (Windows / PowerShell) — via WSL2, with native-feeling shims.
#
# The AiLang compiler's runtime is POSIX-only (BSD sockets, fork(), POSIX
# regex), so there is no native Windows build of the COMPILER. On Windows,
# AiLang runs inside WSL2 — and this installer adds shims to your Windows PATH
# so you never type `wsl` yourself:
#
#     ailrun foo.ail            # compile + run, output in your Windows terminal
#     ailc   foo.ail out        # produce a binary (Linux ELF, run via WSL)
#     ailexe foo.ail foo.exe    # build a NATIVE, self-contained Windows .exe *
#
#   * ailexe needs the MSYS2 mingw64 toolchain (clang + Boehm GC) on Windows;
#     it compiles the generated C to a static native .exe. It works for core
#     AiLang programs (the networking/regex stdlib is POSIX-only). ailrun needs
#     nothing beyond WSL.
#
# What it does:
#   1. ensures WSL2 + a Linux distro (installs it if missing),
#   2. runs the Linux installer inside WSL (ailc + deps + skill, in WSL),
#   3. installs ailc / ailrun / ailexe shims on the Windows PATH,
#   4. drops the AiLang skill on the Windows side too.
#
# One-liner:
#   iwr -useb https://github.com/Ray-T-r/AiLang/releases/latest/download/install.ps1 | iex
#
# Flags:  -NoSkill   skip installing the Claude Code skill on the Windows side

[CmdletBinding()]
param([switch]$NoSkill)
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

Log 'AiLang on Windows runs inside WSL2, fronted by native ailc/ailrun/ailexe shims.'

# -------- 1. is WSL usable (installed + a distro with bash)? --------
$wslReady = $false
if (Get-Command wsl -ErrorAction SilentlyContinue) {
    try { if ((wsl -e bash -c "echo __ailang_wsl_ok__" 2>$null) -match '__ailang_wsl_ok__') { $wslReady = $true } } catch { }
}
if (-not $wslReady) {
    Warn 'WSL2 with a Linux distro is not ready yet.'
    Log  'Installing it (needs admin + usually a reboot): wsl --install -d Ubuntu'
    try { wsl --install -d Ubuntu } catch { Warn "wsl --install failed: $_" }
    Write-Host @"

Next steps:
  1. Reboot if Windows asks you to.
  2. Launch "Ubuntu" once from the Start menu and create your Linux username/password.
  3. Re-run this installer:  iwr -useb $BaseUrl/install.ps1 | iex

"@ -ForegroundColor Yellow
    exit 0
}
OK 'WSL is ready'

# -------- 2. run the Linux installer inside WSL --------
Log 'Installing ailc inside WSL (native deps + the skill, in WSL)'
wsl -e bash -lc "sudo -v && curl -fsSL $BaseUrl/install.sh | bash"
if ($LASTEXITCODE -ne 0) { Warn "the in-WSL install returned exit code $LASTEXITCODE — check the output above." } else { OK 'ailc installed inside WSL' }

# -------- 3. native-feeling shims on the Windows PATH --------
Log 'Installing ailc / ailrun / ailexe shims on the Windows PATH'
$wslHome = (wsl -e bash -lc 'echo $HOME').Trim()
if (-not $wslHome) { $wslHome = '/root' }

# 3a. WSL forwarders (cwd + path translation, then run ailc).
$winrun = @"
#!/usr/bin/env bash
export PATH="`$HOME/.local/bin:`$PATH"
cwd="`$1"; shift
cd "`$(wslpath -u "`$cwd" 2>/dev/null)" 2>/dev/null || true
args=()
for a in "`$@"; do case "`$a" in [A-Za-z]:\\*|*\\*) args+=("`$(wslpath -u "`$a")");; *) args+=("`$a");; esac; done
exec ailc "`${args[@]}"
"@
$ailrunSh = @"
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
Push-ToWsl $winrun   "$wslHome/.local/bin/ailc-winrun"
Push-ToWsl $ailrunSh "$wslHome/.local/bin/ailrun-win"

# 3b. ailexe helper (Windows side): AiLang -> C (in WSL) -> native static .exe (mingw clang).
$ailexeImpl = @'
param([string]$WinCwd, [string]$Src, [string]$OutExe)
$ErrorActionPreference = 'Stop'
function To-WslPath([string]$p){ $d=$p.Substring(0,1).ToLower(); $r=$p.Substring(2) -replace '\\','/'; "/mnt/$d$r" }
function Die($m){ Write-Host "ailexe: $m" -ForegroundColor Red; exit 1 }
if (-not $Src) { Write-Host "usage: ailexe <file.ail> [out.exe]"; exit 2 }
if (-not [IO.Path]::IsPathRooted($Src)) { $Src = Join-Path $WinCwd $Src }
if (-not (Test-Path $Src)) { Die "not found: $Src" }
$srcFull = (Resolve-Path $Src).Path
$dir = Split-Path $srcFull
$stem = [IO.Path]::GetFileNameWithoutExtension($srcFull)
if (-not $OutExe) { $OutExe = Join-Path $dir "$stem.exe" }
elseif (-not [IO.Path]::IsPathRooted($OutExe)) { $OutExe = Join-Path $WinCwd $OutExe }
if (-not $OutExe.ToLower().EndsWith('.exe')) { $OutExe = "$OutExe.exe" }
$cbase = Join-Path $dir "$($stem)__ailexe"; $cfile = "$cbase.c"
$mingw = if ($env:MSYS2_ROOT) { Join-Path $env:MSYS2_ROOT 'mingw64' } else { 'C:\msys64\mingw64' }
$clang = Join-Path $mingw 'bin\clang.exe'
if (-not (Test-Path $clang)) { Die "native .exe needs the MSYS2 mingw64 toolchain (clang + Boehm GC).`n  Install:  winget install MSYS2.MSYS2`n  Then in the MSYS2 shell:  pacman -S --needed mingw-w64-x86_64-clang mingw-w64-x86_64-gc`n  (ailrun works without this.)" }
$srcW = To-WslPath $srcFull; $cbaseW = To-WslPath $cbase
wsl -e bash -lc "export PATH=`$HOME/.local/bin:`$PATH; ailc --keep-c '$srcW' '$cbaseW' >/tmp/ailexe.err 2>&1" | Out-Null
if (-not (Test-Path $cfile)) { Die ("codegen failed`n" + (wsl -e bash -lc "cat /tmp/ailexe.err 2>/dev/null")) }
& $clang $cfile -o $OutExe -DGC_NOT_DLL -static "-I$mingw\include" "-L$mingw\lib" -lgc -O2 2>$null
$rc = $LASTEXITCODE
Remove-Item -Force $cfile, $cbase -ErrorAction SilentlyContinue
if ($rc -ne 0) { Die "C compile failed. Programs using the networking/regex stdlib (sockets/http/tls/pg/redis/regex) can't be native .exe yet -- run them with 'ailrun' instead." }
Write-Host "built $OutExe" -ForegroundColor Green
'@

# 3c. Windows .cmd shims.
New-Item -ItemType Directory -Force -Path $ShimDir | Out-Null
[IO.File]::WriteAllText((Join-Path $ShimDir 'ailc.cmd'),    "@echo off`r`nwsl -e $wslHome/.local/bin/ailc-winrun `"%CD%`" %*`r`n")
[IO.File]::WriteAllText((Join-Path $ShimDir 'ailrun.cmd'),  "@echo off`r`nwsl -e $wslHome/.local/bin/ailrun-win `"%CD%`" %*`r`n")
[IO.File]::WriteAllText((Join-Path $ShimDir 'ailexe-impl.ps1'), $ailexeImpl)
[IO.File]::WriteAllText((Join-Path $ShimDir 'ailexe.cmd'),  "@echo off`r`npowershell -NoProfile -ExecutionPolicy Bypass -File `"%~dp0ailexe-impl.ps1`" `"%CD%`" %*`r`n")
OK "shims -> $ShimDir (ailc, ailrun, ailexe)"

# 3d. PATH.
$up = [Environment]::GetEnvironmentVariable('Path','User'); if (-not $up) { $up = '' }
if (($up -split ';') -notcontains $ShimDir) {
    [Environment]::SetEnvironmentVariable('Path', ($up.TrimEnd(';') + ';' + $ShimDir), 'User')
    OK "added $ShimDir to user PATH"
    Warn 'open a NEW terminal for PATH changes to take effect.'
} else { OK 'shim dir already on PATH' }

# 3e. ailexe toolchain check (optional feature).
$mingwClang = if ($env:MSYS2_ROOT) { Join-Path $env:MSYS2_ROOT 'mingw64\bin\clang.exe' } else { 'C:\msys64\mingw64\bin\clang.exe' }
if (Test-Path $mingwClang) {
    OK 'ailexe ready (MSYS2 mingw64 clang found)'
} else {
    Warn 'ailexe (native .exe) needs MSYS2 mingw64 clang + gc:'
    Warn '    winget install MSYS2.MSYS2'
    Warn '    then in the MSYS2 shell:  pacman -S --needed mingw-w64-x86_64-clang mingw-w64-x86_64-gc'
    Warn '  (ailc / ailrun work without it.)'
}

# -------- 4. Windows-side skill --------
if (-not $NoSkill) {
    Log 'Installing the AiLang skill on the Windows side (for Windows-native Claude Code)'
    New-Item -ItemType Directory -Force -Path $SkillDir | Out-Null
    try { Invoke-WebRequest -Uri "$BaseUrl/SKILL.md" -OutFile $SkillPath -UseBasicParsing; OK "skill -> $SkillPath" }
    catch { Warn "could not download SKILL.md for the Windows side: $_" }
}

Write-Host @"

Done. Open a NEW terminal (so PATH refreshes), then:

    echo 'println("hello")' > hi.ail
    ailrun hi.ail              # compile + run, output right here (needs only WSL)
    ailexe hi.ail hi.exe       # build a native, self-contained Windows .exe
    ailc   hi.ail prog         # Linux binary (run with: wsl ./prog)

ailrun/ailc forward into WSL transparently. ailexe additionally needs the MSYS2
mingw64 toolchain (clang + gc) and works for core AiLang programs.
"@ -ForegroundColor Green

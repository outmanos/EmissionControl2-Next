<#
.SYNOPSIS
    Prepare a fresh checkout of EmissionControl2-Next so it can be configured.

.DESCRIPTION
    The upstream submodules are pinned to commits that predate CMake 4 and current
    Visual Studio, so three things must happen before CMake will configure: the
    submodules must be checked out, the documented patches must be applied, and
    nativefiledialog (which upstream ships only as a VS2010 project) must be
    compiled.

    Every step is idempotent: running it again on a prepared tree changes nothing.

.PARAMETER SkipSubmodules
    Do not touch submodules; only apply patches and build nfd.lib.

.EXAMPLE
    tools\bootstrap.ps1
#>
[CmdletBinding()]
param([switch]$SkipSubmodules)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$ext = Join-Path $root 'ecSource\external'

function Write-Step($message) { Write-Host "==> $message" -ForegroundColor Cyan }

# Windows PowerShell 5.1 turns a native command's stderr into a terminating error
# when $ErrorActionPreference is Stop, even with 2>&1. Capture it explicitly and
# decide on the exit code instead.
function Invoke-Git {
    param([string[]]$Arguments)

    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & git @Arguments 2>&1 | Out-String
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    [pscustomobject]@{ ExitCode = $code; Output = $output }
}

# ---------------------------------------------------------------- submodules
if (-not $SkipSubmodules) {
    Write-Step 'checking out submodules at their pinned commits'

    # schannel fails on some machines with SEC_E_NO_CREDENTIALS; openssl works.
    # LFS only carries example media that the build never touches, so skip it.
    $env:GIT_TERMINAL_PROMPT = '0'
    $env:GIT_LFS_SKIP_SMUDGE = '1'

    $result = Invoke-Git @(
        '-c', 'http.sslBackend=openssl',
        '-c', 'filter.lfs.smudge=', '-c', 'filter.lfs.process=', '-c', 'filter.lfs.required=false',
        '-C', $root, 'submodule', 'update', '--init', '--recursive', '--force')
    if ($result.ExitCode -ne 0) {
        Write-Host $result.Output
        throw 'git submodule update failed'
    }
    Write-Host '    submodules at pinned commits'
}

# ------------------------------------------------------------------- patches
function Invoke-Patch([string]$repo, [string]$patch) {
    if (-not (Test-Path $patch)) { throw "patch not found: $patch" }
    $name = Split-Path $patch -Leaf

    if ((Invoke-Git @('-C', $repo, 'apply', '--check', '--reverse', $patch)).ExitCode -eq 0) {
        Write-Host "    already applied: $name"
        return
    }

    $check = Invoke-Git @('-C', $repo, 'apply', '--check', $patch)
    if ($check.ExitCode -ne 0) {
        Write-Host $check.Output
        throw "$name does not apply cleanly to $repo - is the submodule at its pinned commit?"
    }

    $apply = Invoke-Git @('-C', $repo, 'apply', $patch)
    if ($apply.ExitCode -ne 0) {
        Write-Host $apply.Output
        throw "failed to apply $name"
    }
    Write-Host "    applied: $name"
}

Write-Step 'applying build patches'
Invoke-Patch (Join-Path $ext 'allolib\external\rtaudio') (Join-Path $root 'patches\rtaudio-cmp0042.patch')
Invoke-Patch (Join-Path $ext 'allolib\external\rtmidi')  (Join-Path $root 'patches\rtmidi-cmp0042.patch')
Invoke-Patch (Join-Path $ext 'al_ext')                   (Join-Path $root 'patches\al_ext-remove-extensions.patch')
Invoke-Patch (Join-Path $ext 'allolib')                  (Join-Path $root 'patches\allolib-no-graphics.patch')
Invoke-Patch (Join-Path $root 'vendor\iPlug2')           (Join-Path $root 'patches\iPlug2-sdk-paths.patch')
Invoke-Patch (Join-Path $root 'vendor\iPlug2')           (Join-Path $root 'patches\iPlug2-runtime-library.patch')

# --------------------------------------------------------- nativefiledialog
Write-Step 'locating Visual Studio'
$candidates = @()
foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
    if (-not $base) { continue }
    $vsRoot = Join-Path $base 'Microsoft Visual Studio'
    if (Test-Path $vsRoot) {
        $candidates += Get-ChildItem $vsRoot -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { Join-Path $_.FullName '*\Common7\Tools\VsDevCmd.bat' } |
            ForEach-Object { Get-ChildItem $_ -ErrorAction SilentlyContinue }
    }
}
$vsDevCmd = ($candidates | Sort-Object FullName -Descending | Select-Object -First 1).FullName
if (-not $vsDevCmd) { throw 'Could not find VsDevCmd.bat. Pass its path to tools\build_nfd.cmd manually.' }
Write-Host "    $vsDevCmd"

Write-Step 'building nativefiledialog (nfd.lib)'
& (Join-Path $PSScriptRoot 'build_nfd.cmd') $vsDevCmd
if ($LASTEXITCODE -ne 0) { throw 'nativefiledialog build failed' }

# ------------------------------------------------------------- libsndfile
$dep = Join-Path $root 'build-deps\libsndfile'
if (-not (Test-Path (Join-Path $dep 'lib\sndfile.lib'))) {
    Write-Host ''
    Write-Warning 'libsndfile shim missing. Download libsndfile 1.2.2 win64 and copy into build-deps\libsndfile:'
    Write-Host '    include\sndfile.h, lib\sndfile.lib, bin\sndfile.dll'
    Write-Host '  Then also copy bin\sndfile.dll to bin\libsndfile-1.dll, the name EC2''s CMakeLists copies.'
}

Write-Step 'bootstrap complete'
Write-Host 'Next: configure with CMake, for example:'
Write-Host '  cmake -S ecSource -B ecSource\build\release -G "Visual Studio 18 2026" -A x64'
Write-Host '        -DCMAKE_POLICY_VERSION_MINIMUM=3.5'
Write-Host '        -DCMAKE_CXX_FLAGS="/FIchrono /wd4244 /DOSC_HOST_LITTLE_ENDIAN"'
Write-Host '        -DSNDFILE_LIBRARY=<repo>/build-deps/libsndfile/lib/sndfile.lib'
Write-Host '        -DSNDFILE_INCLUDE_DIR=<repo>/build-deps/libsndfile/include'
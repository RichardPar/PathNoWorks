<#
build-installer.ps1 -- build the Windows installer, PathNoWorks-<version>-setup.exe.

    packaging\windows\build-installer.ps1 [-Version 0.1.0]

Builds PathNoWorks and cppdecnet (which it finds as ..\Decnet\cppdecnet)
for release in build\release-win, gathers what they need to run into
build\stage -- the programs, Qt, and the C++ runtime, so the PC it goes on
needs none of them installed -- and compiles pathnoworks.iss with Inno
Setup into Windows\, at the top of the tree.  Needs Visual Studio 2022's C++ build tools, Qt 6 for
MSVC 2022 (C:\Qt), and Inno Setup 6.
#>

param ([string] $Version = "0.1.0")

$ErrorActionPreference = "Stop"

$here  = $PSScriptRoot
$repo  = Split-Path -Parent (Split-Path -Parent $here)
$build = Join-Path $repo "build\release-win"
$stage = Join-Path $repo "build\stage"

function Say ([string] $text) { Write-Host $text }
function Die ([string] $text) { Write-Host "build-installer.ps1: $text" -ForegroundColor Red; exit 1 }

# ------------------------------------------------------------ the tools

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Die "no Visual Studio here (vswhere.exe is missing)" }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { Die "Visual Studio has no C++ build tools" }
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"

$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
          "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
          "$env:ProgramFiles\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { Die "Inno Setup 6 isn't installed (winget install JRSoftware.InnoSetup)" }

# ------------------------------------------------------------ building

Say "Building in $build"
$configure = "cmake -S `"$repo`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPNW_TESTS=OFF"
cmd /c "call `"$vcvars`" >nul && $configure && cmake --build `"$build`""
if ($LASTEXITCODE -ne 0) { Die "the build failed" }
if (-not (Test-Path (Join-Path $build "gui\pathnoworks.exe"))) {
    Die "the desktop wasn't built: is Qt 6 for MSVC 2022 in C:\Qt?"
}

# ------------------------------------------------------------ staging

Say "Gathering the files in $stage"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# The programs: decnetd and its tools, the PathNoWorks tools, the desktop.
$programs = @(Get-ChildItem (Join-Path $build "cppdecnet") -Filter *.exe) +
            @(Get-ChildItem (Join-Path $build "tools") -Recurse -Filter *.exe) +
            @(Get-Item (Join-Path $build "gui\pathnoworks.exe"))
foreach ($p in $programs) { Copy-Item $p.FullName $stage }

# Qt, beside the desktop.
$qtcore = (Get-Content (Join-Path $build "CMakeCache.txt") |
           Select-String '^Qt6Core_DIR:PATH=(.*)$').Matches.Groups[1].Value
$qtbin = Join-Path $qtcore "..\..\..\bin"
& (Join-Path $qtbin "windeployqt.exe") --release --no-translations --no-compiler-runtime `
    --no-system-d3d-compiler --no-opengl-sw (Join-Path $stage "pathnoworks.exe") | Out-Null
if ($LASTEXITCODE -ne 0) { Die "windeployqt failed" }

# The C++ runtime, beside the programs, so nothing else need be installed.
$crt = Get-ChildItem (Join-Path $vs "VC\Redist\MSVC") -Directory |
       Where-Object { Test-Path (Join-Path $_.FullName "x64") } |
       Sort-Object Name -Descending | Select-Object -First 1
$crtdir = Get-ChildItem (Join-Path $crt.FullName "x64") -Directory -Filter "Microsoft.VC*.CRT" |
          Select-Object -First 1
Copy-Item (Join-Path $crtdir.FullName "*.dll") $stage

# The helper for decnetd's task, DEC's font names for VcXsrv, and the docs.
Copy-Item (Join-Path $here "decnetd-task.ps1") $stage
Copy-Item (Join-Path $repo "tools\pnw-x11\decw-font-aliases.py") $stage
$docs = Join-Path $stage "docs"
New-Item -ItemType Directory -Force -Path $docs | Out-Null
Copy-Item -Recurse (Join-Path $repo "docs\*") $docs
Copy-Item (Join-Path $repo "README.md") $docs
Copy-Item (Join-Path $repo "samples") $stage -Recurse
$cppdecnet = Join-Path (Split-Path -Parent $repo) "Decnet\cppdecnet"
if (Test-Path (Join-Path $cppdecnet "README.md")) {
    Copy-Item (Join-Path $cppdecnet "README.md") (Join-Path $docs "cppdecnet.md")
}

# ------------------------------------------------------------ packaging

Say "Compiling the installer"
& $iscc /Q "/DStage=$stage" "/DAppVersion=$Version" (Join-Path $here "pathnoworks.iss")
if ($LASTEXITCODE -ne 0) { Die "Inno Setup failed" }
$out = Join-Path $repo "Windows\PathNoWorks-$Version-setup.exe"
Say "Done: $out ($([math]::Round((Get-Item $out).Length / 1MB, 1)) MB)"

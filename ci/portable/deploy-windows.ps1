# Deploy the Windows runtime dependencies into the portable folder: resolve the
# executable's DLL closure via the shared DeployRuntimeDependencies.cmake helper
# (including the .NET host pack that provides nethost.dll), run windeployqt for
# the Qt DLL/plugin layout, and copy the Visual C++ CRT and OpenMP runtimes
# app-locally. Invoked by the "Deploy runtime dependencies" stage.
#
# Required environment:
#   VIEWER_STAGE      viewer staging prefix
#   OPENMS_INSTALL    OpenMS install prefix
#   OPENMS_CONTRIB    extracted OpenMS contrib prefix
#   QT_ROOT_DIR       Qt prefix (windeployqt)
#   GITHUB_WORKSPACE  OpenMS Viewer checkout root (for cmake/ helpers)
#   VCToolsRedistDir  (optional) Visual C++ redistributable root from the VS shell
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$bin = Join-Path $env:VIEWER_STAGE "bin"
$executable = Join-Path $bin "openms-viewer.exe"

# openms_thermo_bridge.dll links nethost.dll, the .NET host shim that
# locates hostfxr at run time. OpenMS does not install nethost.dll -- it
# is a .NET SDK artifact from the host pack OpenMS found at configure
# time (Microsoft.NETCore.App.Host.win-x64/<ver>/runtimes/win-x64/native)
# and copied next to the bridge in its build tree. The core-only install
# (OpenMS#9752) no longer keeps that build tree, so add the host pack's
# native directory to the deploy search path. nethost's ABI is stable,
# so the newest host pack resolves the bridge regardless of the runtime
# version bundled below. Pinning the newest matches OpenMS's own
# DotNetHost selection.
$hostPackRoot = Join-Path ${env:ProgramFiles} `
  "dotnet/packs/Microsoft.NETCore.App.Host.win-x64"
$nethostDirectory = $null
if (Test-Path $hostPackRoot) {
  $nethostDirectory = Get-ChildItem $hostPackRoot -Directory |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName "runtimes/win-x64/native" } |
    Where-Object { Test-Path (Join-Path $_ "nethost.dll") } |
    Select-Object -First 1
}
if ($nethostDirectory) { Write-Output "nethost.dll host pack: $nethostDirectory" }
else { throw "No .NET host pack with nethost.dll was found under $hostPackRoot" }

$searchDirectories = @(
  $bin,
  (Join-Path $env:OPENMS_INSTALL "bin"),
  (Join-Path $env:OPENMS_INSTALL "lib"),
  $nethostDirectory,
  (Join-Path $env:OPENMS_CONTRIB "bin"),
  (Join-Path $env:OPENMS_CONTRIB "lib"),
  (Join-Path $env:QT_ROOT_DIR "bin")
) | Where-Object { Test-Path $_ }

$deployArguments = @(
  "-DEXECUTABLE=$executable",
  "-DDESTINATION=$bin",
  "-DSEARCH_DIRECTORIES=$($searchDirectories -join ';')",
  "-P",
  (Join-Path $env:GITHUB_WORKSPACE "cmake/DeployRuntimeDependencies.cmake")
)
& cmake @deployArguments
if ($LASTEXITCODE -ne 0) { throw "Runtime dependency collection failed." }

# windeployqt owns the Qt DLL/plugin layout. The MSVC runtime is copied
# app-locally below so the ZIP needs no prerequisite installer.
$windeployqt = Join-Path $env:QT_ROOT_DIR "bin/windeployqt.exe"
& $windeployqt --release --no-translations --no-compiler-runtime $executable
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed." }

# App-local deployment is the portable Windows model: take the retail
# CRT DLLs from Visual Studio's Redist tree, never from System32.
$redistRoots = [System.Collections.Generic.List[string]]::new()
if ($env:VCToolsRedistDir -and (Test-Path $env:VCToolsRedistDir)) {
  $redistRoots.Add($env:VCToolsRedistDir)
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
if (Test-Path $vswhere) {
  $vsInstall = (& $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath).Trim()
  if ($vsInstall) {
    Get-ChildItem (Join-Path $vsInstall "VC/Redist/MSVC") -Directory |
      Sort-Object Name -Descending |
      ForEach-Object { $redistRoots.Add($_.FullName) }
  }
}

$crtDirectory = $null
$openmpDirectory = $null
foreach ($root in $redistRoots) {
  $x64Root = Join-Path $root "x64"
  if (-not (Test-Path $x64Root)) { continue }
  $candidateCrt = Get-ChildItem $x64Root -Directory -Filter "Microsoft.VC*.CRT" |
    Sort-Object Name -Descending |
    Select-Object -First 1
  $candidateOpenmp = Get-ChildItem $x64Root -Directory -Filter "Microsoft.VC*.OpenMP" |
    Sort-Object Name -Descending |
    Select-Object -First 1
  if ($candidateCrt -and $candidateOpenmp) {
    $crtDirectory = $candidateCrt
    $openmpDirectory = $candidateOpenmp
    break
  }
}
if (-not $crtDirectory) { throw "Could not locate the Visual C++ x64 redistributable DLLs." }
if (-not $openmpDirectory) { throw "Could not locate the Visual C++ x64 OpenMP runtime DLLs." }
Copy-Item (Join-Path $crtDirectory.FullName "*.dll") -Destination $bin -Force
Copy-Item (Join-Path $openmpDirectory.FullName "*.dll") -Destination $bin -Force

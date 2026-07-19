# Fail-closed verification of the Windows portable folder: required files exist,
# and the app plus the Thermo smoke probe run against ONLY the app-local DLLs.
# PATH is restricted to the bundle + System32 so Qt/OpenMS/contrib/Visual Studio
# installations on the runner cannot hide a missing app-local DLL. Invoked by the
# "Verify portable folder" stage.
#
# Required environment:
#   VIEWER_STAGE      viewer staging prefix
#   VIEWER_BUILD      viewer build directory (Thermo smoke probe source)
#   RUNNER_TEMP       runner scratch directory
#   THERMO_TEST_DATA  path to the sample Thermo RAW file
#   DOTNET_FXR_VERSION DOTNET_RUNTIME_VERSION   bundled .NET versions
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$bin = Join-Path $env:VIEWER_STAGE "bin"
$requiredPaths = @(
  (Join-Path $bin "openms-viewer.exe"),
  (Join-Path $bin "OpenMS.dll"),
  (Join-Path $bin "Qt6Core.dll"),
  (Join-Path $bin "Qt6Gui.dll"),
  (Join-Path $bin "Qt6Widgets.dll"),
  (Join-Path $bin "Qt6Svg.dll"),
  (Join-Path $bin "vcruntime140.dll"),
  (Join-Path $bin "msvcp140.dll"),
  (Join-Path $bin "vcomp140.dll"),
  (Join-Path $bin "openms_thermo_bridge.dll"),
  (Join-Path $bin "nethost.dll"),
  (Join-Path $bin "managed/ThermoWrapperManaged.dll"),
  (Join-Path $bin "managed/ThermoWrapperManaged.runtimeconfig.json"),
  (Join-Path $bin "managed/ThermoFisher.CommonCore.RawFileReader.dll"),
  (Join-Path $bin "platforms/qwindows.dll"),
  (Join-Path $env:VIEWER_STAGE "dotnet/dotnet.exe"),
  (Join-Path $env:VIEWER_STAGE "dotnet/host/fxr/$env:DOTNET_FXR_VERSION/hostfxr.dll"),
  (Join-Path $env:VIEWER_STAGE "dotnet/shared/Microsoft.NETCore.App/$env:DOTNET_RUNTIME_VERSION/System.Private.CoreLib.dll"),
  (Join-Path $env:VIEWER_STAGE "share/OpenMS/CHEMISTRY/unimod.xml")
)
foreach ($path in $requiredPaths) {
  if (-not (Test-Path $path)) { throw "Portable package is missing: $path" }
}

# Do not let Qt, OpenMS, contrib, or Visual Studio installations on the
# runner's PATH hide a missing app-local DLL.
$savedPath = $env:PATH
$savedDotnetRoot = $env:DOTNET_ROOT
$savedOpenmsDataPath = $env:OPENMS_DATA_PATH
$savedCommandLineNoMessageBoxes = $env:QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES
try {
  $env:PATH = "$bin;$env:SystemRoot/System32;$env:SystemRoot"
  $env:DOTNET_ROOT = Join-Path $env:VIEWER_STAGE "dotnet"
  $env:OPENMS_DATA_PATH = Join-Path $env:VIEWER_STAGE "share/OpenMS"
  # QCommandLineParser otherwise displays --version in a modal dialog
  # for a Windows GUI executable without an attached console.
  $env:QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES = "1"
  $process = Start-Process `
    -FilePath (Join-Path $bin "openms-viewer.exe") `
    -ArgumentList "--version" -PassThru
  if (-not $process.WaitForExit(30000)) {
    Stop-Process -Id $process.Id -Force
    throw "Portable executable smoke test timed out."
  }
  if ($process.ExitCode -ne 0) {
    throw "Portable executable smoke test exited with $($process.ExitCode)."
  }

  # Load a small real RAW file against only the packaged DLLs,
  # managed bridge, app-local .NET runtime, and shared data.
  $thermoTest = Get-ChildItem $env:VIEWER_BUILD `
    -Filter "openms-viewer-thermo-smoke.exe" -File -Recurse |
    Select-Object -First 1
  if (-not $thermoTest) { throw "Thermo RAW smoke probe was not built." }
  $smokeDirectory = Join-Path $env:RUNNER_TEMP "openms-viewer-thermo-smoke"
  New-Item -ItemType Directory -Path $smokeDirectory -Force | Out-Null
  $smokeExecutable = Join-Path $smokeDirectory "openms-viewer-thermo-smoke.exe"
  Copy-Item $thermoTest.FullName $smokeExecutable -Force
  $smokeProcess = Start-Process `
    -FilePath $smokeExecutable `
    -ArgumentList $env:THERMO_TEST_DATA -NoNewWindow -PassThru
  if (-not $smokeProcess.WaitForExit(300000)) {
    Stop-Process -Id $smokeProcess.Id -Force
    throw "Packaged Thermo RAW smoke test timed out."
  }
  if ($smokeProcess.ExitCode -ne 0) {
    throw "Packaged Thermo RAW smoke test exited with $($smokeProcess.ExitCode)."
  }
}
finally {
  $env:PATH = $savedPath
  $env:DOTNET_ROOT = $savedDotnetRoot
  $env:OPENMS_DATA_PATH = $savedOpenmsDataPath
  $env:QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES = $savedCommandLineNoMessageBoxes
}

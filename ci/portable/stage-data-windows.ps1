# Stage the OpenMS shared data tree, the Thermo managed bridge, third-party
# licenses, provenance (BUILD-INFO.txt), and the Windows README into the viewer
# package. Invoked by the "Add licenses and build provenance" stage.
#
# Required environment:
#   VIEWER_STAGE          viewer staging prefix
#   OPENMS_INSTALL        OpenMS install prefix
#   OPENMS_CONTRIB_SOURCE cloned OpenMS contrib source (license texts)
#   QT_ROOT_DIR           Qt prefix (Qt LICENSES)
#   QT_VERSION            Qt version string (provenance)
#   VIEWER_SHA OPENMS_REF OPENMS_SHA CONTRIB_TAG
#   DOTNET_RUNTIME_VERSION DOTNET_FXR_VERSION   provenance values from earlier stages
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$managedSource = Join-Path $env:OPENMS_INSTALL "lib/openms_thermo_bridge/managed"
$managedDestination = Join-Path $env:VIEWER_STAGE "bin/managed"
if (-not (Test-Path $managedSource)) {
  throw "The installed OpenMS Thermo managed bridge was not found."
}
Copy-Item $managedSource $managedDestination -Recurse

$shareRoot = Join-Path $env:VIEWER_STAGE "share"
Copy-Item (Join-Path $env:OPENMS_INSTALL "share/OpenMS") `
  (Join-Path $shareRoot "OpenMS") -Recurse

$licenseRoot = Join-Path $env:VIEWER_STAGE "share/licenses"
New-Item -ItemType Directory -Path $licenseRoot -Force | Out-Null
Copy-Item (Join-Path $env:OPENMS_INSTALL "share/OpenMS/LICENSES/OpenMS-BSD-3-Clause.txt") `
  (Join-Path $licenseRoot "OpenMS-BSD-3-Clause.txt")
Copy-Item (Join-Path $env:OPENMS_CONTRIB_SOURCE "LICENSE.md") `
  (Join-Path $licenseRoot "OpenMS-contrib.md")
Copy-Item (Join-Path $env:OPENMS_CONTRIB_SOURCE "licensetexts") `
  (Join-Path $licenseRoot "OpenMS-contrib-licenses") -Recurse
$thermoLicense = Join-Path $env:OPENMS_INSTALL "share/OpenMS/LICENSES/ThermoRawFileReader-License.doc"
if (Test-Path $thermoLicense) {
  Copy-Item $thermoLicense $licenseRoot
}
if (Test-Path (Join-Path $env:QT_ROOT_DIR "LICENSES")) {
  Copy-Item (Join-Path $env:QT_ROOT_DIR "LICENSES") `
    (Join-Path $licenseRoot "Qt") -Recurse
}

$buildInfo = @"
OpenMS Viewer portable Windows build
Viewer commit: $env:VIEWER_SHA
OpenMS ref: $env:OPENMS_REF (pinned)
OpenMS commit: $env:OPENMS_SHA
OpenMS contrib release: $env:CONTRIB_TAG
OpenMS WITH_GUI: OFF
OpenMS BUILD_TOPP_TOOLS: OFF
OpenMS WITH_THERMO_RAW: ON
OpenMS WITH_OPENTIMS: ON
OpenMS OpenMP: ON
Qt: $env:QT_VERSION (win64_msvc2022_64)
.NET runtime: $env:DOTNET_RUNTIME_VERSION (hostfxr $env:DOTNET_FXR_VERSION, x64, app-local)
Architecture: x64
Built at: $((Get-Date).ToUniversalTime().ToString("o"))
"@
$buildInfo.Trim() | Set-Content `
  (Join-Path $env:VIEWER_STAGE "BUILD-INFO.txt") -Encoding utf8
"Run bin\openms-viewer.exe. No installation is required." | Set-Content `
  (Join-Path $env:VIEWER_STAGE "README-WINDOWS.txt") -Encoding utf8

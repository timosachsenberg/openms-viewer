# Stage the app-local .NET 8 runtime and hostfxr next to the viewer so the
# OpenMS Thermo bridge resolves them without a system .NET install, then smoke
# test the bundled runtime. Publishes DOTNET_RUNTIME_VERSION / DOTNET_FXR_VERSION
# for later stages via GITHUB_ENV. Invoked by the "Bundle .NET 8 runtime" stage.
#
# Required environment:
#   VIEWER_STAGE  viewer staging prefix
#   GITHUB_ENV    GitHub Actions environment file (for cross-step outputs)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$runtimeCandidates = @()
foreach ($line in (& dotnet --list-runtimes)) {
  if ($line -match '^Microsoft\.NETCore\.App\s+(\d+\.\d+\.\d+)\s+\[(.+)\]$') {
    $version = [version]$Matches[1]
    if ($version.Major -eq 8) {
      $runtimeCandidates += [pscustomobject]@{
        Version = $version
        Root = $Matches[2]
      }
    }
  }
}
$runtime = $runtimeCandidates |
  Sort-Object Version -Descending |
  Select-Object -First 1
if (-not $runtime) { throw "The .NET 8 runtime installed with the SDK was not found." }

$runtimeVersion = $runtime.Version.ToString()
$sourceDotnetRoot = Split-Path (Split-Path $runtime.Root -Parent) -Parent
$runtimeSource = Join-Path $runtime.Root $runtimeVersion
$fxrSource = Get-ChildItem (Join-Path $sourceDotnetRoot "host/fxr") -Directory |
  Where-Object { ([version]$_.Name).Major -eq 8 } |
  Sort-Object { [version]$_.Name } -Descending |
  Select-Object -First 1
if (-not $fxrSource) { throw "A .NET 8 hostfxr directory was not found." }

$targetDotnetRoot = Join-Path $env:VIEWER_STAGE "dotnet"
$targetRuntimeParent = Join-Path $targetDotnetRoot "shared/Microsoft.NETCore.App"
$targetFxrParent = Join-Path $targetDotnetRoot "host/fxr"
New-Item -ItemType Directory -Path $targetRuntimeParent -Force | Out-Null
New-Item -ItemType Directory -Path $targetFxrParent -Force | Out-Null
Copy-Item $runtimeSource (Join-Path $targetRuntimeParent $runtimeVersion) -Recurse
Copy-Item $fxrSource.FullName (Join-Path $targetFxrParent $fxrSource.Name) -Recurse
Copy-Item (Join-Path $sourceDotnetRoot "dotnet.exe") $targetDotnetRoot
foreach ($notice in @("LICENSE.txt", "ThirdPartyNotices.txt")) {
  $source = Join-Path $sourceDotnetRoot $notice
  if (Test-Path $source) { Copy-Item $source $targetDotnetRoot }
}

$savedDotnetRoot = $env:DOTNET_ROOT
try {
  $env:DOTNET_ROOT = $targetDotnetRoot
  & (Join-Path $targetDotnetRoot "dotnet.exe") --list-runtimes
  if ($LASTEXITCODE -ne 0) { throw "The bundled .NET runtime failed its smoke test." }
}
finally {
  $env:DOTNET_ROOT = $savedDotnetRoot
}
"DOTNET_RUNTIME_VERSION=$runtimeVersion" | Out-File `
  -FilePath $env:GITHUB_ENV -Append -Encoding utf8
"DOTNET_FXR_VERSION=$($fxrSource.Name)" | Out-File `
  -FilePath $env:GITHUB_ENV -Append -Encoding utf8

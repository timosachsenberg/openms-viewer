# Create the portable ZIP and its SHA-256 checksum, and publish the artifact
# name for the upload step via GITHUB_OUTPUT. Invoked by the "Create ZIP and
# checksum" stage.
#
# Required environment:
#   VIEWER_STAGE      viewer staging prefix (packaged folder)
#   GITHUB_WORKSPACE  OpenMS Viewer checkout root
#   OPENMS_SHORT_SHA  short OpenMS commit for the artifact name
#   GITHUB_OUTPUT     GitHub Actions step-output file
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$artifactName = "OpenMSViewer-Windows-x64-openms-$env:OPENMS_SHORT_SHA"
$artifactDirectory = Join-Path $env:GITHUB_WORKSPACE "artifacts"
$zip = Join-Path $artifactDirectory "$artifactName.zip"
New-Item -ItemType Directory -Path $artifactDirectory -Force | Out-Null
Compress-Archive -Path $env:VIEWER_STAGE -DestinationPath $zip -CompressionLevel Optimal
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $artifactName.zip" | Set-Content "$zip.sha256" -Encoding ascii
"artifact_name=$artifactName" | Out-File `
  -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8

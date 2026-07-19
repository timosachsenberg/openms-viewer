#!/usr/bin/env bash
# Create the portable ZIP and its SHA-256 checksum, and publish the artifact
# name for the upload step via GITHUB_OUTPUT. Invoked by the "Create ZIP and
# checksum" stage.
#
# Required environment:
#   RUNNER_OS         "Linux" or "macOS"
#   GITHUB_WORKSPACE  OpenMS Viewer checkout root
#   VIEWER_STAGE      viewer staging prefix (packaged folder)
#   PACKAGE_PLATFORM  "Linux" or "macOS" (matrix.platform)
#   ARCH              package arch label (matrix.arch)
#   OPENMS_SHORT_SHA  short OpenMS commit for the artifact name
#   GITHUB_OUTPUT     GitHub Actions step-output file
# shellcheck disable=SC2154  # inputs above are provided by the workflow environment
set -eo pipefail

artifact_name="OpenMSViewer-$PACKAGE_PLATFORM-$ARCH-openms-$OPENMS_SHORT_SHA"
artifact_directory="$GITHUB_WORKSPACE/artifacts"
zip_path="$artifact_directory/$artifact_name.zip"
mkdir -p "$artifact_directory"

if [[ "$RUNNER_OS" == "macOS" ]]; then
  ditto -c -k --sequesterRsrc --keepParent "$VIEWER_STAGE" "$zip_path"
  shasum -a 256 "$zip_path" | \
    sed "s#  .*#  $artifact_name.zip#" > "$zip_path.sha256"
else
  (
    cd "$(dirname "$VIEWER_STAGE")"
    zip -r -9 "$zip_path" "$(basename "$VIEWER_STAGE")"
  )
  sha256sum "$zip_path" | \
    sed "s#  .*#  $artifact_name.zip#" > "$zip_path.sha256"
fi
echo "artifact_name=$artifact_name" >> "$GITHUB_OUTPUT"

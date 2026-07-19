#!/usr/bin/env bash
# Fail-closed verification and ad-hoc signing of the macOS app bundle: required
# files exist, the app plus the Thermo smoke probe run in a sanitized
# environment, then every native Mach-O object is signed bottom-up and the
# bundle is verified and re-launched. Invoked by the "Verify and sign macOS
# portable folder" stage. macOS only.
#
# Required environment:
#   VIEWER_STAGE      viewer staging prefix
#   RUNNER_TEMP       runner scratch directory
#   THERMO_TEST_DATA  path to the sample Thermo RAW file
#   DOTNET_FXR_VERSION DOTNET_RUNTIME_VERSION   bundled .NET versions
# shellcheck disable=SC2154  # inputs above are provided by the workflow environment
set -eo pipefail

app="$VIEWER_STAGE/OpenMS Viewer.app"
contents="$app/Contents"
executable="$contents/MacOS/OpenMS Viewer"
smoke="$contents/MacOS/openms-viewer-thermo-smoke"
test -x "$executable"
test -x "$smoke"
test -f "$contents/PlugIns/platforms/libqcocoa.dylib"
test -f "$contents/PlugIns/platforms/libqoffscreen.dylib"
compgen -G "$contents/Frameworks/libOpenMS*.dylib" >/dev/null
compgen -G "$contents/Frameworks/libopenms_thermo_bridge*.dylib" >/dev/null
test -f "$contents/Frameworks/managed/ThermoWrapperManaged.dll"
test -f "$contents/Frameworks/managed/ThermoWrapperManaged.runtimeconfig.json"
test -x "$contents/dotnet/dotnet"
test -f "$contents/dotnet/host/fxr/$DOTNET_FXR_VERSION/libhostfxr.dylib"
test -f "$contents/dotnet/shared/Microsoft.NETCore.App/$DOTNET_RUNTIME_VERSION/System.Private.CoreLib.dll"
test -f "$contents/share/OpenMS/CHEMISTRY/unimod.xml"

mkdir -p "$RUNNER_TEMP/openms-viewer-home"
perl -e 'alarm shift; exec @ARGV' 30 \
  env -i \
    HOME="$RUNNER_TEMP/openms-viewer-home" \
    PATH=/usr/bin:/bin \
    QT_QPA_PLATFORM=offscreen \
    OMP_NUM_THREADS=2 \
    "$executable" --version

perl -e 'alarm shift; exec @ARGV' 300 \
  env -i \
    HOME="$RUNNER_TEMP/openms-viewer-home" \
    PATH=/usr/bin:/bin \
    DOTNET_ROOT="$contents/dotnet" \
    OPENMS_DATA_PATH="$contents/share/OpenMS" \
    OMP_NUM_THREADS=2 \
    "$smoke" "$THERMO_TEST_DATA"
rm "$smoke"

# Ad-hoc signing makes the moved .app internally consistent after all
# install-name edits. Production notarization can later replace this
# with a Developer ID identity without changing the bundle layout.
while IFS= read -r item; do
  if file "$item" | grep -q 'Mach-O'; then
    codesign --force --sign - "$item"
  fi
done < <(find "$app" -type f -print)
# Sign/verify WITHOUT --deep: the loop above already signed every native
# Mach-O object bottom-up. --deep would instead recurse into
# Frameworks/managed and reject the .NET managed assemblies there — those
# are CLR-loaded data (PE, not Mach-O), not code macOS should sign. The
# app already launched and read real Thermo data in the smoke tests
# above, proving dynamic-loader consistency.
codesign --force --sign - "$app"
codesign --verify --verbose=2 "$app"
perl -e 'alarm shift; exec @ARGV' 30 \
  env -i \
    HOME="$RUNNER_TEMP/openms-viewer-home" \
    PATH=/usr/bin:/bin \
    QT_QPA_PLATFORM=offscreen \
    "$executable" --version

#!/usr/bin/env bash
# Fail-closed verification of the Linux portable folder: required files exist,
# every staged ELF resolves its dependencies, and the app plus the Thermo smoke
# probe run in a sanitized environment (including an adversarial decoy
# OPENMS_DATA_PATH). Invoked by the "Verify Linux portable folder" stage. Linux
# only.
#
# Required environment:
#   VIEWER_STAGE      viewer staging prefix
#   RUNNER_TEMP       runner scratch directory
#   THERMO_TEST_DATA  path to the sample Thermo RAW file
#   DOTNET_FXR_VERSION DOTNET_RUNTIME_VERSION   bundled .NET versions
# shellcheck disable=SC2154  # inputs above are provided by the workflow environment
set -eo pipefail

bin="$VIEWER_STAGE/bin"
lib="$VIEWER_STAGE/lib"
test -x "$bin/openms-viewer"
test -x "$bin/openms-viewer-thermo-smoke"
test -f "$bin/platforms/libqxcb.so"
test -f "$bin/platforms/libqoffscreen.so"
compgen -G "$lib/libOpenMS.so*" >/dev/null
compgen -G "$lib/libopenms_thermo_bridge.so*" >/dev/null
test -f "$lib/managed/ThermoWrapperManaged.dll"
test -f "$lib/managed/ThermoWrapperManaged.runtimeconfig.json"
test -x "$VIEWER_STAGE/dotnet/dotnet"
test -f "$VIEWER_STAGE/dotnet/host/fxr/$DOTNET_FXR_VERSION/libhostfxr.so"
test -f "$VIEWER_STAGE/dotnet/shared/Microsoft.NETCore.App/$DOTNET_RUNTIME_VERSION/System.Private.CoreLib.dll"
test -f "$VIEWER_STAGE/share/OpenMS/CHEMISTRY/unimod.xml"

# ldd must resolve every staged application/plugin/library edge without
# inheriting a build directory through LD_LIBRARY_PATH.
while IFS= read -r binary; do
  if file "$binary" | grep -q 'ELF'; then
    dependencies=$(ldd "$binary" 2>&1) || {
      echo "$dependencies" >&2
      exit 1
    }
    if grep -q 'not found' <<< "$dependencies"; then
      echo "Unresolved dependency in $binary:" >&2
      echo "$dependencies" >&2
      exit 1
    fi
  fi
done < <(find "$bin" "$lib" -type f -print)

mkdir -p "$RUNNER_TEMP/openms-viewer-home"
# Clean environment: the portable build must locate its own bundled
# share/OpenMS (this exercises the self-location + fail-closed logic).
env -i \
  HOME="$RUNNER_TEMP/openms-viewer-home" \
  PATH=/usr/bin:/bin \
  QT_QPA_PLATFORM=offscreen \
  OMP_NUM_THREADS=2 \
  timeout 30 "$bin/openms-viewer" --version

# Adversarial: a *valid* competing OpenMS share pointed to by a stray
# OPENMS_DATA_PATH must be overridden by the bundle. If the app honoured
# the env var, its resolved-path check would fail and it would exit non-zero.
decoy="$RUNNER_TEMP/decoy-openms-share"
mkdir -p "$decoy/CHEMISTRY"
cp "$VIEWER_STAGE/share/OpenMS/CHEMISTRY/unimod.xml" "$decoy/CHEMISTRY/unimod.xml"
env -i \
  HOME="$RUNNER_TEMP/openms-viewer-home" \
  PATH=/usr/bin:/bin \
  QT_QPA_PLATFORM=offscreen \
  OMP_NUM_THREADS=2 \
  OPENMS_DATA_PATH="$decoy" \
  timeout 30 "$bin/openms-viewer" --version

env -i \
  HOME="$RUNNER_TEMP/openms-viewer-home" \
  PATH=/usr/bin:/bin \
  DOTNET_ROOT="$VIEWER_STAGE/dotnet" \
  OPENMS_DATA_PATH="$VIEWER_STAGE/share/OpenMS" \
  OMP_NUM_THREADS=2 \
  timeout 300 "$bin/openms-viewer-thermo-smoke" "$THERMO_TEST_DATA"
rm "$bin/openms-viewer-thermo-smoke"

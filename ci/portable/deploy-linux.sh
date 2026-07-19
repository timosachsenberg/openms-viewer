#!/usr/bin/env bash
# Deploy the Linux runtime dependencies into the portable folder: copy Qt
# plugins, resolve the executable/plugin dependency closure via the shared
# DeployRuntimeDependencies.cmake helper, and repair every staged ELF's RPATH
# so the extracted folder is relocatable. Invoked by the "Deploy Linux runtime
# dependencies" stage. Linux only.
#
# Required environment:
#   GITHUB_WORKSPACE  OpenMS Viewer checkout root (for cmake/ helpers)
#   VIEWER_STAGE      viewer staging prefix
#   VIEWER_BUILD      viewer build directory (Thermo smoke probe source)
#   QT_PLUGIN_DIR     Qt plugin directory
#   QT_ROOT_DIR       Qt prefix
#   OPENMS_INSTALL    OpenMS install prefix
#   OPENMS_CONTRIB    extracted OpenMS contrib prefix
# shellcheck disable=SC2154  # inputs above are provided by the workflow environment
# shellcheck disable=SC2016  # '$ORIGIN' is a literal RPATH token expanded by ld.so, not the shell
set -eo pipefail

bin="$VIEWER_STAGE/bin"
lib="$VIEWER_STAGE/lib"
mkdir -p "$lib"

# Qt discovers these plugin subdirectories relative to the executable.
# They are explicit dependency roots because plugins are loaded with
# dlopen and therefore are not visible from the executable's ELF graph.
for plugin_group in \
  iconengines imageformats platforminputcontexts platforms tls xcbglintegrations; do
  if [[ -d "$QT_PLUGIN_DIR/$plugin_group" ]]; then
    cp -a "$QT_PLUGIN_DIR/$plugin_group" "$bin/$plugin_group"
  fi
done

mapfile -t plugin_libraries < <(
  find "$bin" -mindepth 2 -type f \
    \( -name '*.so' -o -name '*.so.*' \) -print
)
if (( ${#plugin_libraries[@]} == 0 )); then
  echo "No deployed Qt plugins were found." >&2
  exit 1
fi
plugin_list=$(IFS=';'; echo "${plugin_libraries[*]}")
search_directories=(
  "$OPENMS_INSTALL/bin"
  "$OPENMS_INSTALL/lib"
  "$OPENMS_CONTRIB/bin"
  "$OPENMS_CONTRIB/lib"
  "$QT_ROOT_DIR/bin"
  "$QT_ROOT_DIR/lib"
)
search_list=$(IFS=';'; echo "${search_directories[*]}")

cmake \
  "-DEXECUTABLE=$bin/openms-viewer" \
  "-DLIBRARIES=$plugin_list" \
  "-DDESTINATION=$lib" \
  "-DSEARCH_DIRECTORIES=$search_list" \
  -P "$GITHUB_WORKSPACE/cmake/DeployRuntimeDependencies.cmake"

# Repair every staged ELF object so no build/install directory is
# required after the extracted folder is moved elsewhere. Use
# --force-rpath (DT_RPATH, searched BEFORE LD_LIBRARY_PATH) rather than
# the default DT_RUNPATH, so a second OpenMS on LD_LIBRARY_PATH cannot
# shadow the bundled one.
patchelf --force-rpath --set-rpath '$ORIGIN/../lib' "$bin/openms-viewer"
while IFS= read -r library; do
  if patchelf --print-rpath "$library" >/dev/null 2>&1; then
    patchelf --force-rpath --set-rpath '$ORIGIN' "$library"
  fi
done < <(find "$lib" -type f -print)
while IFS= read -r plugin; do
  if patchelf --print-rpath "$plugin" >/dev/null 2>&1; then
    patchelf --force-rpath --set-rpath '$ORIGIN/../../lib' "$plugin"
  fi
done < <(find "$bin" -mindepth 2 -type f -print)

# Confirm we produced DT_RPATH, not DT_RUNPATH (patchelf --print-rpath
# cannot tell them apart).
readelf -d "$bin/openms-viewer" | grep -q 'RPATH' \
  || { echo "openms-viewer is missing DT_RPATH" >&2; exit 1; }
if readelf -d "$bin/openms-viewer" | grep -q 'RUNPATH'; then
  echo "openms-viewer has DT_RUNPATH; expected DT_RPATH" >&2
  exit 1
fi

smoke=$(find "$VIEWER_BUILD" -type f -name 'openms-viewer-thermo-smoke' \
  -perm -u+x -print -quit)
test -n "$smoke"
cp "$smoke" "$bin/openms-viewer-thermo-smoke"
patchelf --force-rpath --set-rpath '$ORIGIN/../lib' "$bin/openms-viewer-thermo-smoke"

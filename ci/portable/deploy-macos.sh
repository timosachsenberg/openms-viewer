#!/usr/bin/env bash
# Deploy the macOS application bundle: run macdeployqt, add the headless
# offscreen plugin, stage the Thermo smoke probe, fix up remaining
# dependencies via the shared FixupMacBundle.cmake helper, wire the managed
# assemblies symlink, and move the .NET runtime into the bundle. Invoked by the
# "Deploy macOS application bundle" stage. macOS only.
#
# Required environment:
#   GITHUB_WORKSPACE  OpenMS Viewer checkout root (for cmake/ helpers)
#   VIEWER_STAGE      viewer staging prefix
#   VIEWER_BUILD      viewer build directory (Thermo smoke probe source)
#   QT_ROOT_DIR       Qt prefix (macdeployqt)
#   QT_PLUGIN_DIR     Qt plugin directory
#   OPENMS_INSTALL    OpenMS install prefix
#   OPENMS_CONTRIB    extracted OpenMS contrib prefix
#   HOMEBREW_PREFIX   output of `brew --prefix`
# shellcheck disable=SC2154  # inputs above are provided by the workflow environment
set -eo pipefail

app="$VIEWER_STAGE/OpenMS Viewer.app"
executable="$app/Contents/MacOS/OpenMS Viewer"
test -x "$executable"

"$QT_ROOT_DIR/bin/macdeployqt" "$app" \
  -always-overwrite \
  -libpath="$OPENMS_INSTALL/lib" \
  -libpath="$OPENMS_CONTRIB/lib" \
  -verbose=2

# Keep a headless platform plugin solely for CI and command-line
# smoke tests; the normal Cocoa plugin remains the desktop default.
mkdir -p "$app/Contents/PlugIns/platforms"
cp "$QT_PLUGIN_DIR/platforms/libqoffscreen.dylib" \
  "$app/Contents/PlugIns/platforms/"

smoke=$(find "$VIEWER_BUILD" -type f -name 'openms-viewer-thermo-smoke' \
  -perm -u+x -print -quit)
test -n "$smoke"
smoke_destination="$app/Contents/MacOS/openms-viewer-thermo-smoke"
cp "$smoke" "$smoke_destination"

openmp_root=$(brew --prefix libomp)
search_directories=(
  "$OPENMS_INSTALL/bin"
  "$OPENMS_INSTALL/lib"
  "$OPENMS_CONTRIB/bin"
  "$OPENMS_CONTRIB/lib"
  "$QT_ROOT_DIR/bin"
  "$QT_ROOT_DIR/lib"
  "$HOMEBREW_PREFIX/lib"
  "$(brew --prefix qtsvg)/lib"
  "$(brew --prefix qtimageformats)/lib"
  "$openmp_root/lib"
)
search_list=$(IFS=';'; echo "${search_directories[*]}")
cmake \
  "-DBUNDLE=$app" \
  "-DSEARCH_DIRECTORIES=$search_list" \
  -P "$GITHUB_WORKSPACE/cmake/FixupMacBundle.cmake"

# The .NET managed assemblies were staged under Resources/ so codesign
# seals them as resources instead of rejecting them as unsigned code in
# Frameworks/. Symlink them back under Frameworks (created by macdeployqt)
# so the Thermo bridge's module_dir/managed lookup still resolves.
ln -s ../Resources/managed "$app/Contents/Frameworks/managed"

mv "$VIEWER_STAGE/.dotnet-runtime" "$app/Contents/dotnet"

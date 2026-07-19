#!/usr/bin/env bash
# Configure, build, and install OpenMS Viewer in portable mode against the
# installed OpenMS core, plus the Thermo RAW smoke probe. Invoked by the
# "Build and install OpenMS Viewer" stage of the windows-portable workflow.
#
# Required environment:
#   GITHUB_WORKSPACE  OpenMS Viewer checkout root
#   VIEWER_BUILD      viewer build directory
#   VIEWER_STAGE      viewer install/staging prefix
#   OPENMS_INSTALL    OpenMS install prefix (provides sqlite3.lib)
#   OPENMS_CONTRIB    extracted OpenMS contrib prefix
#   QT_ROOT_DIR       Qt prefix
# shellcheck disable=SC2154  # inputs above are provided by the workflow environment
set -eo pipefail

test -f "$OPENMS_INSTALL/lib/cmake/OpenMS/OpenMSConfig.cmake"
cmake -S "$GITHUB_WORKSPACE" -B "$VIEWER_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$VIEWER_STAGE" \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR/lib/cmake;$QT_ROOT_DIR;$OPENMS_CONTRIB;$OPENMS_INSTALL" \
  -DOPENMS_CONTRIB_LIBS="$OPENMS_CONTRIB" \
  -DOpenMS_DIR="$OPENMS_INSTALL/lib/cmake/OpenMS" \
  -DOPENMS_VIEWER_SQLITE3_LIBRARY="$OPENMS_INSTALL/lib/sqlite3.lib" \
  -DOPENMS_VIEWER_BUILD_TESTS=OFF \
  -DOPENMS_VIEWER_BUILD_THERMO_SMOKE=ON \
  -DOPENMS_VIEWER_PORTABLE=ON
cmake --build "$VIEWER_BUILD" --target openms-viewer openms-viewer-thermo-smoke
cmake --install "$VIEWER_BUILD"

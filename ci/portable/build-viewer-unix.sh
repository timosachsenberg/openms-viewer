#!/usr/bin/env bash
# Configure, build, and install OpenMS Viewer in portable mode against the
# installed OpenMS core. Builds the viewer and the Thermo RAW smoke probe.
# Invoked by the "Build and install OpenMS Viewer" stage.
#
# Required environment:
#   RUNNER_OS         "Linux" or "macOS"
#   GITHUB_WORKSPACE  OpenMS Viewer checkout root
#   VIEWER_BUILD      viewer build directory
#   VIEWER_STAGE      viewer install/staging prefix
#   OPENMS_INSTALL    OpenMS install prefix
#   OPENMS_CONTRIB    extracted OpenMS contrib prefix
#   QT_ROOT_DIR       Qt prefix
# macOS only:
#   HOMEBREW_PREFIX   output of `brew --prefix`
#   QT_EXTRA_PREFIXES ";"-joined qtsvg/qtimageformats prefixes
# shellcheck disable=SC2154  # inputs above are provided by the workflow environment
set -eo pipefail

test -f "$OPENMS_INSTALL/lib/cmake/OpenMS/OpenMSConfig.cmake"
cmake_prefix="$QT_ROOT_DIR/lib/cmake;$QT_ROOT_DIR;$OPENMS_CONTRIB;$OPENMS_INSTALL"
viewer_args=()
if [[ "$RUNNER_OS" == "macOS" ]]; then
  openmp_root=$(brew --prefix libomp)
  cmake_prefix="$cmake_prefix;$QT_EXTRA_PREFIXES;$HOMEBREW_PREFIX;$openmp_root"
  viewer_args+=("-DOpenMP_ROOT=$openmp_root")
  viewer_args+=("-DCMAKE_INSTALL_RPATH=@executable_path/../Frameworks")
  # Homebrew ships Qt as separate kegs (qtbase, qtsvg, qtimageformats).
  # find_package(Qt6 COMPONENTS Svg) only discovers modules outside
  # qtbase's prefix via QT_ADDITIONAL_PACKAGES_PREFIX_PATH, not plain
  # CMAKE_PREFIX_PATH. (Linux's single-prefix aqt install needs no such hint.)
  viewer_args+=("-DQT_ADDITIONAL_PACKAGES_PREFIX_PATH=$QT_EXTRA_PREFIXES")
else
  viewer_args+=("-DCMAKE_INSTALL_RPATH=\$ORIGIN/../lib")
fi

cmake -S "$GITHUB_WORKSPACE" -B "$VIEWER_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$VIEWER_STAGE" \
  -DCMAKE_PREFIX_PATH="$cmake_prefix" \
  -DOPENMS_CONTRIB_LIBS="$OPENMS_CONTRIB" \
  -DOpenMS_DIR="$OPENMS_INSTALL/lib/cmake/OpenMS" \
  -DOPENMS_VIEWER_BUILD_TESTS=OFF \
  -DOPENMS_VIEWER_BUILD_THERMO_SMOKE=ON \
  -DOPENMS_VIEWER_PORTABLE=ON \
  "${viewer_args[@]}"
if [[ "$RUNNER_OS" == "macOS" ]]; then
  # OpenMS's exported target propagates the contrib libcurl transitive
  # deps (-lssh2, -lidn2, …) as interface link libraries, so linking the
  # viewer and the Thermo smoke probe needs the same Homebrew lib path
  # used for the OpenMS core build. The libraries were installed there.
  brew_prefix=$(brew --prefix)
  export LIBRARY_PATH="$brew_prefix/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
fi
cmake --build "$VIEWER_BUILD" --target openms-viewer openms-viewer-thermo-smoke
cmake --install "$VIEWER_BUILD"

#!/usr/bin/env bash
# Configure the OpenMS core libraries for a portable-package build
# (core only, WITH_GUI=OFF). Invoked by the "Configure OpenMS core" stage of
# the unix-portable workflow; runnable locally with the same environment.
#
# Required environment:
#   RUNNER_OS         "Linux" or "macOS"
#   OPENMS_SOURCE     checked-out OpenMS source tree
#   OPENMS_BUILD      OpenMS build directory (created by cmake)
#   OPENMS_INSTALL    OpenMS install prefix
#   OPENMS_CONTRIB    extracted OpenMS contrib prefix
#   QT_ROOT_DIR       Qt prefix (aqt on Linux, brew keg on macOS)
# macOS only:
#   HOMEBREW_PREFIX   output of `brew --prefix`
#   QT_EXTRA_PREFIXES ";"-joined qtsvg/qtimageformats prefixes
# shellcheck disable=SC2154  # inputs above are provided by the workflow environment
set -eo pipefail

cmake_prefix="$QT_ROOT_DIR/lib/cmake;$QT_ROOT_DIR;$OPENMS_CONTRIB"
openmp_args=()
if [[ "$RUNNER_OS" == "macOS" ]]; then
  openmp_root=$(brew --prefix libomp)
  cmake_prefix="$cmake_prefix;$QT_EXTRA_PREFIXES;$HOMEBREW_PREFIX;$openmp_root"
  openmp_args+=("-DOpenMP_ROOT=$openmp_root")
fi

cmake -S "$OPENMS_SOURCE" -B "$OPENMS_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$OPENMS_INSTALL" \
  -DINSTALL_CMAKE_DIR="$OPENMS_INSTALL/lib/cmake/OpenMS" \
  -DCMAKE_PREFIX_PATH="$cmake_prefix" \
  -DOPENMS_CONTRIB_LIBS="$OPENMS_CONTRIB" \
  -DBUILD_TOPP_TOOLS=OFF \
  -DINSTALL_OPENMS_EXAMPLES=OFF \
  -DPYOPENMS=OFF \
  -DWITH_GUI=OFF \
  -DWITH_THERMO_RAW=ON \
  -DWITH_OPENTIMS=ON \
  -DWITH_WNETALIGN=OFF \
  -DWITH_HDF5=OFF \
  -DWITH_ONNX=OFF \
  -DMT_ENABLE_OPENMP=ON \
  -DARROW_USE_STATIC=ON \
  -DUSE_EXTERNAL_JSON=OFF \
  -DUSE_EXTERNAL_SQLITECPP=OFF \
  -DUSE_EXTERNAL_SIMDE=OFF \
  -DENABLE_DOCS=OFF \
  -DENABLE_CLASS_TESTING=OFF \
  -DENABLE_TOPP_TESTING=OFF \
  -DENABLE_PIPELINE_TESTING=OFF \
  -DENABLE_STYLE_TESTING=OFF \
  -DENABLE_THERMO_RAW_TESTS=OFF \
  -DENABLE_UPDATE_CHECK=OFF \
  -DGIT_TRACKING=OFF \
  -DHAS_XSERVER=OFF \
  -DPACKAGE_TYPE=none \
  "${openmp_args[@]}"

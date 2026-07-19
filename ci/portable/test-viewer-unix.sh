#!/usr/bin/env bash
# Build and run the OpenMS Viewer Qt Test suites against the installed OpenMS
# core. Reuses the OpenMS build (the expensive part); a failure here fails the
# job before the slower packaging steps. Linux only — the suites run headless
# via the offscreen QPA plugin. Invoked by the "Build and run unit tests" stage.
#
# Required environment:
#   GITHUB_WORKSPACE  OpenMS Viewer checkout root
#   QT_ROOT_DIR       Qt prefix
#   OPENMS_CONTRIB    extracted OpenMS contrib prefix
#   OPENMS_INSTALL    OpenMS install prefix
# shellcheck disable=SC2154  # inputs above are provided by the workflow environment
set -eo pipefail

test_build="$GITHUB_WORKSPACE/_build/viewer-tests"
cmake -S "$GITHUB_WORKSPACE" -B "$test_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR/lib/cmake;$QT_ROOT_DIR;$OPENMS_CONTRIB;$OPENMS_INSTALL" \
  -DOPENMS_CONTRIB_LIBS="$OPENMS_CONTRIB" \
  -DOpenMS_DIR="$OPENMS_INSTALL/lib/cmake/OpenMS" \
  -DOPENMS_VIEWER_BUILD_TESTS=ON \
  -DOPENMS_VIEWER_BUILD_THERMO_SMOKE=OFF
cmake --build "$test_build" --target openms_viewer_tests
# The unpackaged test binary needs libOpenMS and its contrib deps
# (e.g. libzip) on the runtime linker path; the portable package
# bundles these instead.
LD_LIBRARY_PATH="$OPENMS_INSTALL/lib:$OPENMS_CONTRIB/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
OPENMS_DATA_PATH="$OPENMS_INSTALL/share/OpenMS" \
QT_QPA_PLATFORM=offscreen \
  ctest --test-dir "$test_build" --output-on-failure

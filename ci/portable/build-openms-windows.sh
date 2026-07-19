#!/usr/bin/env bash
# Build and install the configured OpenMS core libraries. Invoked by the
# "Build and install OpenMS core" stage of the windows-portable workflow.
#
# Required environment:
#   OPENMS_BUILD    OpenMS build directory (already configured)
#   OPENMS_INSTALL  OpenMS install prefix
# shellcheck disable=SC2154  # inputs above are provided by the workflow environment
set -eo pipefail

cmake --build "$OPENMS_BUILD" --target OpenMS
cmake --install "$OPENMS_BUILD" \
  --prefix "$OPENMS_INSTALL" \
  --config Release \
  --strip

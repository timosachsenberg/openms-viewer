#!/usr/bin/env bash
# Build and install the configured OpenMS core libraries. Invoked by the
# "Build and install OpenMS core" stage of the unix-portable workflow.
#
# Required environment:
#   RUNNER_OS       "Linux" or "macOS"
#   OPENMS_BUILD    OpenMS build directory (already configured)
#   OPENMS_INSTALL  OpenMS install prefix
# shellcheck disable=SC2154  # inputs above are provided by the workflow environment
set -eo pipefail

if [[ "$RUNNER_OS" == "macOS" ]]; then
  # The contrib's static libcurl pulls in several optional transitive
  # libraries (ssh2, idn2, nghttp2, brotli, zstd, rtmp) that macOS finds
  # on no default linker path (Linux resolves them from the system).
  # Homebrew's curl formula depends on all of them, so installing it
  # stages the whole set under $(brew --prefix)/lib; expose that at link
  # time (clang honours LIBRARY_PATH for -l lookups).
  brew install curl
  brew_prefix=$(brew --prefix)
  export LIBRARY_PATH="$brew_prefix/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
fi
cmake --build "$OPENMS_BUILD" --target OpenMS
cmake --install "$OPENMS_BUILD" \
  --prefix "$OPENMS_INSTALL" \
  --config Release \
  --strip

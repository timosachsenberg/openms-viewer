#!/usr/bin/env bash
# Stage the .NET 8 runtime and hostfxr app-locally next to the viewer so the
# OpenMS Thermo bridge resolves them without a system .NET install. Publishes
# DOTNET_RUNTIME_VERSION / DOTNET_FXR_VERSION for later stages via GITHUB_ENV.
# Invoked by the "Bundle .NET 8 runtime" stage.
#
# Required environment:
#   RUNNER_OS     "Linux" or "macOS"
#   VIEWER_STAGE  viewer staging prefix
#   GITHUB_ENV    GitHub Actions environment file (for cross-step outputs)
# shellcheck disable=SC2154  # inputs above are provided by the workflow environment
set -eo pipefail

runtime_line=$(dotnet --list-runtimes | \
  awk '$1 == "Microsoft.NETCore.App" && $2 ~ /^8\./ { line=$0 } END { print line }')
if [[ -z "$runtime_line" ]]; then
  echo "The .NET 8 runtime installed with the SDK was not found." >&2
  exit 1
fi
runtime_version=$(awk '{ print $2 }' <<< "$runtime_line")
runtime_parent=$(sed -E 's/.*\[([^]]+)\]$/\1/' <<< "$runtime_line")
source_dotnet_root=$(cd "$runtime_parent/../.." && pwd -P)
runtime_source="$runtime_parent/$runtime_version"

fxr_version=$(dotnet --info | \
  awk '/^Host:/{ in_host=1; next } in_host && $1 == "Version:" { print $2; exit }')
fxr_source="$source_dotnet_root/host/fxr/$fxr_version"
test -d "$runtime_source"
test -d "$fxr_source"

if [[ "$RUNNER_OS" == "macOS" ]]; then
  # Keep the runtime outside the .app until BundleUtilities has
  # rewritten the viewer dependency graph. Otherwise it treats every
  # executable bit in the .NET runtime as another app executable.
  target_dotnet_root="$VIEWER_STAGE/.dotnet-runtime"
else
  target_dotnet_root="$VIEWER_STAGE/dotnet"
fi
mkdir -p \
  "$target_dotnet_root/shared/Microsoft.NETCore.App" \
  "$target_dotnet_root/host/fxr"
cp -a "$runtime_source" \
  "$target_dotnet_root/shared/Microsoft.NETCore.App/$runtime_version"
cp -a "$fxr_source" "$target_dotnet_root/host/fxr/$fxr_version"
cp "$source_dotnet_root/dotnet" "$target_dotnet_root/dotnet"
chmod +x "$target_dotnet_root/dotnet"
for notice in LICENSE.txt ThirdPartyNotices.txt; do
  if [[ -f "$source_dotnet_root/$notice" ]]; then
    cp "$source_dotnet_root/$notice" "$target_dotnet_root/$notice"
  fi
done

DOTNET_ROOT="$target_dotnet_root" "$target_dotnet_root/dotnet" --list-runtimes
echo "DOTNET_RUNTIME_VERSION=$runtime_version" >> "$GITHUB_ENV"
echo "DOTNET_FXR_VERSION=$fxr_version" >> "$GITHUB_ENV"

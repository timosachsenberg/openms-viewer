#!/usr/bin/env bash
# Audit the macOS dependency closure: prove the app bundle is self-contained
# instead of quietly relying on the runner's Homebrew/build libraries. Every
# Mach-O load command must point into the bundle
# (@rpath/@executable_path/@loader_path) or at the macOS system baseline
# (/usr/lib, /System); an absolute path anywhere else (Homebrew, the
# OpenMS/Qt/contrib build trees) is a dependency macdeployqt failed to relocate
# and would be missing for a user. Invoked by the "Audit macOS dependency
# closure" stage. macOS only.
#
# Required environment:
#   VIEWER_STAGE  viewer staging prefix
# shellcheck disable=SC2154  # inputs above are provided by the workflow environment
set -eo pipefail

app="$VIEWER_STAGE/OpenMS Viewer.app"
leaks=()
checked=0
while IFS= read -r macho; do
  file "$macho" | grep -q 'Mach-O' || continue
  checked=$((checked + 1))
  while IFS= read -r line; do
    [[ "$line" == *"(compatibility version"* ]] || continue
    path=$(sed -E 's/^[[:space:]]+(.*) \(compatibility version.*/\1/' <<<"$line")
    case "$path" in
      @rpath/*|@executable_path/*|@loader_path/*) continue ;;
      /usr/lib/*|/System/*) continue ;;
      "$app"/*) continue ;;
      *) leaks+=("$path  [${macho#"$app"/}]") ;;
    esac
  done < <(otool -L "$macho" | tail -n +2)
done < <(find "$app/Contents/MacOS" "$app/Contents/Frameworks" "$app/Contents/PlugIns" -type f)

echo "Audited $checked Mach-O objects in the app bundle."
if (( ${#leaks[@]} > 0 )); then
  {
    echo ""
    echo "These Mach-O dependencies resolve outside the bundle and the macOS"
    echo "system baseline (/usr/lib, /System). They would be missing on a"
    echo "machine without the runner's Homebrew/build libraries; macdeployqt"
    echo "should have relocated them into the bundle:"
    printf '  %s\n' "${leaks[@]}" | sort -u
  } >&2
  exit 1
fi
echo "All app-bundle dependencies resolve inside the bundle or the macOS baseline."

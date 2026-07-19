#!/usr/bin/env bash
# Audit the Linux dependency closure: every dynamic dependency of every staged
# ELF must resolve inside the bundle or to a documented system-baseline library,
# never only because this runner happens to provide it. Invoked by the "Audit
# Linux dependency closure" stage. Linux only.
#
# A portable package must satisfy every dynamic dependency from inside the
# bundle or from the small set of libraries any Linux desktop provides itself
# (glibc/loader plus the OpenGL/X11/Wayland/xkb stack, which must match the
# user's drivers and session -- see README-LINUX.txt). The verify step's ldd
# "not found" scan only proves each dependency resolves *on this runner*, which
# has Qt, OpenMS, contrib, ICU 73, and apt packaging libraries installed. A
# library missing from the bundle but present on the runner passes that scan yet
# breaks on a clean user machine. This audit closes the gap. (The Windows verify
# step does the analogous thing by restricting PATH so runner installs cannot
# hide a missing DLL.)
#
# Required environment:
#   VIEWER_STAGE      viewer staging prefix
#   OPENMS_INSTALL OPENMS_BUILD OPENMS_SOURCE OPENMS_CONTRIB OPENMS_CONTRIB_SOURCE
#   QT_ROOT_DIR GITHUB_WORKSPACE RUNNER_TEMP    build-only directories to reject
# shellcheck disable=SC2154  # inputs above are provided by the workflow environment
set -eo pipefail

stage="$VIEWER_STAGE"

# Libraries the target system is expected to provide, never bundled.
# Tracks the linuxdeployqt / AppImage exclude list: the glibc/loader ABI,
# the compiler runtime, and the graphics + display-server stack.
system_baseline='^(linux-vdso|ld-linux.*|libc|libm|libdl|libpthread|librt|libresolv|libutil|libanl|libnsl|libnss_.*|libBrokenLocale|libmvec|libthread_db|libcrypt|libgcc_s|libstdc\+\+|libGL|libGLX|libGLdispatch|libOpenGL|libEGL|libGLESv2|libglapi|libgbm|libdrm|libX11|libX11-xcb|libxcb|libxcb-.*|libXext|libXau|libXdmcp|libXrender|libXrandr|libXfixes|libXcursor|libXinerama|libXi|libXcomposite|libXdamage|libXtst|libXss|libXxf86vm|libxshmfence|libSM|libICE|libwayland-.*|libxkbcommon|libxkbcommon-x11)\.so.*$'

# Directories that exist only on the build runner; anything resolving
# under them is absent on a user machine regardless of the library.
runner_only_dirs=(
  "$OPENMS_INSTALL" "$OPENMS_BUILD" "$OPENMS_SOURCE"
  "$OPENMS_CONTRIB" "$OPENMS_CONTRIB_SOURCE"
  "$QT_ROOT_DIR"
  "$GITHUB_WORKSPACE/_deps" "$GITHUB_WORKSPACE/_build" "$GITHUB_WORKSPACE/_install"
  "$RUNNER_TEMP" "/usr/local/lib"
)

leaks=()
bundled=0
declare -A host_provided=()

while IFS= read -r binary; do
  file "$binary" | grep -q 'ELF' || continue
  if ! ldd_output=$(ldd "$binary" 2>&1); then
    grep -q 'not a dynamic executable' <<<"$ldd_output" && continue
    echo "ldd failed for $binary:" >&2
    echo "$ldd_output" >&2
    exit 1
  fi
  while IFS= read -r line; do
    if grep -q 'not found' <<<"$line"; then
      soname=$(awk '{print $1}' <<<"$line")
      leaks+=("$soname => <unresolved>  [${binary#"$stage"/}]")
      continue
    fi
    if [[ "$line" == *"=>"* ]]; then
      soname=$(sed -E 's/^[[:space:]]*([^ ]+) =>.*/\1/' <<<"$line")
      path=$(sed -E 's/.*=> (.*) \(0x[0-9a-f]+\)$/\1/' <<<"$line")
    elif [[ "$line" =~ \(0x[0-9a-f]+\)$ ]]; then
      # The loader or vdso, listed as "name (0xADDR)" with no "=>".
      path=$(sed -E 's/^[[:space:]]*([^ ]+) \(0x[0-9a-f]+\)$/\1/' <<<"$line")
      soname=$(basename "$path")
    else
      # An ldd status line that is not a dependency edge, e.g. the
      # "statically linked" printed for a library with no NEEDED entries
      # (ICU data, libX11-xcb, ...). Nothing to resolve.
      continue
    fi
    [[ -z "$path" || "$path" == linux-vdso.so* ]] && continue

    case "$path" in
      "$stage"/*) bundled=$((bundled + 1)); continue ;;
    esac

    runner_only=0
    for dir in "${runner_only_dirs[@]}"; do
      if [[ -n "$dir" && "$path" == "$dir"/* ]]; then runner_only=1; break; fi
    done
    if (( runner_only )); then
      leaks+=("$soname => $path  [${binary#"$stage"/}]")
      continue
    fi

    if [[ "$soname" =~ $system_baseline ]]; then
      host_provided["$soname"]=1
    else
      leaks+=("$soname => $path  [${binary#"$stage"/}]")
    fi
  done <<<"$ldd_output"
done < <(find "$stage/bin" "$stage/lib" -type f)

echo "Dependency edges resolved inside the package: $bundled"
if (( ${#host_provided[@]} > 0 )); then
  echo "System-baseline libraries expected from the target host:"
  printf '  %s\n' "${!host_provided[@]}" | sort
fi

if (( ${#leaks[@]} > 0 )); then
  {
    echo ""
    echo "These dependencies are NOT included in the portable package; they"
    echo "resolved only because this runner provides them and would be missing"
    echo "on a clean user machine. Bundle each one (add its source directory to"
    echo "SEARCH_DIRECTORIES in the Deploy step) or, if it is a core system"
    echo "library present on every target, add it to system_baseline above:"
    printf '  %s\n' "${leaks[@]}" | sort -u
  } >&2
  exit 1
fi
echo "All non-baseline dependencies are bundled; the package is self-contained."

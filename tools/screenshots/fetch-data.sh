#!/usr/bin/env bash
# Fetch the real archive.openms.de datasets used to regenerate the docs
# screenshots into a local cache (skipped if already present, resumable).
#
# Usage:
#   tools/screenshots/fetch-data.sh [tier ...]      # default: small
#   tools/screenshots/fetch-data.sh small large     # add the ~1.5GB tier
#   tools/screenshots/fetch-data.sh all             # small+large+xlarge (~4.5GB)
#
# Cache dir: $SCREENSHOT_CACHE or <repo>/.screenshot-cache (gitignored).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
manifest="$here/manifest.tsv"
cache="${SCREENSHOT_CACHE:-$repo/.screenshot-cache}"
mkdir -p "$cache"

tiers=("${@:-small}")
[[ "${tiers[*]}" == "all" ]] && tiers=(small large xlarge)
want() { for t in "${tiers[@]}"; do [[ "$t" == "$1" ]] && return 0; done; return 1; }

curlopts=(--fail --location --continue-at - --retry 3 --retry-delay 5 -sS)

remote_size() { curl -sI --fail -L "$1" 2>/dev/null | tr -d '\r' \
  | awk 'tolower($1)=="content-length:"{n=$2} END{print n+0}'; }

fetch_file() { # url dest
  local url="$1" dest="$2" remote local
  remote=$(remote_size "$url")
  local=$(stat -c%s "$dest" 2>/dev/null || echo 0)
  # Only "have" it when the local size matches the server's; a shorter file is a
  # truncated/interrupted download and must be resumed, not treated as complete.
  if [[ "$remote" -gt 0 && "$local" -eq "$remote" ]]; then
    echo "  have $(basename "$dest")"; return
  fi
  echo "  get  $(basename "$dest") (${local}/${remote} bytes)"
  curl "${curlopts[@]}" -o "$dest" "$url"
}

fetch_dir() { # base-url destdir  (Apache directory index)
  local url="$1" destdir="$2" f
  mkdir -p "$destdir"
  # pull every non-navigation file link listed in the index (quote each filename)
  local files=()
  while IFS= read -r f; do [[ -n "$f" ]] && files+=("$f"); done < <(
    curl -sS --fail "$url" | grep -oE 'href="[^"?/]+"' | sed -E 's/href="([^"]+)"/\1/' \
      | grep -vE '^\.\.$' || true)
  for f in "${files[@]}"; do fetch_file "$url$f" "$destdir/$f"; done
}

while IFS=$'\t' read -r id tier category dest url; do
  [[ "$id" =~ ^#|^$ ]] && continue
  want "$tier" || continue
  echo "[$id] ($tier / $category)"
  target="$cache/$dest"
  if [[ "$url" == */ ]]; then
    fetch_dir "$url" "$target"
  else
    fetch_file "$url" "$target"
    # The .d.zip extracts to a vendor-named directory, so key idempotency off a
    # marker rather than guessing the extracted directory name.
    if [[ "$dest" == *.zip && ! -f "$target.extracted" ]]; then
      echo "  unzip $(basename "$dest")"
      ( cd "$cache" && unzip -q -o "$dest" ) && : > "$target.extracted"
    fi
  fi
done < "$manifest"

echo "Cache ready at: $cache"
du -sh "$cache" 2>/dev/null | awk '{print "Total: " $1}'

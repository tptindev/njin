#!/usr/bin/env bash
# Reads the njin version from src/engine/api/njin_version.h and checks it
# against the newest numbered section of CHANGELOG.md.
#
#   version.sh          print the version, or fail if the two disagree
#   version.sh notes    print the body of that version's CHANGELOG section
#
# ROOT is the checkout to read (default: the repository this script is in).
set -euo pipefail

root="${ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
header="$root/src/engine/api/njin_version.h"
changelog="$root/CHANGELOG.md"

part() { sed -n "s/^#define NJIN_VERSION_$1 \([0-9][0-9]*\).*/\1/p" "$header"; }
version="$(part MAJOR).$(part MINOR).$(part PATCH)"

case "$version" in
  [0-9]*.[0-9]*.[0-9]*) ;;
  *) echo "::error::could not read the version from $header (got '$version')" >&2; exit 1 ;;
esac

# The newest numbered section. A "## Unreleased" heading above it is skipped.
top="$(grep -m1 -E '^## [0-9]+\.[0-9]+\.[0-9]+' "$changelog" | sed 's/^## //' | awk '{print $1}')"
if [ "$top" != "$version" ]; then
  echo "::error::njin_version.h says $version but the newest CHANGELOG.md section is '${top:-none}'. Release edits both." >&2
  exit 1
fi

if [ "${1:-}" = "notes" ]; then
  # From the "## X.Y.Z" line to the next "## " line, without the heading itself.
  awk -v v="$version" '
    $0 ~ "^## " v "([ ]|$)" { on = 1; next }
    on && /^## / { exit }
    on { print }
  ' "$changelog" | sed '/./,$!d' | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}'
else
  echo "$version"
fi

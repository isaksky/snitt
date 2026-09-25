#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 2 || ( "$1" != --write && "$1" != --check ) ]]; then
    printf 'usage: %s (--write|--check) VERSION [RELEASE_DIRECTORY] [OWNER/REPOSITORY] [--private]\n' "${0##*/}" >&2
    exit 2
fi
script_directory="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
mode="${1#--}"
shift
exec python3 "$script_directory/scoop_manifest.py" update "$mode" "$@"

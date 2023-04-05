#!/bin/bash

set -e

usage() {
    cat <<EOF
usage: $0 pdf...
Overlays pdfs in order (first is top, last is bottom).
EOF
}

if [[ $# -eq 0 ]]; then
    usage >&2
    exit 1
fi

cur="$1"
shift 1

while [[ $# -gt 0 ]]; do
    next=`mktemp`
    trap "rm -rf ${next}" EXIT
    pdftk "$cur" background "$1" output "$next"
    cur="$next"
    shift 1
done

cat "$cur"

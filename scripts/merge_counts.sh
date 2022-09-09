#!/bin/bash

set -e

usage() {
    cat <<EOF
usage: $0 [files...]
EOF
}

files=("$@")
if [[ $# -eq 0 ]]; then
    files=(/dev/stdin)
fi

cat "${files[@]}" | awk '
{ map[$2] += $1 }
END {
  for (key in map)
    print map[key], key
}
'

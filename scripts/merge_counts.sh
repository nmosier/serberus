#!/bin/bash

set -e

usage() {
    cat <<EOF
usage: $0 [-h] -t col [--] [files...]
EOF
}

while getopts "ht:" optc; do
    case $optc in
	h)
	    usage
	    exit
	    ;;
	t)
	    col=$((OPTARG-1))
	    ;;
	*)
	    usage >&2
	    exit 1
	    ;;
    esac
done

files=("$@")
if [[ $# -eq 0 ]]; then
    files=(/dev/stdin)
fi

if [[ -z "${col+x}" ]]; then
    usage >&2
    exit 1
fi

input=$(mktemp)
trap "rm -rf ${input}"
cat "${files[@]}" > ${input}

declare -A arr


cat "${files[@]}" | awk -vcol=${col} '
{ map[$2] += $1 }
END {
  for (key in map)
    print map[key], key
}
'

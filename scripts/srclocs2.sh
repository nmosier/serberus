#!/bin/bash

usage() {
    cat <<EOF
usage: $0 -t col binary
EOF
}

while getopts "ht:" optc; do
    case $optc in
	h)
	    usage
	    exit
	    ;;
	t)
	    col=$((OPTARG))
	    ;;
	*)
	    usage >&2
	    exit 1;
	    ;;
    esac
done
shift $((OPTIND-1))

if [[ $# -ne 1 || -z "${col+x}" ]]; then
    usage >&2
    exit 1
fi

binary="$1"
shift 1

file=$(mktemp)
trap "rm -rf ${file}" EXIT
cat > ${file}

srcs=$(mktemp)
trap "rm -rf ${srcs}" EXIT
cut -d' ' -f${col} < ${file} | addr2line -e "${binary}" > ${srcs}

head ${file} >&2
if [[ $(wc -l < ${file}) -ne $(wc -l < ${srcs}) ]]; then
    echo 'internal error' >&2
    exit 1
fi

paste -d' ' ${file} ${srcs}

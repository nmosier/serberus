#!/bin/bash

usage() {
    cat <<EOF
usage: $0 file1 file2 ll
EOF
}

if [[ $# -ne 3 ]]; then
    usage >&2
    exit 1
fi

sorted1=$(mktemp)
sorted2=$(mktemp)
trap "rm -rf $sorted1 $sorted2" EXIT

sort -t' ' -k2 "$1" > ${sorted1}
sort -t' ' -k2 "$2" > ${sorted2}

if ! diff <(cut -d' ' -f2 < ${sorted1}) <(cut -d' ' -f2 < ${sorted2}); then
    echo 'files differ in keys' >&2
    exit 1
fi

do_paste() {
    paste -d' ' <(cut -d' ' -f2 ${sorted1}) <(cut -d' ' -f2 ${sorted2})
}


diffs=$(mktemp)
trap "rm -rf ${diffs}" EXIT
paste -d' ' <(cut -d' ' -f1 ${sorted1}) <(cut -d' ' -f1 ${sorted2}) <(cut -d' ' -f2 ${sorted1}) | grep -e '^pub sec' -e '^sec pub' > ${diffs}

if [[ -s ${diffs} ]]; then
    cat ${diffs}
    cat $3
    exit 1
fi

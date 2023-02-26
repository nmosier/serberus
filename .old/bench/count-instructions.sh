#!/bin/bash

usage() {
    cat <<EOF
usage: $0 [asm]
EOF
}

case $# in
    0)
	f=/dev/stdin
	;;
    1)
	f="$1"
	;;
    *)
	usage >&2
	exit 1
esac

grep -v 'assembler' < "$f" | wc -l

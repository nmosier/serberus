#!/bin/bash

usage() {
    cat <<EOF
usage: $0 <asm>
EOF
}

if [[ $# -ne 1 ]]; then
    usage >&2
    exit 1
fi

lfences=$(grep 'lfence' "$1" | wc -l)
calls=$(grep 'call' "$1" | wc -l)
functions=$(grep 'assembler code for function' "$1" | wc -l)

echo $((lfences + calls + functions * 2))

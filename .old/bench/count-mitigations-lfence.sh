#!/bin/bash

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <asm>" >&2
    exit 1
fi

# lfences
lfences=$(grep 'lfence' "$1" | wc -l)

# retpoline
retpolines=$(grep -e 'call.*<__llvm_retpoline' -e 'call.*@plt' "$1" | wc -l)

echo $((lfences + retpolines))



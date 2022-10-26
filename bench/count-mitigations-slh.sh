#!/bin/bash

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <asm> <tab>" >&2
    exit 1
fi

asm="$1"
tab="$2"

# slhs
slhs=$(grep '^Dump of assembler code for function .*:$' "$asm" | awk '{print $NF}' | grep -o '^[^:]*' | sort | join --nocheck-order - <(sort $tab) | sort | uniq | awk '{x+=$2} END{print x}')


# retpoline
retpolines=$(grep -e 'call.*<__llvm_retpoline' -e 'call.*@plt' "$asm" | wc -l)

echo $((slhs + retpolines))

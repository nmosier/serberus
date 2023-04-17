#!/bin/bash

set -e

dir="$(dirname "${BASH_SOURCE[0]}")"

usage() {
    cat <<EOF
usage: $0 -n count
EOF
}

while getopts "hn:" optc; do
    case $optc in
	h)
	    usage
	    exit
	    ;;
	n)
	    count="$OPTARG"
	    ;;
	*)
	    usage >&2
	    exit 1
    esac
done

if [[ -z "${count}" ]]; then
    usage >&2
    exit 1
fi

pdfs=()

plot() {
    out=$1
    shift 1
    python3 ${dir}/plot2.py -o $out $@
    pdfs+=($out)
}

for ((i=0; i<count; ++i)); do
    plot tmp${i}.pdf -d bench ${dir}/spec.json --ymax=100 --ymin=-5 -n -i${i}
    # python3 ${dir}/plot2.py -o tmp${i}.pdf -d bench ${dir}/spec.json --ymax=100 --ymin=-5 -n -i${i}
    # pdfs+=(tmp${i}.pdf)
done

plot tmp${i}.pdf -d bench ${dir}/spec.json --ymax=100 --ymin=-5 -i -1

${dir}/overlay.sh ${pdfs[@]} > tmp.pdf

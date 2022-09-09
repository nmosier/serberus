#!/bin/bash

usage() {
    cat <<EOF
usage: $0 [-h] [-c] binary
EOF
}

show_counts=0

while getopts optc "hc"; do
    case $optc in
	h)
	    usage
	    exit
	    ;;
	c)
	    show_counts=1
	    ;;
	*)
	    usage >&2
	    exit 1
	    ;;
    esac
done
shift $((OPTIND-1))

if [[ $# -ne 1 ]]; then
    usage >&2
    exit 1
fi
binary="$1"
shift 1

counts=()
locations=()
while read -r count loc; do
    counts+=(${count})
    locations+=(${loc})
done

get_counts() {
    printf '%s\n' ${counts[@]}
}

get_locations() {
    printf '%s\n' ${locations[@]}
}

get_lldb_input() {
    while read -r loc; do
	echo "source list -a ${loc}"
    done
}

lookups() {
    get_lldb_input | lldb "${binary}" | awk '
BEGIN {
  seen_cmd = 0;	     
} 
{
  if (seen_cmd) {
    print $NF;
    seen_cmd = 0;
  }
}
/source list -a/ {
  seen_cmd = 1;
}
'
}

paste -d' ' <(get_counts) <(get_locations) <(get_locations | lookups 2>/dev/null) | sort -t' ' -k1 -n

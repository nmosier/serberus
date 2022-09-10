#!/bin/bash

usage() {
    cat <<EOF
usage: $0 [-h] -t col binary
EOF
}

col=
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
shift $((OPTIND-1))

if [[ $# -ne 1 || -z "${col+x}" ]]; then
    usage >&2
    exit 1
fi

binary="$1"
shift 1

file=$(mktemp)
trap "rm -rf ${file}" EXIT

# Copy all input to file so we can re-read it
cat > ${file}

get_locations() {
    cut -f${col} -d' ' ${file}
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

srclines=$(mktemp)
trap "rm -rf ${srclines}" EXIT
cut -f${col} -d' ' ${file} | lookups 2>/dev/null >${srclines}

wc -l < ${srclines} >&2
wc -l < ${file} >&2
diff ${file} ${srclines}
if [[ $(wc -l < ${srclines}) -ne $(wc -l < ${file}) ]]; then
    echo 'internal error' >&2
    exit 1
fi

paste -d' ' ${file} ${srclines}

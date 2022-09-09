#!/bin/bash

set -e

usage() {
    cat <<EOF
usage: $0 [-hv] [-o timefile] [-x execwrapper] [--] [make-option...]
Invokes make check and prints out runtime report of all tests.
EOF
}

logfile=/dev/stderr

wrappercmds=("time")
while getopts "hvo:x:" optc; do
    case $optc in
	h)
	    usage; exit
	    ;;
	v)
	    verbose=1
	    ;;
	o)
	    logfile="${OPTARG}"
	    ;;
	x)
	    wrappercmd="$(realpath "${OPTARG}" || echo "${OPTARG}")"
	    wrappercmds+=("${wrappercmd}")
	    ;;
	*)
	    usage >&2
	    exit 1
	    ;;	
    esac
done

shift $((OPTIND-1))

make_opts=("$@")
make_cmd=(make --quiet check TESTS_ENVIRONMENT="$(echo ${wrappercmds[@]})" "${make_opts[@]}")

extract_times() {
    grep '^real[[:space:]]' | gawk '
{
  seconds = gensub(/real[[:space:]]+[[:digit:]]+m([[:digit:].]+)s/, "\\1", "g", $0);
  print seconds;
}
'
}

tmplogfile=$(mktemp)
trap "rm -rf ${tmplogfile}" EXIT

if [[ ${verbose} -ne 0 ]]; then
    echo "${make_cmd[@]}" >&2
fi
exitno=0
if ! "${make_cmd[@]}" 2>>${tmplogfile}; then
    exitno=1
fi
grep -v '^real[[:space:]]' <${tmplogfile} >&2 || true

if [[ ${exitno} -ne 0 ]]; then
    exit ${exitno}
fi

extract_times < ${tmplogfile} | awk 'BEGIN{acc=0}{acc+=$0}END{print acc}' >>${logfile}

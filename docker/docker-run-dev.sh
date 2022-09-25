#!/bin/bash

usage() {
    cat <<EOF
usage: $0 [name]
EOF
}

case $# in
    0)
	name=cloucc-dev
	;;
    1)
	name="$1"
	;;
    *)
	usage >&2
	exit 1
esac

HOST_ROOT="$(dirname $(dirname $(dirname $(realpath ${BASH_SOURCE[0]}))))"
GUEST_ROOT="/cloucc"

docker run -it --privileged --cap-add=SYS_PTRACE  --cap-add=SYS_ADMIN --security-opt=seccomp=unconfined -v "$HOST_ROOT":"$GUEST_ROOT":z "$name"

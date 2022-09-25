#!/bin/bash

usage() {
    cat <<EOF
usage: $0 [name]
EOF
}

case $# in
    0)
	name="cloucc-dev"
	;;
    1)
	name="$1"
	;;
    *)
	usage >&2
	exit 1
	;;
esac

cd "$(dirname ${BASH_SOURCE[0]})"

docker build . -f Dockerfile.dev -t "$name" --build-arg="UID=$(id -u)"

#!/bin/bash

set -eu

docker_dir="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
root="$(dirname "$docker_dir")"

docker run \
       --platform=linux/amd64 \
       -v "$root":/serberus \
       --privileged \
       -v /dev/cpu:/dev/cpu \
       -it \
       serberus

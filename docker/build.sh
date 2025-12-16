#!/bin/bash

set -eu

docker_dir="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
cd "$docker_dir"

docker build -t serberus --platform=linux/amd64 .

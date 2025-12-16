#!/bin/bash

set -eu

root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
cd "$root"

cmake -G Ninja \
      -S . \
      -B build \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DLLSCT_LLVM_DIR="$root/llvm/install" \
      -DLLSCT_REQUIRE_CET=0

cmake --build build --target src/all

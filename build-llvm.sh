#!/bin/bash

set -eu

root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
cd "$root"

cmake -G Ninja \
      -S llvm/llvm \
      -B llvm/build \
      -DCMAKE_INSTALL_PREFIX=llvm/install \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_STANDARD=20 \
      -DLLVM_ENABLE_ASSERTIONS=1 \
      -DLLVM_ENABLE_PROJECTS='clang;lld' \
      -DLLVM_TARGETS_TO_BUILD=X86

cmake --build llvm/build
cmake --install llvm/build

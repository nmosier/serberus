#!/bin/bash

set -x

suffix="docker-dev"
build="build-$suffix"
install="install-$suffix"

cd /cloucc/clouxx-llvm
mkdir -p $build
cd $build
cmake -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DLLVM_ENABLE_PROJECTS="clang" -DLLVM_TARGETS_TO_BUILD="X86" -DLLVM_ENABLE_ASSERTIONS=On -DLLVM_BINUTILS_INCDIR=/usr/include -DCMAKE_INSTALL_PREFIX=../install $install ../llvm
cmake --build .
cmake --install .

cd /cloucc/clouxx-passes
mkdir -p build-docker-dev
cd 

exec /bin/bash

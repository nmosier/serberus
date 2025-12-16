#!/bin/bash

set -eu

./build-llvm.sh
./build-passes.sh
./build-bench.sh

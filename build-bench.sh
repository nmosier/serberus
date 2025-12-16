#!/bin/bash

set -eu

cmake --build build --target raw_compile
cmake --build build --target time_compile clean_bench

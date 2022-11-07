#include <benchmark/benchmark.h>

#include "wrappers.h"

BENCHMARK(libsodium_sha256_bench)->RangeMultiplier(2)->Range(1 << 6, 1 << 16);

BENCHMARK_MAIN();


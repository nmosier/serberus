#include <benchmark/benchmark.h>

extern void libsodium_sha256(benchmark::State& state);

BENCHMARK(libsodium_sha256)->RangeMultiplier(2)->Range(1 << 6, 1 << 16);

BENCHMARK_MAIN();


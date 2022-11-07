#pragma once

#include <benchmark/benchmark.h>

void libsodium_salsa20_bench(benchmark::State& state);
void libsodium_sha256_bench(benchmark::State& state);
void hacl_chacha20_benchmark(benchmark::State& state);
void hacl_poly1305_benchmark(benchmark::State& state);
void hacl_curve25519_bench(benchmark::State& state);

#include <benchmark/benchmark.h>

#include "wrappers.h"

BENCHMARK(libsodium_salsa20_bench);
BENCHMARK(libsodium_sha256_bench)->Arg(64)->Arg(8192);
BENCHMARK(hacl_chacha20_bench)->Arg(8192);
BENCHMARK(hacl_poly1305_bench)->Arg(1024)->Arg(8192);
BENCHMARK(hacl_curve25519_bench);

BENCHMARK_MAIN();

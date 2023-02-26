#include <benchmark/benchmark.h>
#include "shared-main.h"

#define EVAL(x) x

#define BENCHMARK_(x) BENCHMARK(x)

BENCHMARK_(BENCH_NAME)->Arg(BENCH_ARG);

BENCHMARK_MAIN();

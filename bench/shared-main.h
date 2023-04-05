#pragma once

#ifndef BENCH_NAME
# error "BENCH_NAME undefined"
#endif

#ifndef BENCH_ARG
# error "BENCH_ARG undefined"
#endif

extern void BENCH_NAME(benchmark::State& state);

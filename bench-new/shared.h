#pragma once

#include <cstdint>
#include <cstdlib>
#include <vector>

#ifndef BENCH_NAME
# error "BENCH_NAME not defined"
#endif
#ifndef BENCH_ARG
# error "BENCH_ARG not defined"
#endif
#ifndef BENCH_METRIC
# error "BENCH_METRIC not defined"
#endif

#if defined(BENCH_LIBSODIUM)
# include <sodium.h>
#elif defined(BENCH_HACL)
extern "C" {
# include <Hacl_Chacha20.h>
# include <Hacl_Poly1305_32.h>
# include <Hacl_Curve25519_51.h>
}
#else
# error "No library defined"
#endif

#if defined(BENCH_TIME)
# include <benchmark/benchmark.h>
#elif defined(BENCH_MEM) || defined(BENCH_CACHE)
# include "benchmark_memory.h"
#else
# error "No benchmark type specified"
#endif

#define CLOBBER_REGS() asm volatile ("" ::: "r12", "r13", "r14", "r15")
#define SAFE_CALL(call)				\
  do {						\
    CLOBBER_REGS();				\
    (call);					\
    CLOBBER_REGS();				\
  } while (false)

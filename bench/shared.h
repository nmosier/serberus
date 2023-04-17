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
# define BENCH_LIB "libsodium"
# include <sodium.h>
#elif defined(BENCH_HACL)
# define BENCH_LIB "hacl"
extern "C" {
# include <Hacl_Chacha20.h>
# include <Hacl_Poly1305_32.h>
# include <Hacl_Curve25519_51.h>
}
#elif defined(BENCH_OPENSSL)
# define BENCH_LIB "openssl"
# include <openssl/sha.h>
#else
# error "No library defined"
#endif

#if defined(BENCH_TIME)
# include <benchmark/benchmark.h>
#else
// #elif defined(BENCH_MEM) || defined(BENCH_CACHE) || defined(BENCH_STALL) || defined(BENCH_INST)
# include "benchmark_memory.h"
#endif

#define CLOBBER_REGS() asm volatile ("" ::: "r12", "r13", "r14", "r15")
#define SAFE_CALL(call)				\
  do {						\
    CLOBBER_REGS();				\
    (call);					\
    CLOBBER_REGS();				\
  } while (false)

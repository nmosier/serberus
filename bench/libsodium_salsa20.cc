#include <cstdint>
#include <cstdlib>
#include <benchmark/benchmark.h>
#include <sodium.h>

#define CLOBBER_REGS() asm volatile ("" ::: "r12", "r13", "r14", "r15")

void libsodium_salsa20_bench(benchmark::State& state) {
  uint8_t out[crypto_core_salsa20_OUTPUTBYTES];
  uint8_t in[crypto_core_salsa20_INPUTBYTES];
  uint8_t k[crypto_core_salsa20_KEYBYTES];
  uint8_t c[crypto_core_salsa20_CONSTBYTES];
  for (auto& b : in) b = rand();
  for (auto& b : k) b = rand();
  for (auto& b : c) b = rand();
  for (auto _ : state) {
    CLOBBER_REGS();
    crypto_core_salsa20(out, in, k, c);
    CLOBBER_REGS();    
  }
}

BENCHMARK(libsodium_salsa20_bench)->Arg(64);
BENCHMARK_MAIN();

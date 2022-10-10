#include <cstdlib>
#include <cstdint>
#include <vector>
#include <benchmark/benchmark.h>

extern "C" {
#include "Hacl_Poly1305_32.h"
}

#define CLOBBER_REGS() asm volatile ("" ::: "r12", "r13", "r14", "r15")

void hacl_poly1305_benchmark(benchmark::State& state) {
  constexpr size_t TAG_BYTES = 16;
  constexpr size_t KEY_BYTES = 16;
  uint8_t tag[TAG_BYTES];
  uint8_t key[KEY_BYTES];
  for (auto& b : key)
    b = rand();
  std::vector<uint8_t> text(state.range(0));
  for (auto& b : text)
    b = rand();
  for (auto _ : state) {
    CLOBBER_REGS();
    Hacl_Poly1305_32_poly1305_mac(tag, text.size(), text.data(), key);
    CLOBBER_REGS();
  }
}

BENCHMARK(hacl_poly1305_benchmark)->Arg(1024)->Arg(8192);

BENCHMARK_MAIN();

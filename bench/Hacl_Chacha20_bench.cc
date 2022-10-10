#include <cstdint>
#include <cstdlib>
#include <vector>
#include <benchmark/benchmark.h>

extern "C" {
#include "Hacl_Chacha20.h"
}

#define CLOBBER_REGS() asm volatile ("" ::: "r12", "r13", "r14", "r15")

void hacl_chacha20_benchmark(benchmark::State& state) {
  // msg_len, out, msg, key, nonce, 0
  std::vector<uint8_t> msg(state.range(0));
  for (auto& c : msg)
    c = rand();
  std::vector<uint8_t> key = {
    11, 22, 33, 44, 55, 66, 77, 88, 99, 111, 122, 133, 144, 155, 166, 177,
    188, 199, 211, 222, 233, 244, 255, 0, 10, 20, 30, 40, 50, 60, 70, 80,
  };
  std::vector<uint8_t> nonce = {98, 76, 54, 32, 10, 0, 2, 4, 6, 8, 10, 12};
  std::vector<uint8_t> out(msg.size());
  for (auto _ : state) {
    CLOBBER_REGS();
    Hacl_Chacha20_chacha20_encrypt(msg.size(), out.data(), msg.data(),
				   key.data(), nonce.data(), 0);
    CLOBBER_REGS();
  }
}

BENCHMARK(hacl_chacha20_benchmark)->Arg(8192);

BENCHMARK_MAIN();

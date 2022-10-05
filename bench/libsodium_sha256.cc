#include <benchmark/benchmark.h>

#include <sodium.h>

#define CLOBBER_REGS() asm volatile ("" ::: "rbx", "rbp", "r12", "r13", "r14", "r15")

// TODO: Could inline this.
void sha256(const unsigned char *in, size_t in_len) {
  unsigned char h[crypto_hash_BYTES];
  crypto_hash_sha256(h, in, in_len);
}

void sha256_bench(benchmark::State& state) {
  std::vector<unsigned char> in(state.range(0));
  for (auto& byte : in) {
    byte = rand();
  }
  for (auto _ : state) {
    CLOBBER_REGS();
    sha256(in.data(), in.size());
    CLOBBER_REGS();
  }
}

BENCHMARK(sha256_bench)->RangeMultiplier(2)->Range(1 << 6, 1 << 16);

BENCHMARK_MAIN();

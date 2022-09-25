#include <benchmark/benchmark.h>

#include <sodium.h>

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
    sha256(in.data(), in.size());
  }
}

BENCHMARK(sha256_bench)->RangeMultiplier(2)->Range(1 << 6, 1 << 16);

BENCHMARK_MAIN();

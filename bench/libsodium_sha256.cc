#include "shared.h"

void libsodium_sha256(benchmark::State& state) {
  std::vector<unsigned char> in(state.range(0));
  for (auto& byte : in) {
    byte = rand();
  }
  for ([[maybe_unused]] auto _ : state) {
    unsigned char h[crypto_hash_BYTES];
    SAFE_CALL(crypto_hash_sha256(h, in.data(), in.size()));
  }
}

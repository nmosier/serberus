#include "shared.h"

void openssl_sha256(benchmark::State& state) {
  std::vector<unsigned char> in(state.range(0));
  for (auto& byte : in)
    byte = rand();
  for ([[maybe_unused]] auto _ : state) {
    unsigned char md[SHA256_DIGEST_LENGTH];
    SAFE_CALL(SHA256(in.data(), in.size(), md));
  }
}

#include "shared.h"

void libsodium_salsa20(benchmark::State& state) {
  uint8_t out[crypto_core_salsa20_OUTPUTBYTES];
  uint8_t in[crypto_core_salsa20_INPUTBYTES];
  uint8_t k[crypto_core_salsa20_KEYBYTES];
  uint8_t c[crypto_core_salsa20_CONSTBYTES];
  for (auto& b : in) b = rand();
  for (auto& b : k) b = rand();
  for (auto& b : c) b = rand();
  for ([[maybe_unused]] auto _ : state) {
    SAFE_CALL(crypto_core_salsa20(out, in, k, c));
  }
}

#include "shared.h"

void hacl_poly1305(benchmark::State& state) {
  constexpr size_t TAG_BYTES = 16;
  constexpr size_t KEY_BYTES = 16;
  uint8_t tag[TAG_BYTES];
  uint8_t key[KEY_BYTES];
  for (auto& b : key)
    b = rand();
  std::vector<uint8_t> text(state.range(0));
  for (auto& b : text)
    b = rand();
  for ([[maybe_unused]] auto _ : state) {
    SAFE_CALL(Hacl_Poly1305_32_poly1305_mac(tag, text.size(), text.data(), key));
  }
}


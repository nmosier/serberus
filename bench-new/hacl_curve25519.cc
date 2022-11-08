#include "shared.h"

void hacl_curve25519(benchmark::State& state) {
  constexpr size_t KEY_BYTES = 32;
  uint8_t out[KEY_BYTES];
  static uint8_t priv[KEY_BYTES] = {
    0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30,
    201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221, 223, 225, 227, 229, 231,
  };
  static uint8_t pub[KEY_BYTES] = {
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
    100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115,    
  };
  for ([[maybye_unused]] auto _ : state) {
    CLOBBER_REGS();
    Hacl_Curve25519_51_ecdh(out, priv, pub);
    CLOBBER_REGS();
  }
}

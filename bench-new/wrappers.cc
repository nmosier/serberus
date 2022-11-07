#include <cstdint>
#include <cstdlib>
#include <vector>
#include <benchmark/benchmark.h>
#include <sodium.h>

extern "C" {
#include <Hacl_Chacha29.h>
#include <Hacl_Poly1305_32.h>
#include <Hacl_Curve25519_51.h>
}

#define CLOBBER_REGS() asm volatile ("" ::: "r12", "r13", "r14", "r15")
#define SAFE_CALL(call)				\
  CLOBBER_REGS();				\
  (call);					\
  CLOBBER_REGS()


void libsodium_salsa20_bench(benchmark::State& state) {
  uint8_t out[crypto_core_salsa20_OUTPUTBYTES];
  uint8_t in[crypto_core_salsa20_INPUTBYTES];
  uint8_t k[crypto_core_salsa20_KEYBYTES];
  uint8_t c[crypto_core_salsa20_CONSTBYTES];
  for (auto& b : in) b = rand();
  for (auto& b : k) b = rand();
  for (auto& b : c) b = rand();
  for (auto _ : state) {
    SAFE_CALL(crypto_core_salsa20(out, in, k, c));
  }
}

void libsodium_sha256_bench(benchmark::State& state) {
  std::vector<unsigned char> in(state.range(0));
  for (auto& byte : in) {
    byte = rand();
  }
  for (auto _ : state) {
    unsigned char h[crypto_hash_BYTES];
    SAFE_CALL(crypto_hash_sha256(h, in.data(), in.size()));
  }
}

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
    SAFE_CALL(Hacl_Chacha20_chacha20_encrypt(msg.size(), out.data(), msg.data(),
					     key.data(), nonce.data(), 0));
  }
}

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
    SAFE_CALL(Hacl_Poly1305_32_poly1305_mac(tag, text.size(), text.data(), key));
  }
}

void hacl_curve25519_bench(benchmark::State& state) {
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
  for (auto _ : state) {
    CLOBBER_REGS();
    Hacl_Curve25519_51_ecdh(out, priv, pub);
    CLOBBER_REGS();
  }
}

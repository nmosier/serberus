#include "bench.h"

#include "sodium.h"

static unsigned char h[crypto_hash_BYTES];

void hash3_bench(const unsigned char *x, size_t x_len) {
  crypto_hash(h, x, x_len);  
}

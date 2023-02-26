#include "bench.h"

#include <assert.h>
#include <stdlib.h>

#include "sodium.h"

void generichash2_bench(const unsigned char *in, size_t in_len) {
  if (in_len == 0) {
    return;
  }
  
    crypto_generichash_state *st;
    unsigned char            out[crypto_generichash_BYTES_MAX];
    unsigned char            k[crypto_generichash_KEYBYTES_MAX];
    size_t                   h, j;

    assert(crypto_generichash_statebytes() >= sizeof *st);
    st = (crypto_generichash_state *)
        sodium_malloc(crypto_generichash_statebytes());
    for (h = 0; h < crypto_generichash_KEYBYTES_MAX; ++h) {
        k[h] = (unsigned char) h;
    }

        const size_t i = in_len - 1;
        if (crypto_generichash_init(st, k,
                                    1 + i % crypto_generichash_KEYBYTES_MAX,
                                    1 + i % crypto_generichash_BYTES_MAX) != 0) {
	  abort();
        }
        crypto_generichash_update(st, in, i);
        crypto_generichash_update(st, in, i);
        crypto_generichash_update(st, in, i);
        if (crypto_generichash_final(st, out,
                                     1 + i % crypto_generichash_BYTES_MAX) != 0) {
	  abort();
        }
        if (crypto_generichash_final(st, out,
                                     1 + i % crypto_generichash_BYTES_MAX) != -1) {
	  abort();
        }

    assert(crypto_generichash_init(st, k, sizeof k, 0U) == -1);
    assert(crypto_generichash_init(st, k, sizeof k,
                                   crypto_generichash_BYTES_MAX + 1U) == -1);
    assert(crypto_generichash_init(st, k, crypto_generichash_KEYBYTES_MAX + 1U,
                                   sizeof out) == -1);
    assert(crypto_generichash_init(st, k, 0U, sizeof out) == 0);
    assert(crypto_generichash_init(st, k, 1U, sizeof out) == 0);
    assert(crypto_generichash_init(st, NULL, 1U, 0U) == -1);
    assert(crypto_generichash_init(st, NULL, crypto_generichash_KEYBYTES,
                                   1U) == 0);
    assert(crypto_generichash_init(st, NULL, crypto_generichash_KEYBYTES,
                                   0U) == -1);

    sodium_free(st);    
  
}

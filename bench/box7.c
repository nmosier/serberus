#include "bench.h"

#include <string.h>
#include <assert.h>

#include "sodium.h"

static unsigned char alicesk[crypto_box_SECRETKEYBYTES];
static unsigned char alicepk[crypto_box_PUBLICKEYBYTES];
static unsigned char bobsk[crypto_box_SECRETKEYBYTES];
static unsigned char bobpk[crypto_box_PUBLICKEYBYTES];
static unsigned char n[crypto_box_NONCEBYTES];

void box7_bench(size_t mlen) {
    unsigned char *m;
    unsigned char *c;
    unsigned char *m2;
    size_t         i;
    int            ret;

    m  = (unsigned char *) sodium_malloc(mlen + crypto_box_ZEROBYTES);
    c  = (unsigned char *) sodium_malloc(mlen + crypto_box_ZEROBYTES);
    m2 = (unsigned char *) sodium_malloc(mlen + crypto_box_ZEROBYTES);
    memset(m, 0, crypto_box_ZEROBYTES);
    crypto_box_keypair(alicepk, alicesk);
    crypto_box_keypair(bobpk, bobsk);

    randombytes_buf(n, crypto_box_NONCEBYTES);
    randombytes_buf(m + crypto_box_ZEROBYTES, mlen);
    ret = crypto_box(c, m, mlen + crypto_box_ZEROBYTES, n, bobpk, alicesk);
    assert(ret == 0);
    if (crypto_box_open(m2, c, mlen + crypto_box_ZEROBYTES, n, alicepk,
			bobsk) == 0) {
      for (i = 0; i < mlen + crypto_box_ZEROBYTES; ++i) {
	if (m2[i] != m[i]) {
	  printf("bad decryption\n");
	  break;
	}
      }
    } else {
      printf("ciphertext fails verification\n");
    }

    sodium_free(m);
    sodium_free(c);
    sodium_free(m2);
}

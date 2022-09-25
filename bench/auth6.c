#include "bench.h"

#include "sodium.h"

/* "Test Case 2" from RFC 4231 */
static unsigned char key[32] = "Jefe";
static unsigned char a[64];

// NOTE: This is a duplicate of auth2?
void auth6_bench(const unsigned char *c, size_t clen) {
  crypto_auth_hmacsha512(a, c, clen, key);
}

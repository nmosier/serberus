#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sodium.h>

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s n\n", argv[0]);
    return EXIT_FAILURE;
  }

  const int n = atoi(argv[1]);
  unsigned char *buf = malloc(n);
  memset(buf, 0x42, n);

  unsigned char h[crypto_hash_BYTES];
  crypto_hash_sha256(h, buf, n);
}

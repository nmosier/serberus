#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "clou.h"

#define BUFSIZE 100

static void random_buf(char *dst, size_t len) {
  while (len--) {
    *dst++ = rand();
  }
}
  

int main(void) {
  char src1[BUFSIZE];
  char src2[BUFSIZE];
  char dst1[BUFSIZE];
  char dst2[BUFSIZE];
  random_buf(src1, sizeof(src1));
  random_buf(src2, sizeof(src2));
  random_buf(dst1, sizeof(dst1));
  random_buf(dst2, sizeof(dst2));
  
  clou_transform_xor(dst1, src1, src2, BUFSIZE);

  for (size_t i = 0; i < BUFSIZE; ++i) {
    dst2[i] = src1[i] ^ src2[i];
  }

  if (memcmp(dst1, dst2, BUFSIZE) != 0) {
    fprintf(stderr, "dst1 and dst2 differ\n");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

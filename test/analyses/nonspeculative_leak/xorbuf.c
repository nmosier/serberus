#include <stddef.h>

void xorbuf(char *dst, const char *src1, const char *src2, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    dst[i] = src1[i] ^ src2[i];
  }
}

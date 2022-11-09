#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>

#define ARCH_X86_CET_STATUS                0x3001

extern int arch_prctl(int code, long *addr);

__attribute__((constructor)) void require_ibt(void) {
  if (getenv("NOCET")) {
    return;
  }
  long buf[3];
  if (arch_prctl(ARCH_X86_CET_STATUS, buf) < 0) {
    fprintf(stderr, "%s: arch_prctl: %s\n", __func__, strerror(errno));
    _Exit(EXIT_FAILURE);
  }
  if ((buf[0] & 1) == 0) {
    fprintf(stderr, "%s: IBT not enabled\n", __func__);
    _Exit(EXIT_FAILURE);
  }
}

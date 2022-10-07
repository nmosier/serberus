#include <sys/prctl.h>
#include <err.h>
#include <stdlib.h>

__attribute__((constructor)) void enable_ssbd(void) {
  if (prctl(PR_SET_SPECULATION_CTRL, PR_SPEC_STORE_BYPASS, PR_SPEC_FORCE_DISABLE, 0, 0) < 0) {
    err(EXIT_FAILURE, "prctl");
  }
}

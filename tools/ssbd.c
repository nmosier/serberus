#include <sys/prctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

void usage(FILE *f, const char *prog) {
  const char *fmt =
    "usage: %s command args...\n"
    ;
  fprintf(f, fmt, prog);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    usage(stderr, argv[0]);
    return EXIT_FAILURE;
  }

  if (prctl(PR_SET_SPECULATION_CTRL, PR_SPEC_STORE_BYPASS, PR_SPEC_FORCE_DISABLE, 0, 0) < 0) {
    perror("prctl");
    return EXIT_FAILURE;
  }

  execvp(argv[1], &argv[1]);
  perror("execvp");
  return EXIT_FAILURE;
}

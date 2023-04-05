#include <string>

#include <cstring>
#include <cstdint>

#include <err.h>

#define EXECUTE_CUSTOM

#include "shared-pseudo.h"
#include "shared.h"
#include "shared-pin.h"

#define WARMUP_ITERS 1
#define COLLECT_ITERS 10

static void execute_nested(void) {
  benchmark::State state(BENCH_ARG);

  for (unsigned i = 0; i < WARMUP_ITERS; ++i)
    SAFE_CALL(BENCH_NAME(state));

  asm volatile ("int3");
  
  for (unsigned i = 0; i < COLLECT_ITERS; ++i)
    SAFE_CALL(BENCH_NAME(state));

  asm volatile ("int3");
}

static long execute([[maybe_unused]] char *argv[]) {
  char command[1024];
  snprintf(command, sizeof command, "NOCET=1 EXECUTE=1 %s/pin -t %s -- %s", PIN_DIR, PIN_TOOL, argv[0]);
  fprintf(stderr, "Executing command: %s\n", command);

  FILE *f = popen(command, "r");
  char line[1024];
  long n = -1;
  while (fgets(line, sizeof line, f)) {
    printf("%s", line);
    if (sscanf(line, "Count: %ld", &n) == 1)
      break;
  }
  if (ferror(f))
    err(EXIT_FAILURE, "fgets");
  if (n < 0)
    errx(EXIT_FAILURE, "didn't see COUNT in output");

  return n / COLLECT_ITERS;
}

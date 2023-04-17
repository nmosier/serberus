#include <vector>
#include <algorithm>

#include <cstring>
#include <cstdint>
#include <cstdlib>

#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <err.h>

#include "shared-pseudo.h"
#include "shared.h"

#define WARMUP_ITERS 10
#define COLLECT_ITERS 100

static void ioctl_perf(int fd, int request) {
  if (ioctl(fd, request, 0) < 0)
    err(EXIT_FAILURE, "ioctl");
}

static void clear_cache(void) {
  std::vector<uint8_t> x(1024 * 1024 * 32); // 32 MB
  std::fill(x.begin(), x.end(), 0x42);  
}

static uint64_t get_raw(void) {
  if (const char *s = getenv("PERF")) {
    uint64_t raw;
    if (sscanf(s, "%zx", &raw) != 1)
      errx(EXIT_FAILURE, "PERF: bad format: %s", s);
    return raw;
  } else {
    errx(EXIT_FAILURE, "PERF unset");
  }
}

static bool cold = false;
static __attribute__((constructor)) void init(void) {
  if (const char *s = getenv("COLD"))
    if (atoi(s) != 0)
      cold = true;
}

static long execute([[maybe_unused]] char *argv[]) {
  struct perf_event_attr pea;
  memset(&pea, 0, sizeof pea);
  pea.type = PERF_TYPE_RAW;
  pea.config = get_raw();
  pea.size = sizeof pea;
  pea.disabled = 1;

  int fd;
  if ((fd = syscall(SYS_perf_event_open, &pea, 0, -1, -1, 0)) < 0)
    err(EXIT_FAILURE, "perf_event_open");

  benchmark::State state(BENCH_ARG);

  for (unsigned i = 0; i < WARMUP_ITERS; ++i)
    SAFE_CALL(BENCH_NAME(state));

  ioctl_perf(fd, PERF_EVENT_IOC_RESET);
  ioctl_perf(fd, PERF_EVENT_IOC_ENABLE);

  if (cold) {
    clear_cache();
  } else {
    for (unsigned i = 0; i < COLLECT_ITERS; ++i)
      SAFE_CALL(BENCH_NAME(state));
  }

  ioctl_perf(fd, PERF_EVENT_IOC_DISABLE);

  uint64_t count;
  ssize_t bytes;
  if ((bytes = read(fd, &count, sizeof count) != sizeof count)) {
    if (bytes < 0)
      err(EXIT_FAILURE, "read");
    else
      errx(EXIT_FAILURE, "read: unexpected partial read");
  }

  fprintf(stderr, "perf: %lu\n", count);

  return count / COLLECT_ITERS;
}

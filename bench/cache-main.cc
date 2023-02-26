#include <vector>
#include <algorithm>

#include <cstring>
#include <cstdint>

#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <err.h>

#include "shared-pseudo.h"
#include "shared.h"

static void ioctl_perf(int fd, int request) {
  if (ioctl(fd, request, 0) < 0)
    err(EXIT_FAILURE, "ioctl");
}

static void clear_cache(void) {
  std::vector<uint8_t> x(1024 * 1024 * 32); // 32 MB
  std::fill(x.begin(), x.end(), 0x42);  
}

static long execute([[maybe_unused]] char *argv[]) {
  struct perf_event_attr pea;
  memset(&pea, 0, sizeof pea);
  pea.type = PERF_TYPE_HARDWARE;
  pea.config = PERF_COUNT_HW_CACHE_MISSES;
  pea.size = sizeof pea;
  pea.disabled = 1;

  int fd;
  if ((fd = syscall(SYS_perf_event_open, &pea, 0, -1, -1, 0)) < 0)
    err(EXIT_FAILURE, "perf_event_open");

  // clear the cache
  benchmark::State state(BENCH_ARG);

  clear_cache();

  ioctl_perf(fd, PERF_EVENT_IOC_RESET);
  ioctl_perf(fd, PERF_EVENT_IOC_ENABLE);

  SAFE_CALL(BENCH_NAME(state));

  ioctl_perf(fd, PERF_EVENT_IOC_DISABLE);

  uint64_t count;
  ssize_t bytes;
  if ((bytes = read(fd, &count, sizeof count) != sizeof count)) {
    if (bytes < 0)
      err(EXIT_FAILURE, "read");
    else
      errx(EXIT_FAILURE, "read: unexpected partial read");
  }

  return count;
}

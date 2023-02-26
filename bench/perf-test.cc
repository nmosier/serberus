#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cstdio>

#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <err.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define STAT 0x010009B0

#define ioctl_chk(fd, request, ...)		\
  do								\
    if (ioctl(fd, request __VA_OPT__(,) __VA_ARGS__) < 0)	\
      err(EXIT_FAILURE, "ioctl");				\
  while (false)
  

int main(int argc, char *argv[]) {
  if (argc < 2)
    return EXIT_FAILURE;
  
  const int iters = atoi(argv[1]);
  
  struct perf_event_attr pea;
  memset(&pea, 0, sizeof pea);
#if 0
  pea.type = PERF_TYPE_HARDWARE;
  pea.config = PERF_COUNT_HW_INSTRUCTIONS;
#else
  pea.type = PERF_TYPE_RAW;
  pea.config = STAT;
#endif
  pea.size = sizeof pea;
  pea.disabled = 1;
  
  int fd;
  if ((fd = syscall(SYS_perf_event_open, &pea, 0, -1, -1, 0)) < 0)
    err(EXIT_FAILURE, "perf_event_open");

  ioctl_chk(fd, PERF_EVENT_IOC_RESET, 0);
  ioctl_chk(fd, PERF_EVENT_IOC_ENABLE, 0);

  // work
  volatile int acc = 0;
  for (volatile int i = 0; i < iters; ++i) {
    // do nothing
    acc /= i + 1;
  }

  ioctl_chk(fd, PERF_EVENT_IOC_DISABLE, 0);

  uint64_t count;
  if (read(fd, &count, sizeof count) != sizeof(count))
    err(EXIT_FAILURE, "read");
  
  fprintf(stderr, "instructions: %lu\n", count);
}

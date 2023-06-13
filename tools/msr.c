#define _GNU_SOURCE
#include <fcntl.h>
#include <sched.h>
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>

static int open_msrs(void) {
  char buf[256];
  sprintf(buf, "/dev/cpu/%d/msr", sched_getcpu());
  return open(buf, O_RDWR);
}

static uint64_t read_msr(int fd, uint32_t msr) {
  uint64_t value;
  if (pread(fd, &value, sizeof value, msr) < 0)
    err(EXIT_FAILURE, "pread");
  return value;
}

static void write_msr(int fd, uint32_t msr, uint64_t value) {
  if (pwrite(fd, &value, sizeof value, msr) < 0)
    err(EXIT_FAILURE, "pwrite");
}

struct spec_ctrl {
  uint32_t addr;
  unsigned bit; // bit in IA32_SPEC_CTRL_MSR (72D/48H)
  int mode;
  const char *key; // string representing it (e.g., "SSBD" or "PSFD")
};

static struct spec_ctrl ctrls[] = {
  {.addr = 72,     .bit = 0,  .mode = -1, .key = "IBRS"},
  {.addr = 72,     .bit = 1,  .mode = 0,  .key = "STIBP"},
  {.addr = 72,     .bit = 2,  .mode = 0,  .key = "SSBD"},
  {.addr = 72,     .bit = 3,  .mode = 0,  .key = "IPRED_DIS_U"},
  {.addr = 72,     .bit = 4,  .mode = 0,  .key = "IPRED_DIS_S"},
  {.addr = 72,     .bit = 5,  .mode = 0,  .key = "RRSBA_DIS_U"},
  {.addr = 72,     .bit = 6,  .mode = 0,  .key = "RRSBA_DIS_S"},
  {.addr = 72,     .bit = 7,  .mode = 0,  .key = "PSFD"},
  {.addr = 72,     .bit = 8,  .mode = 0,  .key = "DDPU_U"},
  {.addr = 72,     .bit = 10, .mode = 0,  .key = "BHI_DIS_S"},
  {.addr = 0x1B01, .bit = 0,  .mode = 0,  .key = "DOITM"},
};

#define arrlen(arr) (sizeof (arr) / sizeof (arr)[0])

static int fd;


static void parse_env(void) {
  for (unsigned i = 0; i < arrlen(ctrls); ++i) {
    const char *s;
    struct spec_ctrl *ctrl = &ctrls[i];
    if ((s = getenv(ctrl->key))) {
      if (strcmp(s, "0") == 0)
	ctrl->mode = 0;
      else if (strcmp(s, "1") == 0)
	ctrl->mode = 1;
      else
	errx(EXIT_FAILURE, "bad env var: %s=%s", ctrl->key, s);
    }
  }
}

static void print_controls(int fd) {
  for (unsigned i = 0; i < arrlen(ctrls); ++i) {
    const struct spec_ctrl *ctrl = &ctrls[i];
    const uint64_t value = read_msr(fd, ctrl->addr);
    const bool enabled = (bool) (value & (1 << ctrl->bit));
    fprintf(stderr, "%s = %d\n", ctrl->key, enabled);
  }
}

__attribute__((constructor)) static void init(void) {
  parse_env();
  if ((fd = open_msrs()) < 0) {
    if (getenv("BENCH"))
      err(EXIT_FAILURE, "open_msrs");
    else
      return;
  }
  uint64_t value = read_msr(fd, 72);
  for (unsigned i = 0; i < arrlen(ctrls); ++i) {
    const struct spec_ctrl *ctrl = &ctrls[i];
    uint64_t value = read_msr(fd, ctrl->addr);
    const uint64_t mask = (1 << ctrl->bit);
    switch (ctrl->mode) {
    case 0:
      value &= ~mask;
      break;
    case 1:
      value |= mask;
      break;
    default: break;
    }
    write_msr(fd, ctrl->addr, value);
    assert(read_msr(fd, ctrl->addr) == value);
  }
}

__attribute__((destructor)) static void fin(void) {
  if (fd < 0)
    return;
  for (unsigned i = 0; i < arrlen(ctrls); ++i) {
    const struct spec_ctrl *ctrl = &ctrls[i];
    const uint64_t value = read_msr(fd, ctrl->addr);
    const uint64_t mask = (1 << ctrl->bit);
    switch (ctrl->mode) {
    case 0:
      if ((value & mask))
	errx(EXIT_FAILURE, "control unexpectedly enabled: %s", ctrl->key);
      break;
    case 1:
      if (!(value & mask))
	errx(EXIT_FAILURE, "control unexpectedly disabled: %s", ctrl->key);
      break;
    default: break;
    }
  }
}

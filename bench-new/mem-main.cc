#include <sys/types.h>
#include <unistd.h>
#include <cstdlib>
#include <err.h>
#include <sys/wait.h>
#include "shared-pseudo.h"
#include <sys/resource.h>

static long execute(char *argv[]) {
  const pid_t pid = fork();
  if (pid < 0) {
    err(EXIT_FAILURE, "fork");
  } else if (pid == 0) {
    setenv("EXECUTE", "1", false);
    execvp(argv[0], argv);
    err(EXIT_FAILURE, "execvp");
  } else {
    struct rusage ru;
    if (wait3(nullptr, 0, &ru) < 0)
      err(EXIT_FAILURE, "wait3");
    return ru.ru_maxrss;
  }
}

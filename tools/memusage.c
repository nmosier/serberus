#include <sys/resource.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <sys/wait.h>

static void usage(FILE *f, const char *prog) {
  const char *fmt =
    "usage: %s [-h] [-o <json>] [--] command [arg...]\n"
    ;
  fprintf(f, fmt, prog);
}

int main(int argc, char *argv[]) {
  FILE *log = stderr;

  int optc;
  while ((optc = getopt(argc, argv, "ho:")) >= 0) {
    switch (optc) {
    case 'h':
      usage(stdout, argv[0]);
      return EXIT_SUCCESS;
      
    case 'o': {
      if ((log = fopen(optarg, "w")) == NULL) {
	err(EXIT_FAILURE, "fopen: %s", optarg);
      }
      break;
    }

    default:
      break;
    }
  }

  if (optind == argc) {
    usage(stderr, argv[0]);
    return EXIT_FAILURE;
  }

  char **cmd = &argv[optind];

  const pid_t pid = fork();
  if (pid == 0) {
    execvp(cmd[0], cmd);
    err(EXIT_FAILURE, "execvp: %s", cmd[0]);
  }

  int status;
  if (wait(&status) < 0) {
    err(EXIT_FAILURE, "wait");
  }

  struct rusage usage;
  if (getrusage(RUSAGE_CHILDREN, &usage) < 0) {
    err(EXIT_FAILURE, "getrusage");
  }

  const char *fmt = "{\"mem\": %ld, \"unit\": \"%s\"}\n";
  fprintf(log, fmt, usage.ru_maxrss, "KB");

  int exitno;
  if (WIFEXITED(status) && WEXITSTATUS(status)) {
    exitno = 0;
  } else {
    exitno = 1;
  }

  return exitno;
}

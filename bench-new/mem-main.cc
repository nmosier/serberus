#include "benchmark_memory.h"
#include "shared-main.h"
#include <sys/resource.h>
#include <err.h>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <climits>
#include <cstring>
#include <vector>
#include <fstream>
#include <numeric>
#include <cmath>

#if 1
static FILE *f = stdout;
static unsigned repetitions = 1;

static void parse_args(int argc, char *argv[]) {
  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    std::vector<char> value(std::strlen(arg) + 1);
    if (std::sscanf(arg, "--benchmark_out=%s", value.data()) == 1) {
      if ((f = fopen(value.data(), "w")) == nullptr)
	err(EXIT_FAILURE, "fopen: %s", value.data());
    } else if (std::sscanf(arg, "--benchmark_repetitions=%u", &repetitions) == 1) {
    }
  }
}

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

int main(int argc, char *argv[]) {
  if (getenv("EXECUTE")) {
    benchmark::State state(BENCH_ARG);
    BENCH_NAME(state);
  } else {
    parse_args(argc, argv);
    std::vector<double> measurements(repetitions);
    for (unsigned i = 0; i < repetitions; ++i) {
      measurements[i] = execute(argv);
    }

    // compute average
    const double mean = std::accumulate(measurements.begin(), measurements.end(), 0.) / measurements.size();
    const double variance = std::accumulate(measurements.begin(), measurements.end(), 0., [mean] (double acc, double newval) {
      const double diff = newval - mean;
      return acc + diff * diff;
    }) / measurements.size();
    const double stddev = std::sqrt(variance);

    fprintf(f, "{\n");
    fprintf(f, "\t\"mem\": %f,\n", mean);
    fprintf(f, "\t\"stddev\": %f\n", stddev);
    fprintf(f, "}\n");
  }
}
#else
int main() {
  benchmark::State state(BENCH_ARG);
  BENCH_NAME(state);
}
#endif

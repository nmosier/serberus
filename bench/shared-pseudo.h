#pragma once

#include <cstdio>
#include <cstring>
#include <vector>
#include <numeric>
#include <err.h>
#include "benchmark_memory.h"
#include "shared-main.h"

#define STR(x) #x
#define XSTR(x) STR(x)

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

static long execute(char *argv[]);

#ifdef EXECUTE_CUSTOM
static void execute_nested(void);
#endif

#ifndef NOMAIN
int main(int argc, char *argv[]) {
  if (getenv("EXECUTE")) {
#ifndef EXECUTE_CUSTOM
    benchmark::State state(BENCH_ARG);
    BENCH_NAME(state);
#else
    execute_nested();
#endif
  } else {
    parse_args(argc, argv);
    std::vector<double> measurements(repetitions);
    for (unsigned i = 0; i < repetitions; ++i) {
      measurements[i] = execute(argv);
    }

    // compute average
    const double mean = std::accumulate(measurements.begin(), measurements.end(), 0.) / measurements.size();
#if 0
    const double variance = std::accumulate(measurements.begin(), measurements.end(), 0., [mean] (double acc, double newval) {
      const double diff = newval - mean;
      return acc + diff * diff;
    }) / measurements.size();
    const double stddev = std::sqrt(variance);
#endif

    const char *fmt = R"=(
{
  "benchmarks": [
    {
      "name": "%s/%d_mean",
      "%s": %f
    }
  ]
}
)=";
    std::fprintf(f, fmt, XSTR(BENCH_NAME), BENCH_ARG, XSTR(BENCH_METRIC), mean);
  }
}
#endif

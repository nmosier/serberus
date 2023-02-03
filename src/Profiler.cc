#include <sstream>
#include <iomanip>

#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <cassert>

#include <unistd.h>
#include <err.h>

#include <gperftools/profiler.h>

static bool profiling = false;
static char *path = nullptr;
static clock_t start;

static __attribute__((constructor)) void profiler_init(void) {
  const char *tmpdir = getenv("TMPDIR");
  if (tmpdir == nullptr)
    tmpdir = "/tmp";

  if (asprintf(&path, "%s/XXXXXX", tmpdir) < 0)
    err(EXIT_FAILURE, "asprintf");

  if (mkstemp(path) < 0)
    err(EXIT_FAILURE, "mkstemp: %s", path);

  start = clock();
  ProfilerStart(path);
  profiling = true;
}

static __attribute__((destructor)) void profiler_deinit(void) {
  if (profiling) {
    ProfilerStop();

    // get milliseconds
    const float s = static_cast<float>(clock() - start) / CLOCKS_PER_SEC;
    assert(s >= 0);
    const unsigned ms = s * 1000;

    std::stringstream ss;

    if (const char *dir = getenv("PROFDIR"))
      ss << dir << "/";
    ss << "prof-t" << std::setw(6) << std::setfill('0') << ms << "-p" << getpid() << ".out";

    if (rename(path, ss.str().c_str()) < 0)
      err(EXIT_FAILURE, "rename: %s to %s", path, ss.str().c_str());
  }
    
}

#include <libunwind.h>
#include <err.h>
#include <cstdlib>
#include <string>
#include <map>
#include <cstdint>

#define UNW_CHK(name, res)					\
  do {								\
    const int err_ = (res);					\
    if (err_ < 0) {						\
      errx(EXIT_FAILURE, name ": %s", unw_strerror(-err_));	\
    }								\
  } while (false)

namespace {

  struct Record {
    size_t n = 0;
    unw_word_t off;
    int64_t id;
    const char *s;
    char func[256];
  };

  static FILE *log = stderr;

  struct Trace {
    using Key = uintptr_t;
    using Value = Record;

    std::map<Key, Value> locs;

    ~Trace() {
      for (const auto& [key, value] : locs) {
	std::fprintf(log, "%zu %s%+zd %zd %s\n", value.n, value.func, value.off, value.id, value.s);
      }
    }
    
  } trace;

}

extern "C" void clou_trace(int64_t id, const char *s) {
  unw_context_t ctx;
  int err;
  UNW_CHK("unw_getcontext", unw_getcontext(&ctx));

  unw_cursor_t cursor;
  UNW_CHK("unw_init_local", unw_init_local(&cursor, &ctx));

  err = unw_step(&cursor);
  if (err < 0) {
    errx(EXIT_FAILURE, "unw_step: %s", unw_strerror(-err));
  } else if (err == 0) {
    std::abort();
  }

  unw_word_t rip;
  UNW_CHK("unw_get_reg", unw_get_reg(&cursor, UNW_X86_64_RIP, &rip));

  
  auto& record = trace.locs[rip];
  if (record.n == 0) {
    UNW_CHK("unw_get_proc_name", unw_get_proc_name(&cursor, record.func, sizeof record.func, &record.off));
  }
  ++record.n;
  record.s = s;
  record.id = id;
}

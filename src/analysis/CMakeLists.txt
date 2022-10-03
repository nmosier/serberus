add_library(LeakAnalysis SHARED
  LeakAnalysis.cc
  ../include/clou/analysis/LeakAnalysis.h
)
register_llvm_pass(LeakAnalysis)
target_link_libraries(LeakAnalysis PRIVATE Transmitter CommandLine)

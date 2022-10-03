add_library(LeakAnalysis SHARED
  LeakAnalysis.cc
  ../include/clou/analysis/LeakAnalysis.h
)
register_llvm_pass(LeakAnalysis)
target_link_libraries(LeakAnalysis PRIVATE Transmitter CommandLine)

add_library(NonspeculativeTaintAnalysis SHARED
  NonspeculativeTaintAnalysis.cc
  ../include/clou/analysis/NonspeculativeTaintAnalysis.h
)
register_llvm_pass(NonspeculativeTaintAnalysis)
target_link_libraries(NonspeculativeTaintAnalysis PRIVATE Mitigation util Transmitter CommandLine)

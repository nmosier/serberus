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

add_library(SpeculativeTaintAnalysis SHARED
  SpeculativeTaintAnalysis.cc
  ../include/clou/analysis/SpeculativeTaintAnalysis.h
)
register_llvm_pass(SpeculativeTaintAnalysis)
target_link_libraries(SpeculativeTaintAnalysis PRIVATE util Mitigation)

add_library(StackInitAnalysis SHARED
  StackInitAnalysis.cc
  ../include/clou/analysis/StackInitAnalysis.h
)
register_llvm_pass(StackInitAnalysis)
target_link_libraries(StackInitAnalysis PRIVATE util LeakAnalysis SpeculativeTaintAnalysis Frontier)

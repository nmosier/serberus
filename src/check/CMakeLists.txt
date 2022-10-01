add_library(StackInitChecker SHARED
  StackInitChecker.cc
)
register_llvm_pass(StackInitChecker)
target_link_libraries(StackInitChecker PRIVATE util)


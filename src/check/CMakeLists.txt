add_library(ContractChecker SHARED
  ContractChecker.cc
)
register_llvm_pass(ContractChecker)
target_link_libraries(ContractChecker PRIVATE util Transmitter)


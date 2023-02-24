include(BenchmarkFlags)
include(LLVMTestSuite)

llvm_test_suite(test_suite_cloucc
  ${cloucc_base_args}
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou
  CMAKE_OPTIONS -DTEST_SUITE_BENCHMARKING_ONLY=On
)


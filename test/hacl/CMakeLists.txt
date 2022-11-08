include(HaclLibrary)
include(BenchmarkFlags)

make_directory(${CMAKE_CURRENT_BINARY_DIR}/logs)

function(hacl_benchmark_library NAME)
  hacl_library(${NAME} ${ARGN} CFLAGS -O3 -fno-stack-protector)
endfunction()

hacl_benchmark_library(hacl_baseline_none)
hacl_benchmark_library(hacl_baseline_lfence ${baseline_lfence_args})
hacl_benchmark_library(hacl_baseline_slh ${baseline_slh_args})
hacl_benchmark_library(hacl_baseline_retpoline ${baseline_retpoline_args})
hacl_benchmark_library(hacl_baseline_ssbd ${baseline_ssbd_args})
hacl_benchmark_library(hacl_baseline_lfence+retpoline+ssbd ${baseline_lfence_retpoline_ssbd_args})
hacl_benchmark_library(hacl_baseline_slh+retpoline+ssbd ${baseline_slh_retpoline_ssbd_args})
hacl_benchmark_library(hacl_cloucc ${cloucc_args}
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
)

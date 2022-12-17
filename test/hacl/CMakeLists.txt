include(HaclLibrary)
include(BenchmarkFlags)

make_directory(${CMAKE_CURRENT_BINARY_DIR}/logs)

function(hacl_benchmark_library NAME)
  hacl_library(${NAME} ${ARGN} CFLAGS -O3 -fno-stack-protector)
  hacl_library(${NAME}_marked ${ARGN}
    CFLAGS -O3 -fno-stack-protector
    LLVMFLAGS -clou-mitigation-trap
    LDFLAGS -Wl,-z,trace-mitigations
  )
endfunction()

hacl_benchmark_library(hacl_baseline_none)
hacl_benchmark_library(hacl_baseline_lfence ${baseline_lfence_args})
hacl_benchmark_library(hacl_baseline_slh ${baseline_slh_args})
hacl_benchmark_library(hacl_baseline_retpoline ${baseline_retpoline_args})
hacl_benchmark_library(hacl_baseline_ssbd ${baseline_ssbd_args})
hacl_benchmark_library(hacl_baseline_lfence+retpoline+ssbd ${baseline_lfence_retpoline_ssbd_args})
hacl_benchmark_library(hacl_baseline_slh+retpoline+ssbd ${baseline_slh_retpoline_ssbd_args})

hacl_benchmark_library(hacl_cloucc ${cloucc_base_args}
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou
)

hacl_benchmark_library(hacl_cloucc_greedy ${cloucc_base_args}
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou -clou-min-cut-alg=greedy
)

hacl_benchmark_library(hacl_cloucc_psf ${cloucc_base_args}
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou -clou-min-cut-alg=greedy -clou-psf
)

hacl_benchmark_library(hacl_cloucc_psfmem ${cloucc_base_args}
  PASS InlinePass MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou -clou-min-cut-alg=greedy -clou-psf -clou-restricted-psf=0
)

hacl_benchmark_library(hacl_cloucc_ncas ${cloucc_base_args}
  PASS InlinePass MitigatePass Attributes
  LLVMFLAGS -clou=oobs
)
hacl_benchmark_library(hacl_cloucc_fps ${cloucc_base_args}
  PASS InlinePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=fps,postch
)

hacl_benchmark_library(hacl_cloucc_nofps ${cloucc_base_args}
  PASS InlinePass MitigatePass Attributes
  LLVMFLAGS -clou=udt,oobs,prech
)

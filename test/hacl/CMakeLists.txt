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

hacl_benchmark_library(hacl_cloucc ${cloucc_base_args}
  PASS DuplicatePass MemIntrinsicPass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou
)

hacl_benchmark_library(hacl_cloucc_ncasall ${cloucc_base_args}
  PASS DuplicatePass MemIntrinsicPass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou -llsct-ncas-all
)

hacl_benchmark_library(hacl_cloucc_csrs ${cloucc_base_args}
  PASS DuplicatePass MemIntrinsicPass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit,ncal_glob,ncas_xmit,ncas_ctrl,prech,fps,postch
)

hacl_benchmark_library(hacl_cloucc_ssbd ${cloucc_base_args}
  PASS DuplicatePass MemIntrinsicPass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit,ncal_glob,ncas_xmit,ncas_ctrl,prech,stackinit
  ${baseline_ssbd_args}
)

hacl_benchmark_library(hacl_cloucc_init ${cloucc_base_args}
  PASS DuplicatePass MemIntrinsicPass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit,ncal_glob,ncas_xmit,ncas_ctrl,prech,stackinit
)

hacl_benchmark_library(hacl_cloucc_mem ${cloucc_base_args}
  PASS DuplicatePass MemIntrinsicPass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou
)

hacl_benchmark_library(hacl_cloucc_opt ${cloucc_base_args}
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou -clou-expand-sts
)

hacl_benchmark_library(hacl_cloucc_naive ${cloucc_base_args}
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=load_xmit,prech
)

hacl_benchmark_library(hacl_cloucc_ncal_xmit ${cloucc_base_args}
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit
)

hacl_benchmark_library(hacl_cloucc_ncal_glob ${cloucc_base_args}
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_glob
)

hacl_benchmark_library(hacl_cloucc_ncas_xmit ${cloucc_base_args}
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncas_xmit
)

hacl_benchmark_library(hacl_cloucc_ncas_ctrl ${cloucc_base_args}
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncas_ctrl
)

hacl_benchmark_library(hacl_cloucc_load_xmit ${cloucc_base_args}
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=load_xmit
)

hacl_benchmark_library(hacl_cloucc_entry ${cloucc_base_args}
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit,ncal_glob,ncas_ctrl,ncas_xmit,entry_xmit,prech
)

hacl_benchmark_library(hacl_cloucc_sts ${cloucc_base_args}
  PASS DuplicatePass InlinePass MitigatePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=ncal_xmit,ncal_glob,ncas_xmit,ncas_ctrl
)

hacl_benchmark_library(hacl_cloucc_fps+postch ${cloucc_base_args}
  PASS DuplicatePass InlinePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=fps,postch
)

hacl_benchmark_library(hacl_cloucc_fps ${cloucc_base_args}
  PASS DuplicatePass InlinePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=fps
)

hacl_benchmark_library(hacl_cloucc_prech ${cloucc_base_args}
  PASS DuplicatePass InlinePass FunctionLocalStacks Attributes
  LLVMFLAGS -clou=prech
)

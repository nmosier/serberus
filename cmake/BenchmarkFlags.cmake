set(baseline_lfence_args
  LLVMFLAGS -x86-speculative-load-hardening -x86-slh-lfence -x86-slh-fence-call-and-ret
)
set(baseline_slh_args
  LLVMFLAGS -x86-speculative-load-hardening
)
set(baseline_retpoline_args
  CFLAGS -mretpoline
  LDFLAGS -Wl,-z,retpolineplt
)
set(baseline_ssbd_args
  LDFLAGS -Wl,-rpath,$<TARGET_FILE_DIR:libssbd> -L$<TARGET_FILE_DIR:libssbd> -l$<TARGET_FILE_BASE_NAME:libssbd>
  DEPENDS libssbd
)

set(baseline_lfence_retpoline_ssbd_args
  ${baseline_lfence_args}
  ${baseline_retpoline_args}
  ${baseline_ssbd_args}
)

set(baseline_slh_retpoline_ssbd_args
  ${baseline_slh_args}
  ${baseline_retpoline_args}
  ${baseline_ssbd_args}
)

set(cloucc_args
  PASSES MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes  
  LLVMFLAGS -clou -no-stack-slot-sharing
)

set(cloucc_udt_args
  PASSES MitigatePass
  LLVMFLAGS -clou=udt
)

set(cloucc_ncas_args
  PASSES MitigatePass
  LLVMFLAGS -clou=oobs
)

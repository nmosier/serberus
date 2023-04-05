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
  CFLAGS -fcf-protection=none
)

set(baseline_slh_retpoline_ssbd_args
  ${baseline_slh_args}
  ${baseline_retpoline_args}
  ${baseline_ssbd_args}
  CFLAGS -fcf-protection=none
)

set(cloucc_base_args
  LLVMFLAGS -clou
  
  # Restrictions
  LLVMFLAGS -no-stack-slot-sharing -no-promote-arguments
  CFLAGS -fno-jump-tables

  # Weights
  LLVMFLAGS -clou-loop-weight=${LLSCT_LOOP_WEIGHT} -clou-dom-weight=${LLSCT_DOM_WEIGHT} -clou-st-weight=${LLSCT_ST_WEIGHT}
  
  # CET
  LDFLAGS -Wl,-rpath,$<TARGET_FILE_DIR:cet> -L$<TARGET_FILE_DIR:cet> -lcet
  DEPENDS cet
  LDFLAGS -Wl,-z,force-ibt -Wl,-z,force-shstk
  CFLAGS -fcf-protection=full
)

# Google Benchmark Flags
set(benchmark_runtime_flags --benchmark_repetitions=5 --benchmark_min_warmup_time=1 --benchmark_display_aggregates_only=true
  --benchmark_time_unit=ns)

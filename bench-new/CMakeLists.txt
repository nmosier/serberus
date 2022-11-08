include(BenchmarkFlags)

add_link_options(-fPIE -fPIC -pthread)

function(add_benchmark_shared lib name mode arg metric)
  string(TOUPPER ${lib} LIB)
  string(TOUPPER ${metric} METRIC)
  set(exe ${metric}_${lib}_${name}_${arg}_${mode})
  add_executable(${exe} ${lib}_${name}.cc ${metric}-main.cc)
  target_link_libraries(${exe} PRIVATE ${lib}_${mode})
  target_compile_definitions(${exe} PRIVATE BENCH_${LIB} BENCH_${METRIC} BENCH_NAME=${lib}_${name} BENCH_ARG=${arg})
  set(exe ${exe} PARENT_SCOPE)
endfunction()

function(add_mem_benchmark)
  add_benchmark_shared(${ARGN} mem)
endfunction()

function(add_time_benchmark)
  add_benchmark_shared(${ARGN} time)
  target_link_libraries(${exe} PRIVATE benchmark::benchmark)
endfunction()

function(add_benchmarks lib name arg)
  set(modes baseline_none baseline_lfence+retpoline+ssbd baseline_slh+retpoline+ssbd cloucc)
  foreach(mode IN LISTS modes)
    add_time_benchmark(${lib} ${name} ${mode} ${arg})
    add_mem_benchmark(${lib} ${name} ${mode} ${arg})
  endforeach()
endfunction()


# add_time_benchmark(libsodium salsa20 baseline_none 6# 4)
# add_mem_benchmark(libsodium salsa20 baseline_none 64)
add_benchmarks(libsodium salsa20    64)
add_benchmarks(libsodium sha256     64)
add_benchmarks(libsodium sha256     8192)
add_benchmarks(hacl      chacha20   8192)
add_benchmarks(hacl      poly1305   1024)
add_benchmarks(hacl      curve25519 64)


function(add_breakdowns)
  set(modes baseline_none baseline_lfence baseline_slh baseline_retpoline baseline_ssbd baseline_lfence+retpoline+ssbd baseline_slh+retpoline+ssbd cloucc cloucc_udt cloucc_ncas cloucc_fps cloucc_prech)
  foreach(mode IN LISTS modes)
    set(exe breakdown_${mode})
    add_executable(${exe} ${ARGN})
    target_link_libraries(${exe} PRIVATE libsodium_${mode} benchmark::benchmark)
    target_compile_definitions(${exe} PRIVATE BENCH_LIBSODIUM BENCH_TIME)
  endforeach()
endfunction()

add_breakdowns(breakdown.cc libsodium_sha256.cc)

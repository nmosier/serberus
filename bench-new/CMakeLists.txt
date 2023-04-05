include(BenchmarkFlags)

add_link_options(-fPIE -fPIC -pthread)

set(metrics time mem cache stall inst serial perf mitigation raw)

foreach(metric IN LISTS metrics)
  add_custom_target(${metric}_compile)
endforeach()

function(add_benchmark_shared lib name mode arg metric libsuffix)
  string(TOUPPER ${lib} LIB)
  string(TOUPPER ${metric} METRIC)
  set(exe ${metric}_${lib}_${name}_${arg}_${mode})
  add_executable(${exe} ${lib}_${name}.cc ${metric}-main.cc)
  target_link_libraries(${exe} PRIVATE ${lib}_${mode}${libsuffix})
  target_compile_definitions(${exe} PRIVATE BENCH_${LIB} BENCH_${METRIC} BENCH_METRIC=${metric}
    BENCH_NAME=${lib}_${name} BENCH_ARG=${arg})
  target_compile_options(${exe} PRIVATE -Wno-unused-parameter)
  set(exe ${exe} PARENT_SCOPE)

  # add rule for generating jsons
  set(json ${exe}.json)
  add_custom_command(OUTPUT ${json}
    COMMAND ${exe} ${benchmark_runtime_flags} --benchmark_out_format=json --benchmark_out=${json} --benchmark_color=true
    DEPENDS ${exe}
  )
  add_custom_target(${exe}_json
    DEPENDS ${json}
  )
  get_directory_property(jsons ${metric}_jsons)
  set_property(DIRECTORY PROPERTY ${metric}_jsons ${jsons} ${json})

  add_dependencies(${metric}_compile ${exe})
endfunction()

function(add_mem_benchmark)
  add_benchmark_shared(${ARGN} mem "")
endfunction()

function(add_time_benchmark)
  add_benchmark_shared(${ARGN} time "")
  target_link_libraries(${exe} PRIVATE benchmark::benchmark)
endfunction()

function(add_cache_benchmark)
  add_benchmark_shared(${ARGN} cache "")
endfunction()

function(add_stall_benchmark)
  add_benchmark_shared(${ARGN} stall "")
endfunction()

function(add_inst_benchmark)
  FetchContent_GetProperties(Pin SOURCE_DIR PIN_DIR)
  add_benchmark_shared(${ARGN} inst "")
  target_compile_definitions(${exe} PRIVATE PIN_DIR=\"${PIN_DIR}\" PIN_TOOL=\"$<TARGET_PROPERTY:inscount,LIBRARY>\")
  add_dependencies(${exe} inscount)
endfunction()

function(add_serial_benchmark)
  add_benchmark_shared(${ARGN} serial "")
endfunction()

function(add_perf_benchmark)
  add_benchmark_shared(${ARGN} perf "")
endfunction()

function(add_mitigation_benchmark)
  add_benchmark_shared(${ARGN} mitigation "")
  target_compile_definitions(${exe} PRIVATE PIN_DIR=\"${PIN_DIR}\" PIN_TOOL=\"$<TARGET_PROPERTY:bkptcount,LIBRARY>\")
  add_dependencies(${exe} bkptcount)
endfunction()

function(add_trace_benchmark lib name arg)
  set(metric trace)
  set(mode cloucc_trace)
  string(TOUPPER ${lib} LIB)
  string(TOUPPER ${metric} METRIC)
  set(exe ${metric}_${lib}_${name}_${arg}_${mode})
  add_executable(${exe} ${lib}_${name}.cc ${metric}-main.cc)
  target_link_libraries(${exe} PRIVATE ${lib}_${mode}${libsuffix})
  target_compile_definitions(${exe} PRIVATE BENCH_${LIB} BENCH_${METRIC} BENCH_METRIC=${metric}
    BENCH_NAME=${lib}_${name} BENCH_ARG=${arg})
  target_compile_options(${exe} PRIVATE -Wno-unused-parameter)
  set(exe ${exe} PARENT_SCOPE)
endfunction()

function(add_raw_benchmark lib name mode arg)
  set(metric raw)
  string(TOUPPER ${lib} LIB)
  string(TOUPPER ${metric} METRIC)
  set(exe ${metric}_${lib}_${name}_${arg}_${mode})
  add_executable(${exe} ${lib}_${name}.cc ${metric}-main.cc)
  target_link_libraries(${exe} PRIVATE ${lib}_${mode}${libsuffix})
  target_compile_definitions(${exe} PRIVATE BENCH_${LIB} BENCH_${METRIC} BENCH_METRIC=${metric}
    BENCH_NAME=${lib}_${name} BENCH_ARG=${arg})
  target_compile_options(${exe} PRIVATE -Wno-unused-parameter -Wno-unused-function)
  set(exe ${exe} PARENT_SCOPE)
  add_dependencies(${metric}_compile ${exe})
endfunction()

function(add_benchmarks lib name arg)
  # set(modes baseline_none cloucc)
  # set(modes baseline_none cloucc cloucc_sts cloucc_ncal_xmit cloucc_ncal_glob cloucc_ncas_xmit cloucc_ncas_ctrl cloucc_fps cloucc_fps+postch cloucc_prech)
  set(modes baseline_none baseline_lfence+retpoline+ssbd baseline_slh+retpoline+ssbd cloucc baseline_slh) # cloucc_csrs) # cloucc_ssbd cloucc_init) # cloucc_opt cloucc_naive)
  # set(modes baseline_none baseline_lfence+retpoline+ssbd baseline_slh+retpoline+ssbd cloucc_test)
  # set(modes baseline_none baseline_lfence+retpoline+ssbd baseline_slh+retpoline+ssbd cloucc_ncal_xmit)  
  # set(modes baseline_none baseline_lfence+retpoline+ssbd baseline_slh+retpoline+ssbd cloucc cloucc_ncal_xmit cloucc_ncas_xmit cloucc_ncas_ctrl)
  # set(modes baseline_none baseline_slh+retpoline+ssbd cloucc_ncas)
  foreach(mode IN LISTS modes)
    add_time_benchmark(${lib} ${name} ${mode} ${arg})
    add_mem_benchmark(${lib} ${name} ${mode} ${arg})
    add_cache_benchmark(${lib} ${name} ${mode} ${arg})
    add_stall_benchmark(${lib} ${name} ${mode} ${arg})
    add_inst_benchmark(${lib} ${name} ${mode} ${arg})
    add_serial_benchmark(${lib} ${name} ${mode} ${arg})
    add_perf_benchmark(${lib} ${name} ${mode} ${arg})
    add_mitigation_benchmark(${lib} ${name} ${mode} ${arg})
    add_raw_benchmark(${lib} ${name} ${mode} ${arg})
  endforeach()
  add_trace_benchmark(${lib} ${name} ${arg})
endfunction()


add_benchmarks(libsodium salsa20    64)
add_benchmarks(libsodium sha256     64)
add_benchmarks(libsodium sha256     8192)
add_benchmarks(hacl      chacha20   8192)
add_benchmarks(hacl      poly1305   1024)
add_benchmarks(hacl      curve25519 64)
add_benchmarks(openssl   sha256     64)
add_benchmarks(openssl   sha256     8192)
add_benchmarks(openssl   chacha20   8192)
add_benchmarks(openssl   curve25519 64)

# add_benchmarks(openssl   sha256     1048576)


function(add_breakdowns)
  set(modes baseline_none baseline_lfence baseline_slh baseline_retpoline baseline_ssbd baseline_lfence+retpoline+ssbd baseline_slh+retpoline+ssbd cloucc cloucc_udt cloucc_ncas cloucc_fps cloucc_prech)
  foreach(mode IN LISTS modes)
    set(exe breakdown_${mode})
    add_executable(${exe} ${ARGN})
    target_link_libraries(${exe} PRIVATE libsodium_${mode} benchmark::benchmark)
    target_compile_definitions(${exe} PRIVATE BENCH_NAME=libsodium_sha256 BENCH_LIBSODIUM BENCH_TIME BENCH_METRIC=time BENCH_ARG=0)
  endforeach()
endfunction()

add_breakdowns(breakdown.cc libsodium_sha256.cc)

# Generate timing plot
foreach(metric IN LISTS metrics)
  get_directory_property(metric_jsons ${metric}_jsons)
  get_directory_property(bench_clean bench_clean)
  set_property(DIRECTORY PROPERTY bench_clean ${bench_clean} ${metric_jsons} ${metric}.pdf)
  
  add_custom_target(${metric}_jsons DEPENDS ${metric_jsons})
  
  add_custom_command(OUTPUT ${metric}.pdf ${metric}.pdf.txt
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/plot.py ${metric_jsons} -o ${metric}.pdf
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/plot.py ${metric_jsons} ${Python3_EXECUTABLE}
  )
  add_custom_target(${metric}_pdf
    DEPENDS ${metric}.pdf
  )
endforeach()

get_directory_property(bench_clean bench_clean)
add_custom_target(clean_bench
  COMMAND rm -f ${bench_clean}
)



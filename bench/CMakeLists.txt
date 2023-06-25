include(../bench/CompilationFlags)

# set dependencies
# expects list of keys 
function(register_mode name)
  set(flags)
  foreach(key IN ITEMS ${ARGN})
    list(APPEND flags ${compile_${key}})
  endforeach()
  # set(compile_${name} ${flags} PARENT_SCOPE)
  message("${name} compile flags: ${flags}")
  foreach(lib IN ITEMS libsodium hacl openssl)
    cmake_language(CALL add_${lib}_library ${lib}_${name} ${flags})
  endforeach()

  # run flags
  set(runflags)
  foreach(key IN ITEMS ${ARGN})
    list(APPEND runflags ${run_${key}})
  endforeach()
  set(runc_${name} ${runflags} PARENT_SCOPE)

  list(APPEND modes ${name})
  set(modes ${modes} PARENT_SCOPE)
endfunction()

register_mode(base base)
# register_mode(lfence base lfence)
# register_mode(lfence+retpoline base lfence retpoline)
register_mode(lfence+retpoline+ssbd base lfence retpoline ssbd)
# register_mode(slh base slh)
# register_mode(slh+retpoline base slh retpoline)
register_mode(slh+retpoline+ssbd base slh retpoline ssbd)
register_mode(uslh+retpoline+ssbd base uslh retpoline ssbd)
# register_mode(blade+retpoline base blade retpoline)
register_mode(blade+retpoline+ssbd base blade retpoline ssbd)

# register_mode(llsct-fence        base swmodel llsct_fence)
# register_mode(llsct-fence+fps    base swmodel llsct_fence llsct_fps)
# register_mode(llsct-fence+fps+rc base swmodel llsct_fence llsct_fps llsct_regclean)
register_mode(llsct              base swmodel llsct_fence llsct_fps llsct_regclean hwmodel)

register_mode(llsct+fallthru base swmodel llsct_fence llsct_fps llsct_regclean hwmodel llsct_fallthru)


# These are disabled for now. 
# register_mode(llsctssbd-fence                  base swmodel llsctssbd_fence)
# register_mode(llsctssbd-fence+stkinit          base swmodel llsctssbd_fence llsctssbd_stkinit)
# register_mode(llsctssbd-fence+stkinit+regclean base swmodel llsctssbd_fence llsctssbd_stkinit llsct_regclean)
# register_mode(llsctssbd                        base swmodel llsctssbd_fence llsctssbd_stkinit llsct_regclean hwmodel ssbd)
# 
# register_mode(llsctpsf-fence    base swmodel llsctpsf_fence)
# register_mode(llsctpsf-fence+rc base swmodel llsctpsf_fence llsct_regclean)
# register_mode(llsctpsf          base swmodel llsctpsf_fence llsct_regclean hwmodel+psf)


include(BenchmarkFlags)

add_link_options(-fPIE -fPIC -pthread)

# set(metrics time mem cache stall inst serial perf mitigation raw)
set(metrics time raw)

foreach(metric IN LISTS metrics)
  add_custom_target(${metric}_compile)
  add_custom_target(${metric}_sh)
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
  add_dependencies(${exe} ${lib}_${mode}${libsuffix})
  set(exe ${exe} PARENT_SCOPE)

  # add rule for generating jsons
  set(json ${exe}.json)
  add_custom_command(OUTPUT ${json}
    COMMAND sudo taskset -c 0 env BENCH=1 ${runc_${mode}} ${CMAKE_CURRENT_BINARY_DIR}/${exe} ${benchmark_runtime_flags} --benchmark_out_format=json --benchmark_out=${json} --benchmark_color=true
    DEPENDS ${exe}
  )
  add_custom_target(${exe}_json
    DEPENDS ${json}
  )
  get_directory_property(jsons ${metric}_jsons)
  set_property(DIRECTORY PROPERTY ${metric}_jsons ${jsons} ${json})

  # generate run script
  set(sh ${exe}.sh)
  set(cmd sudo taskset -c 0 env BENCH=1 $@ ${runc_${mode}} ${CMAKE_CURRENT_BINARY_DIR}/${exe} ${benchmark_runtime_flags} --benchmark_out_format=json --benchmark_out=${json} --benchmark_color=true)
  list(JOIN cmd " " cmd)
  configure_file(template.sh.in ${sh})
    # FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE  

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
  # set(modes base lfence lfence+retpoline lfence+retpoline+ssbd slh slh+retpoline slh+retpoline+ssbd)
    # set(modes baseline_none baseline_lfence+retpoline+ssbd baseline_slh+retpoline+ssbd cloucc baseline_slh) # cloucc_csrs) # cloucc_ssbd cloucc_init) # cloucc_opt cloucc_naive)
  # set(modes baseline_none baseline_lfence+retpoline+ssbd baseline_slh+retpoline+ssbd cloucc_test)
  # set(modes baseline_none baseline_lfence+retpoline+ssbd baseline_slh+retpoline+ssbd cloucc_ncal_xmit)  
  # set(modes baseline_none baseline_lfence+retpoline+ssbd baseline_slh+retpoline+ssbd cloucc cloucc_ncal_xmit cloucc_ncas_xmit cloucc_ncas_ctrl)
  # set(modes baseline_none baseline_slh+retpoline+ssbd cloucc_ncas)
  foreach(mode IN LISTS modes)
    add_time_benchmark(${lib} ${name} ${mode} ${arg})
    # add_mem_benchmark(${lib} ${name} ${mode} ${arg})
    # add_cache_benchmark(${lib} ${name} ${mode} ${arg})
    # add_stall_benchmark(${lib} ${name} ${mode} ${arg})
    # add_inst_benchmark(${lib} ${name} ${mode} ${arg})
    # add_serial_benchmark(${lib} ${name} ${mode} ${arg})
    # add_perf_benchmark(${lib} ${name} ${mode} ${arg})
    # add_mitigation_benchmark(${lib} ${name} ${mode} ${arg})
    add_raw_benchmark(${lib} ${name} ${mode} ${arg})
  endforeach()
  # add_trace_benchmark(${lib} ${name} ${arg})
endfunction()


add_benchmarks(libsodium salsa20    64)
add_benchmarks(libsodium sha256     64)
add_benchmarks(libsodium sha256     8192)
add_benchmarks(hacl      chacha20   8192)
add_benchmarks(hacl      poly1305   8192)
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

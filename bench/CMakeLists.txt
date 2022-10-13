include(BenchmarkFlags)

add_link_options(-fPIE -fPIC -pthread)

configure_file(breakdown.py.in breakdown.py)
configure_file(stats.py.in stats.py)

foreach(benchspec IN ITEMS baseline_breakdown cloucc_breakdown)
  file(GENERATE
    OUTPUT ${benchspec}.json
    INPUT ${benchspec}.json.in
  )
endforeach()

function(add_benchmark NAME SRC LIB)
  add_executable(${NAME} ${SRC})
  target_link_libraries(${NAME} PRIVATE ${LIB} benchmark::benchmark)
  add_custom_command(OUTPUT ${NAME}.json
    COMMAND ${NAME} --benchmark_out=${NAME}.json --benchmark_out_format=json --benchmark_repetitions=5 --benchmark_min_warmup_time=1
    DEPENDS ${NAME}
  )
  add_custom_target(${NAME}_json
    DEPENDS ${NAME}.json
  )
endfunction()

set(libsodium_benchmark_libraries
  libsodium_baseline_none
  libsodium_baseline_lfence
  libsodium_baseline_slh
  libsodium_baseline_retpoline
  libsodium_baseline_ssbd
  libsodium_baseline_lfence+retpoline+ssbd
  libsodium_baseline_slh+retpoline+ssbd
  libsodium_cloucc_udt
  libsodium_cloucc_ncas
  libsodium_cloucc_fps
  libsodium_cloucc_prech
  libsodium_cloucc
  libsodium_cloucc_unweighted
  libsodium_cloucc_unsafeaa
)

foreach(libsodium_benchmark_library IN LISTS libsodium_benchmark_libraries)
  # ExternalProject_Get_Property(${libsodium_benchmark_library} install_dir)
  string(REGEX REPLACE "^libsodium_" "" name ${libsodium_benchmark_library})
  add_benchmark(${name}_benchmark libsodium_sha256.cc ${libsodium_benchmark_library})
endforeach()

get_target_property(cloucc_benchmark_logs libsodium_cloucc CLOU_LOGS)
set_target_properties(cloucc_benchmark PROPERTIES CLOU_LOGS ${cloucc_benchmark_logs})

add_executable(libsodium_sha256_none_benchmark ALIAS baseline_none_benchmark)
add_executable(libsodium_sha256_lfence+retpoline+ssbd_benchmark ALIAS baseline_lfence+retpoline+ssbd_benchmark)
add_executable(libsodium_sha256_slh+retpoline+ssbd_benchmark ALIAS baseline_slh+retpoline+ssbd_benchmark)
add_executable(libsodium_sha256_cloucc_benchmark ALIAS cloucc_benchmark)

add_executable(libsodium_salsa20_none_benchmark libsodium_salsa20.cc)
add_executable(libsodium_salsa20_lfence+retpoline+ssbd_benchmark libsodium_salsa20.cc)
add_executable(libsodium_salsa20_slh+retpoline+ssbd_benchmark libsodium_salsa20.cc)
add_executable(libsodium_salsa20_cloucc_benchmark libsodium_salsa20.cc)
target_link_libraries(libsodium_salsa20_none_benchmark PRIVATE libsodium_baseline_none benchmark::benchmark)
target_link_libraries(libsodium_salsa20_lfence+retpoline+ssbd_benchmark PRIVATE libsodium_baseline_lfence+retpoline+ssbd benchmark::benchmark)
target_link_libraries(libsodium_salsa20_slh+retpoline+ssbd_benchmark PRIVATE libsodium_baseline_slh+retpoline+ssbd benchmark::benchmark)
target_link_libraries(libsodium_salsa20_cloucc_benchmark PRIVATE libsodium_cloucc benchmark::benchmark)

get_target_property(libsodium_salsa20_cloucc_benchmark_logs libsodium_cloucc CLOU_LOGS)
set_target_properties(libsodium_salsa20_cloucc_benchmark PROPERTIES CLOU_LOGS ${libsodium_salsa20_cloucc_benchmark_logs})

# foreach(pass IN ITEMS MitigatePass NoCalleeSavedRegistersPass FunctionLocalStacks Attributes)
#   list(APPEND PASS_FLAGS -Xclang -load -Xclang $<TARGET_FILE:${pass}>)
# endforeach()
# 
# FetchContent_GetProperties(Hacl SOURCE_DIR Hacl_SOURCE_DIR)
# set(Hacl_INCLUDE_DIRS ${Hacl_SOURCE_DIR}/dist/gcc-compatible ${Hacl_SOURCE_DIR}/dist/karamel/include ${Hacl_SOURCE_DIR}/dist/karamel/krmllib/dist/minimal)
# list(TRANSFORM Hacl_INCLUDE_DIRS PREPEND -I OUTPUT_VARIABLE Hacl_INCLUDE_FLAGS)
# 
# add_custom_command(OUTPUT Hacl_Chacha20.o
#   COMMAND ${LLVM_BINARY_DIR}/bin/clang -flegacy-pass-manager -fPIE ${Hacl_INCLUDE_FLAGS} -mllvm -clou -mllvm -no-stack-slot-sharing ${PASS_FLAGS} ${Hacl_SOURCE_DIR}/dist/gcc-compatible/Hacl_Chacha20.c -c -o Hacl_Chacha20.o
# )
# 
# add_executable(Hacl_Chacha20_benchmark Hacl_Chacha20_bench.cc Hacl_Chacha20.o)
# target_link_libraries(Hacl_Chacha20_benchmark PRIVATE benchmark::benchmark)
# target_include_directories(Hacl_Chacha20_benchmark PRIVATE ${Hacl_INCLUDE_DIRS})

include(HaclBenchmark)

function(hacl_benchmarks BASENAME BENCHSRC SRC)
  hacl_benchmark(${BASENAME}_none_benchmark ${BENCHSRC} ${SRC}
    CFLAGS -O3 -fno-stack-protector
  )
  hacl_benchmark(${BASENAME}_lfence+retpoline+ssbd_benchmark ${BENCHSRC} ${SRC}
    ${baseline_lfence_retpoline_ssbd_args}
    CFLAGS -O3 -fno-stack-protector
  )
  hacl_benchmark(${BASENAME}_slh+retpoline+ssbd_benchmark ${BENCHSRC} ${SRC}
    ${baseline_slh_retpoline_ssbd_args}
    CFLAGS -O3 -fno-stack-protector
  )
  set(logs ${CMAKE_CURRENT_BINARY_DIR}/${BASENAME}_cloucc_benchmark_logs)
  make_directory(${logs})
  hacl_benchmark(${BASENAME}_cloucc_benchmark ${BENCHSRC} ${SRC}
    ${cloucc_args}
    CFLAGS -O3 -fno-stack-protector
    LLVMFLAGS -clou-log=${logs}
  )
  set_target_properties(${BASENAME}_cloucc_benchmark PROPERTIES CLOU_LOGS ${logs})
endfunction()

hacl_benchmarks(hacl_chacha20 Hacl_Chacha20_bench.cc Hacl_Chacha20.c)
hacl_benchmarks(hacl_poly1305 Hacl_Poly1305_bench.cc Hacl_Poly1305_32.c)
hacl_benchmarks(hacl_curve25519 Hacl_Curve25519_bench.cc Hacl_Curve25519_51.c)

file(GENERATE OUTPUT table.json
  INPUT table.json.in
)

configure_file(table.py.in table.py)


make_directory(${CMAKE_CURRENT_BINARY_DIR}/hacl_curve25519_cloucc_benchmark_logs)
hacl_benchmark(hacl_curve25519_cloucc_benchmark_dbg Hacl_Curve25519_bench.cc Hacl_Curve25519_51.c
  ${cloucc_args}
  CFLAGS -O3 -fno-stack-protector -g
  LDFLAGS -Wl,-rpath,$<TARGET_FILE_DIR:trace_runtime> -ltrace_runtime -L$<TARGET_FILE_DIR:trace_runtime>
  LLVMFLAGS -clou-log=${CMAKE_CURRENT_BINARY_DIR}/hacl_curve25519_cloucc_benchmark_logs -clou-whitelist=point_add_and_double,point_double
  PASSES TracePass
  DEPENDS trace_runtime
)

hacl_benchmark(hacl_curve25519_cloucc_benchmark_test Hacl_Curve25519_bench.cc Hacl_Curve25519_51.c
  ${cloucc_args}
  CFLAGS -O3 -fno-stack-protector -finline-functions
)


function(register_benchmark NAME FUNC)
  add_custom_command(OUTPUT ${NAME}_stats.json
    COMMAND ${Python3_EXECUTABLE} stats.py --logs $<TARGET_PROPERTY:${NAME},CLOU_LOGS> --func ${FUNC} --json ${CMAKE_CURRENT_SOURCE_DIR}/stats.json | clang-format --assume-filename=.json > ${NAME}_stats.json
    DEPENDS ${Python3_EXECUTABLE} ${NAME} stats.py stats.json
  )
  add_custom_target(${NAME}_stats ALL DEPENDS ${NAME}_stats.json stats.json)
endfunction()

register_benchmark(libsodium_salsa20_cloucc_benchmark "crypto_core_salsa20")
register_benchmark(libsodium_sha256_cloucc_benchmark "crypto_hash_sha256")
register_benchmark(hacl_chacha20_cloucc_benchmark "Hacl_Chacha20_chacha20_encrypt")
register_benchmark(hacl_poly1305_cloucc_benchmark "Hacl_Poly1305_32_poly1305_mac")
register_benchmark(hacl_curve25519_cloucc_benchmark "Hacl_Curve25519_51_ecdh")

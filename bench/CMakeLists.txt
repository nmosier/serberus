include(BenchmarkFlags)

add_link_options(-fPIE -fPIC -pthread)

configure_file(breakdown.py.in breakdown.py)

foreach(benchspec IN ITEMS baseline_breakdown cloucc_breakdown)
  file(GENERATE
    OUTPUT ${benchspec}.json
    INPUT ${benchspec}.json.in
  )
endforeach()

function(add_benchmark NAME SRC LIB INC)
  add_executable(${NAME} ${SRC})
  target_link_libraries(${NAME} PRIVATE ${LIB} benchmark::benchmark)
  add_custom_command(OUTPUT ${NAME}.json
    COMMAND ${NAME} --benchmark_out=${NAME}.json --benchmark_out_format=json
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
)

foreach(libsodium_benchmark_library IN LISTS libsodium_benchmark_libraries)
  # ExternalProject_Get_Property(${libsodium_benchmark_library} install_dir)
  string(REGEX REPLACE "^libsodium_" "" name ${libsodium_benchmark_library})
  add_benchmark(${name}_benchmark libsodium_sha256.cc ${libsodium_benchmark_library} ${install_dir}/include)
endforeach()

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
  hacl_benchmark(${BASENAME}_cloucc_benchmark ${BENCHSRC} ${SRC}
    ${cloucc_args}
    CFLAGS -O3 -fno-stack-protector
  )
  hacl_benchmark(${BASENAME}_udt_benchmark ${BENCHSRC} ${SRC}
    ${cloucc_udt_args}
    CFLAGS -O3 -fno-stack-protector
  )
  hacl_benchmark(${BASENAME}_ncas_benchmark ${BENCHSRC} ${SRC}
    ${cloucc_ncas_args}
    CFLAGS -O3 -fno-stack-protector
  )
endfunction()

hacl_benchmarks(hacl_chacha20 Hacl_Chacha20_bench.cc Hacl_Chacha20.c)
hacl_benchmarks(hacl_poly1305 Hacl_Poly1305_bench.cc Hacl_Poly1305_32.c)
hacl_benchmarks(hacl_curve25519 Hacl_Curve25519_bench.cc Hacl_Curve25519_51.c)

# Generate table
set(table_json ${CMAKE_CURRENT_BINARY_DIR}/table.json)

function(table_init FILENAME)
  file(REMOVE ${FILENAME}.in)
  file(APPEND ${FILENAME}.in "[")
endfunction()

function(table_finish FILENAME)
  file(APPEND ${FILENAME}.in "]")
  file(GENERATE OUTPUT ${FILENAME} INPUT ${FILENAME}.in)
endfunction()

function(table_add FILENAME NAME BASENAME SIZEUNIT)
  list(JOIN ARGN ", " SIZES)

  file(APPEND ${FILENAME}.in "
  {
    \"name\": \"${NAME}\",
    \"sizes\": [${SIZES}],
    \"sizeunit\": \"${SIZEUNIT}\",
    \"defenses\": [
      {
        \"name\": \"none\",
        \"bin\": \"$<TARGET_FILE:${BASENAME}_none_benchmark>\"
      },
      {
        \"name\": \"lfence+retpoline+ssbd\",
        \"bin\": \"$<TARGET_FILE:${BASENAME}_lfence+retpoline+ssbd_benchmark>\"
      },
      {
        \"name\": \"slh+retpoline+ssbd\",
        \"bin\": \"$<TARGET_FILE:${BASENAME}_slh+retpoline+ssbd_benchmark>\"
      },
      {
        \"name\": \"\\\\tool{}\",
        \"bin\": \"$<TARGET_FILE:${BASENAME}_cloucc_benchmark>\"
      }
    ]
  }
")
endfunction()

function(table_add1 FILENAME)
  file(APPEND ${FILENAME}.in ",")
  table_add(${FILENAME} ${ARGN})
endfunction()

table_init(${table_json})
table_add(${table_json} "Salsa20 (libsodium)" "libsodium_salsa20" "bytes" 64)
table_add1(${table_json} "SHA-256 (libsodium)" "libsodium_sha256" "bytes" 64 8192)
table_add1(${table_json} "ChaCha20 (HACL*)" "hacl_chacha20" "bytes" 8192)
table_add1(${table_json} "Poly1305 (HACL*)" "hacl_poly1305" "bytes" 1024 8192)
table_add1(${table_json} "ECDH Curve25519 (HACL*)" "hacl_curve25519" "bytes" 0)
table_finish(${table_json})

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

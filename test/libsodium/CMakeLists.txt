get_filename_component(LIBSODIUM_DIR ${LIBSODIUM_DIR} ABSOLUTE BASE_DIR ${CMAKE_BINARY_DIR})

function(libsodium_library NAME)
  set(multi_value_args DEPENDS PASS CPPFLAGS CFLAGS LDFLAGS LLVMFLAGS)
  cmake_parse_arguments(LIBSODIUM "" "" "${multi_value_args}" ${ARGN})

  # Add arguments for pass
  list(APPEND LIBSODIUM_CPPFLAGS -flegacy-pass-manager)
  
  foreach(pass IN LISTS LIBSODIUM_PASS)
    list(APPEND LIBSODIUM_CPPFLAGS -Xclang -load -Xclang $<TARGET_FILE:${pass}>)
    list(APPEND LIBSODIUM_DEPENDS ${pass})
  endforeach()

  foreach(llvmflag IN LISTS LIBSODIUM_LLVMFLAGS)
    list(APPEND LIBSODIUM_CFLAGS -mllvm ${llvmflag})
  endforeach()
  
  list(APPEND LIBSODIUM_CFLAGS "-g")
  list(JOIN LIBSODIUM_CFLAGS " " LIBSODIUM_CFLAGS)
  list(JOIN LIBSODIUM_CPPFLAGS " " LIBSODIUM_CPPFLAGS)
  list(JOIN LIBSODIUM_LDFLAGS " " LIBSODIUM_LDFLAGS)

  ExternalProject_Add(${NAME}
    # URL https://download.libsodium.org/libsodium/releases/libsodium-1.0.18.tar.gz
    SOURCE_DIR ${LIBSODIUM_DIR}
    CONFIGURE_COMMAND ${LIBSODIUM_DIR}/configure --quiet --prefix=${CMAKE_CURRENT_BINARY_DIR}/${NAME}-prefix CC=${LLVM_BINARY_DIR}/bin/clang "CPPFLAGS=${LIBSODIUM_CPPFLAGS}" "CFLAGS=${LIBSODIUM_CFLAGS}" "LDFLAGS=${LIBSODIUM_LDFLAGS}"
    BUILD_COMMAND ${MAKE_EXE} --quiet; ${MAKE_EXE} --quiet check || true # TESTS_ENVIRONMENT="true" # Don't actually run any tests; just want to build the runners.
    # BUILD_ALWAYS ON
    INSTALL_COMMAND ${MAKE_EXE} --quiet install
  )
  ExternalProject_Get_Property(${NAME} BINARY_DIR)
  ExternalProject_Add_Step(${NAME} clean
    COMMAND make --quiet clean
    DEPENDEES configure
    DEPENDERS build
    WORKING_DIRECTORY ${BINARY_DIR}
  )
  ExternalProject_Add_StepDependencies(${NAME} clean ${LIBSODIUM_DEPENDS}) # TODO: Merge this into Add_Step above?
endfunction()

### Correctness tests ###
function(libsodium_correctness_test NAME)
  set(multi_value_args DEPENDS PASS)
  cmake_parse_arguments(LIBSODIUM "" "" "${multi_value_args}" ${ARGN})
  libsodium_library(${NAME}
    DEPENDS ${LIBSODIUM_DEPENDS}
    PASS ${LIBSODIUM_PASS}
  )
  add_test(NAME ${NAME}
    COMMAND ${MAKE_EXE} check 
    WORKING_DIRECTORY ${BINARY_DIR}
  )
endfunction()


### Bitcode Library ###
function(libsodium_bc NAME OUTPUT)
  set(libsodium_lib ${NAME}_lib)
  libsodium_library(${libsodium_lib} ${ARGN}
    CFLAGS "-flto"
    LDFLAGS "-flto"
  )
  ExternalProject_Get_Property(${libsodium_lib} INSTALL_DIR)
  set(STATIC_LIB ${INSTALL_DIR}/lib/libsodium.a)
  set(LINK_SH ${CMAKE_BINARY_DIR}/scripts/link.sh)
  add_custom_command(OUTPUT ${OUTPUT}
    COMMAND ${LINK_SH} ${STATIC_LIB} > ${OUTPUT}
    DEPENDS ${libsodium_lib} ${LINK_SH}
  )
  add_custom_target(${NAME}
    DEPENDS ${OUTPUT}
  )
endfunction()

libsodium_correctness_test(libsodium_correctness_noop)

foreach(PASS IN ITEMS ZeroFillCalls SpeculativeTaint)
  libsodium_correctness_test(libsodium_correctness_${PASS}
    PASS ${PASS}
  )
endforeach()
libsodium_correctness_test(libsodium_correctness_SpectreV1
  PASS SpectreV1_1 LfencePass
)

# libsodium_library(libsodium_profile_SpectreV11
#   PASS SpectreV1_1 BreakPass
# )


# Profiling Tests
function(libsodium_profile NAME)
  set(multi_value_args DEPENDS PASS CFLAGS)
  cmake_parse_arguments(LIBSODIUM "" "" "${multi_value_args}" ${ARGN})
  list(APPEND LIBSODIUM_PASS BreakPass)
  libsodium_library(${NAME}
    DEPENDS ${LIBSODIUM_DEPENDS}
    PASS ${LIBSODIUM_PASS} BreakPass
    CFLAGS ${LIBSODIUM_CFLAGS}
  )

  # Now, iterate over tests
  file(GLOB TEST_SRCS ${LIBSODIUM_DIR}/test/default/*.c)
  set(LFENCE_LOGS)
  foreach(TEST_SRC IN LISTS TEST_SRCS)
    get_filename_component(TEST_BASENAME ${TEST_SRC} NAME_WLE)
    set(TEST_FULLNAME ${NAME}_${TEST_BASENAME})
    set(LFENCES_LOG ${TEST_FULLNAME}_lfences.log)
    set(TEST_EXE ${NAME}-prefix/src/${NAME}-build/test/default/.libs/${TEST_BASENAME})
    add_custom_target(${TEST_FULLNAME}
      COMMAND lfence_profiler -o ${LFENCES_LOG} -b -m pc -x -- ${TEST_EXE}
      DEPENDS lfence_profiler ${NAME}
    )
    list(APPEND LFENCE_LOGS ${TEST_FULLNAME})
  endforeach()

  # Merge logs
  set(LFENCE_SUMMARY_LOG ${NAME}_lfences.log)
  set(MERGE_LFENCES_AWK ${CMAKE_SOURCE_DIR}/scripts/merge_lfences.awk)
  add_custom_command(OUTPUT ${LFENCE_SUMMARY_LOG}
    COMMAND ${CAT_EXE} ${LFENCE_LOGS} | ${AWK_EXE} -f ${MERGE_LFENCES_AWK} > ${LFENCE_SUMMARY_LOG}
    DEPENDS ${MERGE_LFENCE_AWK} ${LFENCE_LOGS}
  )
  add_custom_target(${NAME}_lfences ALL DEPENDS ${LFENCE_SUMMARY_LOG})

  # Symbolize logs
  set(LFENCE_LINES_LOG ${NAME}_lfences_src.log)
  set(SYMBOLIZE_SH ${CMAKE_SOURCE_DIR}/scripts/srclocs.sh)
  add_custom_command(OUTPUT ${LFENCE_LINES_LOG}
    COMMAND ${SYMBOLIZE_SH} ${NAME}-prefix/lib/libsodium.so < ${LFENCE_SUMMARY_LOG} > ${LFENCE_LINES_LOG}
    DEPENDS ${LFENCE_SUMMARY_LOG} ${SYMBOLIZE_SH} ${NAME}
  )
  add_custom_target(${NAME}_lfence_src ALL DEPENDS ${LFENCE_LINES_LOG})
endfunction()

function(libsodium_trace NAME)
  libsodium_library(${NAME}_lib ${ARGN})

  set(LIB ${NAME}_lib)
  ExternalProject_Get_Property(${LIB} BINARY_DIR)
  ExternalProject_Get_Property(${LIB} INSTALL_DIR)
  set(LOG ${NAME}.log)
  set(SYMBOLIZE_SH ${CMAKE_SOURCE_DIR}/scripts/srclocs.sh)
  set(MERGE_COUNTS_SH ${CMAKE_SOURCE_DIR}/scripts/merge_counts.sh)
  add_custom_command(OUTPUT ${LOG}
    COMMAND find ${BINARY_DIR}/test/default/ -name *.log -exec cat "{}" ";" | grep "^[[:digit:]]\\+[[:space:]]" | ${MERGE_COUNTS_SH} | ${SYMBOLIZE_SH} ${INSTALL_DIR}/lib/libsodium.so > ${LOG}
    DEPENDS ${SYMBOLIZE_SH} ${LIB}  ${MERGE_COUNTS_SH}
    VERBATIM
  )
  add_custom_target(${NAME} DEPENDS ${LOG})
  
endfunction()

libsodium_profile(libsodium_profile_SpectreV11
  PASS SpectreV1_1
)

libsodium_library(libsodium_baseline_lfence
  CFLAGS "-mllvm -x86-speculative-load-hardening -mllvm -x86-slh-lfence -mretpoline -O3"
)

libsodium_library(libsodium_baseline_slh
  CFLAGS "-mllvm -x86-speculative-load-hardening -mretpoline -O3"
)

libsodium_library(libsodium_bcbs_secret
  PASS LoopIndexPass SpectreBCBSSecretPass NoSpillLowering LfencePass
  CFLAGS "-O3 -mllvm -clou-nospill -mllvm -clou-nospill-max=8 -mllvm -exhaustive-register-search"
)

set(TRACE_RUNTIME_DIR ${CMAKE_BINARY_DIR}/tools)

libsodium_trace(libsodium_bcbs_secret_trace
  PASS LoopIndexPass SpectreBCBSSecretPass NoSpillLowering TracePass
  CFLAGS "-O3 -mllvm -clou-nospill -mllvm -clou-nospill-max=8 -mllvm -exhaustive-register-search"
  LDFLAGS "-Wl,-rpath,${TRACE_RUNTIME_DIR} -L${TRACE_RUNTIME_DIR} -ltrace_runtime"
)

libsodium_bc(libsodium_bc libsodium.bc
  CFLAGS "-O1"
)

libsodium_library(libsodium_cloucc
  PASS LoopIndexPass SpectreBCBSSecretPass SpeculativeTaint NoSpillLowering LfencePass
  CFLAGS -O3
  LLVMFLAGS -clou-nospill -clou-nospill-max=8 -exhaustive-register-search
)

libsodium_trace(libsodium_cloucc_trace
  PASS LoopIndexPass SpectreBCBSSecretPass SpeculativeTaint NoSpillLowering TracePass
  CFLAGS -O3
  LLVMFLAGS -clou-nospill -clou-nospill-max=8 -exhaustive-register-search
  LDFLAGS -Wl,-rpath,${TRACE_RUNTIME_DIR} -L${TRACE_RUNTIME_DIR} -ltrace_runtime
)

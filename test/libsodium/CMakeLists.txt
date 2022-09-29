include(LibsodiumLibrary)

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
  add_custom_command(OUTPUT ${LFENCE_SUMMARY_LOG}
    COMMAND ${CAT_EXE} ${LFENCE_LOGS} | ${AWK_EXE} -f ${MERGE_COUNT_PY} > ${LFENCE_SUMMARY_LOG}
    DEPENDS ${MERGE_LFENCE_AWK} ${LFENCE_LOGS}
  )
  add_custom_target(${NAME}_lfences DEPENDS ${LFENCE_SUMMARY_LOG})

  # Symbolize logs
  set(LFENCE_LINES_LOG ${NAME}_lfences_src.log)
  add_custom_command(OUTPUT ${LFENCE_LINES_LOG}
    COMMAND ${SYMBOLIZE_SH} ${NAME}-prefix/lib/libsodium.so < ${LFENCE_SUMMARY_LOG} > ${LFENCE_LINES_LOG}
    DEPENDS ${LFENCE_SUMMARY_LOG} ${SYMBOLIZE_SH} ${NAME}
  )
  add_custom_target(${NAME}_lfence_src DEPENDS ${LFENCE_LINES_LOG})
endfunction()

function(libsodium_trace NAME)
  set(TRACE_RUNTIME_DIR ${CMAKE_BINARY_DIR}/tools)
  
  libsodium_library(${NAME}_lib ${ARGN}
    PASS TracePass # ensure this comes after all the passes above
    LDFLAGS -Wl,-rpath,${TRACE_RUNTIME_DIR} -L${TRACE_RUNTIME_DIR} -ltrace_runtime    
    DEPENDS trace_runtime
  )

  set(LIB ${NAME}_lib)
  ExternalProject_Get_Property(${LIB} BINARY_DIR)
  ExternalProject_Get_Property(${LIB} INSTALL_DIR)
  set(LINES_LOG ${NAME}_lines.log)
  set(FUNCS_LOG ${NAME}_funcs.log)
  set(MERGE_COUNTS_SH ${CMAKE_SOURCE_DIR}/scripts/merge_counts.sh)
  set(FUNCS_AWK ${CMAKE_SOURCE_DIR}/scripts/trace_function_totals.awk)
  add_custom_command(OUTPUT ${LINES_LOG}
    COMMAND find ${BINARY_DIR}/test/default/ -name *.log -exec cat "{}" ";" | grep "^[[:digit:]]\\+[[:space:]]" | ${Python3_EXECUTABLE} ${MERGE_COUNTS_PY} -t1 | ${SYMBOLIZE_SH} -t2 ${INSTALL_DIR}/lib/libsodium.so | sort -k1 "-t " -n > ${LINES_LOG}
    DEPENDS ${SYMBOLIZE_SH} ${LIB} ${MERGE_COUNTS_SH} ${Python3_EXECUTABLE}
    VERBATIM
  )
  add_custom_command(OUTPUT ${FUNCS_LOG}
    COMMAND ${AWK_EXE} -f ${FUNCS_AWK} ${LINES_LOG} | sort -k1 -n "-t " > ${FUNCS_LOG}
    DEPENDS ${FUNCS_AWK} ${LINES_LOG}
    VERBATIM
  )
  add_custom_target(${NAME} DEPENDS ${LINES_LOG} ${FUNCS_LOG})
  
endfunction()

libsodium_library(libsodium_baseline_none
  CFLAGS -O3
)

libsodium_library(libsodium_baseline_lfence
  PASS BaselinePass
  CFLAGS -O3
  LLVMFLAGS -x86-speculative-load-hardening -x86-slh-lfence -x86-slh-fence-call-and-ret
)

libsodium_library(libsodium_baseline_slh
  CFLAGS -O3 -mretpoline
  LLVMFLAGS -x86-speculative-load-hardening
)

libsodium_bc(libsodium_bc_O1 libsodium.O1.bc
  CFLAGS -O1
)

libsodium_bc(libsodium_bc_O3 libsodium.O3.bc
  CFLAGS -O3
)

libsodium_library(libsodium_mitigate
  PASS MitigatePass NoSpillPublic NoSpillLowering
  CFLAGS -O3
  LLVMFLAGS -clou-nospill -exhaustive-register-search
)

libsodium_trace(libsodium_mitigate_trace
  PASS MitigatePass
  CFLAGS -O3
)

# libsodium_library(libsodium_mitigate2
#   PASS MitigatePass
#   CFLAGS -O3
# )
# 
# libsodium_trace(libsodium_mitigate2_trace
#   PASS MitigatePass
#   CFLAGS -O3
# )

libsodium_library(libsodium_fps
  PASS FunctionLocalStacks
  CFLAGS -O3 -mno-red-zone
  LLVMFLAGS -x86-function-local-stacks -no-stack-slot-sharing
)

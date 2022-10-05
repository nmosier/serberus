set(MERGE_COUNTS_PY ${CMAKE_SOURCE_DIR}/scripts/merge_counts.py)
set(SYMBOLIZE_SH ${CMAKE_SOURCE_DIR}/scripts/srclocs2.sh)

function(libsodium_library NAME)
  set(multi_value_args DEPENDS PASS CPPFLAGS CFLAGS LDFLAGS LLVMFLAGS CLOUFLAGS CONFIGURE_OPTIONS)
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

  foreach(clouflag IN LISTS LIBSODIUM_CLOUFLAGS)
    list(APPEND LIBSODIUM_CPPFLAGS -mllvm ${clouflag})
  endforeach()

  list(APPEND LIBSODIUM_LDFLAGS -pthread)
  
  list(APPEND LIBSODIUM_CFLAGS "-g")
  list(JOIN LIBSODIUM_CFLAGS " " LIBSODIUM_CFLAGS)
  list(JOIN LIBSODIUM_CPPFLAGS " " LIBSODIUM_CPPFLAGS)
  list(JOIN LIBSODIUM_LDFLAGS " " LIBSODIUM_LDFLAGS)

  ExternalProject_Add(${NAME}
    # URL https://download.libsodium.org/libsodium/releases/libsodium-1.0.18.tar.gz
    SOURCE_DIR ${LIBSODIUM_DIR}
    CONFIGURE_COMMAND ${LIBSODIUM_DIR}/configure --quiet "--prefix=${CMAKE_CURRENT_BINARY_DIR}/${NAME}-prefix" "CC=${LLVM_BINARY_DIR}/bin/clang" "LD=${LLVM_BINARY_DIR}/bin/ld.lld" "CPPFLAGS=${LIBSODIUM_CPPFLAGS}" "CFLAGS=${LIBSODIUM_CFLAGS}" "LDFLAGS=${LIBSODIUM_LDFLAGS}" ${LIBSODIUM_CONFIGURE_OPTIONS}
    BUILD_COMMAND ${MAKE_EXE} -j64 2>&1 | tee ${NAME}.build.log && ${MAKE_EXE} -j64 check  # TESTS_ENVIRONMENT="true" # Don't actually run any tests; just want to build the runners.
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

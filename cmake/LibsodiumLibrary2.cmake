set(MERGE_COUNTS_PY ${CMAKE_SOURCE_DIR}/scripts/merge_counts.py)
set(SYMBOLIZE_SH ${CMAKE_SOURCE_DIR}/scripts/srclocs2.sh)

function(libsdoium_library NAME)
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

  set(PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/${NAME}
  set(BUILD_DIR ${PREFIX_DIR}/build)
  set(INSTALL_DIR ${PREFIX_DIR})
  set(STAMP_FILE ${PREFIX_DIR}/stamp)

  # configure command
  add_custom_command(OUTPUT ${BUILD_DIR}/Makefile
    COMMAND ${LIBSODIUM_DIR}/configure --quiet --prefix=${CMAKE_CURRENT_BINARY_DIR}/${NAME}-prefix CC=${LLVM_BINARY_DIR}/bin/clang "CPPFLAGS=${LIBSODIUM_CPPFLAGS}" "CFLAGS=${LIBSODIUM_CFLAGS}" "LDFLAGS=${LIBSODIUM_LDFLAGS}" ${LIBSODIUM_CONFIGURE_OPTIONS}
    DEPENDS ${LIBSODIUM_DEPENDS}
    COMMENT "Configuring libsodium library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
  )

  # clean step
  add_custom_command(OUTPUT ${STAMP_FILE}
    COMMAND make --quiet clean && touch ${STAMP_FILE}
    DEPENDS ${BUILD_DIR}/Makefile
    COMMENT "Cleaning libsodium library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
  )

  # build step
  add_custom_command(OUTPUT ${BUILD_DIR}/src/libsodium/.libs/libsodium.a
    COMMAND make -j64 && make check -j64
    DEPENDS ${BUILD_DIR}
    
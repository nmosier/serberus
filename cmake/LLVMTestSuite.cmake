function(llvm_test_suite NAME)
  set(multi_value_args DEPENDS PASS CPPFLAGS CFLAGS LDFLAGS LLVMFLAGS CLOUFLAGS BUILD_TYPE CMAKE_OPTIONS)
  cmake_parse_arguments(TEST "" "" "${multi_value_args}" ${ARGN})
  
  set(PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/${NAME})
  set(BUILD_DIR ${PREFIX_DIR}/build)
  set(STAMP_DIR ${PREFIX_DIR}/stamp)
  set(INSTALL_DIR ${PREFIX_DIR})
  set(LOG_DIR ${PREFIX_DIR}/logs)

  make_directory(${INSTALL_DIR}/include)
  make_directory(${BUILD_DIR})
  make_directory(${LOG_DIR})

  # Use ld.lld
  list(APPEND TEST_LDFLAGS -fuse-ld=${LLVM_BINARY_DIR}/bin/ld.lld)

  # TODO: log
  list(APPEND TEST_LLVMFLAGS -clou-log=${LOG_DIR})

  # Add arguments for pass
  list(APPEND TEST_CPPFLAGS -flegacy-pass-manager)

  foreach(pass IN LISTS TEST_PASS)
    list(APPEND TEST_CPPFLAGS -Xclang -load -Xclang $<TARGET_FILE:${pass}>)
    list(APPEND TEST_DEPENDS ${pass})
  endforeach()

  foreach(llvmflag IN LISTS TEST_LLVMFLAGS)
    list(APPEND TEST_CFLAGS -mllvm ${llvmflag})
  endforeach()

  foreach(clouflag IN LISTS TEST_CLOUFLAGS)
    list(APPEND TEST_CPPFLAGS -mllvm ${clouflag})
  endforeach()

  list(APPEND TEST_CMAKE_OPTIONS -DCMAKE_BUILD_TYPE=Release)
  foreach(build_type IN LISTS TEST_BUILD_TYPE)
    list(APPEND TEST_CMAKE_OPTIONS -DCMAKE_BUILD_TYPE=${build_type})
  endforeach()

  list(APPEND TEST_LDFLAGS -pthread)

  list(JOIN TEST_CFLAGS " " TEST_CFLAGS)
  list(JOIN TEST_CPPFLAGS " " TEST_CPPFLAGS)
  list(JOIN TEST_LDFLAGS " " TEST_LDFLAGS)

  # configure command
  add_custom_command(OUTPUT ${STAMP_DIR}/configure.stamp
    COMMAND cmake ${LLVM_TEST_SUITE_DIR} -DCMAKE_INSTALL_PREFIX=${PREFIX_DIR} -DCMAKE_C_COMPILER=${LLVM_BINARY_DIR}/bin/clang -DCMAKE_CXX_COMPILER=${LLVM_BINARY_DIR}/bin/clang++
    "-DCMAKE_C_FLAGS=${TEST_CPPFLAGS} ${TEST_CFLAGS} ${TEST_LDFLAGS}"
    "-DCMAKE_CXX_FLAGS=${TEST_CPPFLAGS} ${TEST_CFLAGS} ${TEST_LDFLAGS}" # for now just use CFLAGS for CXXFLAGS
    ${TEST_CMAKE_OPTIONS}
    COMMAND touch ${STAMP_DIR}/configure.stamp
    COMMENT "Configuring llvm-test-suite ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
    DEPENDS ${TEST_DEPENDS} ${LLVM_BINARY_DIR}/bin/clang ${LLVM_BINARY_DIR}/bin/ld.lld
  )

  add_custom_target(${NAME}_configure
    DEPENDS ${STAMP_DIR}/configure.stamp
  )

endfunction()

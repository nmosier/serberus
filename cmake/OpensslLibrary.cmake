function(openssl_library NAME)
  set(multi_value_args DEPENDS PASS CPPFLAGS CFLAGS LDFLAGS LLVMFLAGS CLOUFLAGS CONFIGURE_OPTIONS)
  cmake_parse_arguments(OPENSSL "" "" "${multi_value_args}" ${ARGN})

  set(PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/${NAME})
  set(BUILD_DIR ${PREFIX_DIR}/build)
  set(STAMP_DIR ${PREFIX_DIR}/stamp)
  set(INSTALL_DIR ${PREFIX_DIR})
  set(LOG_DIR ${PREFIX_DIR}/logs)

  make_directory(${BUILD_DIR})
  make_directory(${LOG_DIR})
  make_directory(${INSTALL_DIR}/include)

  list(APPEND OPENSSL_CFLAGS -flegacy-pass-manager)

  list(APPEND OPENSSL_CONFIGURE_OPTIONS no-threads)

  list(APPEND OPENSSL_LLVMFLAGS -clou-log=${LOG_DIR})

  foreach(pass IN LISTS OPENSSL_PASS)
    set(flags -Xclang -load -Xclang $<TARGET_FILE:${pass}>)
    list(APPEND OPENSSL_CFLAGS ${flags})
    list(APPEND OPENSSL_LDFLAGS ${flags})
    list(APPEND OPENSSL_DEPENDS ${pass})
  endforeach()

  foreach(llvmflag IN LISTS OPENSSL_LLVMFLAGS)
    list(APPEND OPENSSL_CFLAGS -mllvm ${llvmflag})
  endforeach()

  foreach(clouflag IN LISTS OPENSSL_CLOUFLAGS)
    list(APPEND OPENSSL_CFLAGS -mllvm ${llvmflag})
  endforeach()

  list(APPEND OPENSSL_LDFLAGS -pthread) # Not sure why
  
  list(JOIN OPENSSL_CFLAGS " " OPENSSL_CFLAGS)
  list(JOIN OPENSSL_CPPFLAGS " " OPENSSL_CPPFLAGS)
  list(JOIN OPENSSL_LDFLAGS " " OPENSSL_LDFLAGS)

  add_custom_command(OUTPUT ${STAMP_DIR}/configure.stamp
    COMMAND ${OPENSSL_DIR}/Configure --prefix=${INSTALL_DIR} CC=${LLVM_BINARY_DIR}/bin/clang LD=${LLVM_BINARY_DIR}/bin/ld.lld "CPPFLAGS=${OPENSSL_CPPFLAGS}" "CFLAGS=${OPENSSL_CFLAGS}" "LDFLAGS=${OPENSSL_LDFLAGS}" ${OPENSSL_CONFIGURE_OPTIONS} RANLIB=${LLVM_BINARY_DIR}/bin/llvm-ranlib
    COMMAND touch ${STAMP_DIR}/configure.stamp
    COMMENT "Configuring OpenSSL library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
    DEPENDS ${OPENSSL_DEPENDS}
  )

  # clean step
  add_custom_command(OUTPUT ${STAMP_DIR}/clean.stamp
    COMMAND make --quiet clean
    COMMAND touch ${STAMP_DIR}/clean.stamp
    DEPENDS ${STAMP_DIR}/configure.stamp
    COMMENT "Cleaning OpenSSL library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
  )

  # build step
  add_custom_command(OUTPUT ${STAMP_DIR}/build.stamp
    COMMAND make --quiet -j16
    COMMAND touch ${STAMP_DIR}/build.stamp
    DEPENDS ${STAMP_DIR}/clean.stamp
    COMMENT "Building OpenSSL library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
  )

  # test step
  add_custom_command(OUTPUT ${STAMP_DIR}/test.stamp
    # COMMAND make --quiet test
    COMMAND touch ${STAMP_DIR}/test.stamp
    DEPENDS ${STAMP_DIR}/build.stamp
    COMMENT "Testing OpenSSL library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
  )

  # install step
  add_custom_command(OUTPUT ${STAMP_DIR}/install.stamp ${INSTALL_DIR}/lib64/libssl.a ${INSTALL_DIR}/lib64/libcrypto.a
    COMMAND make --quiet install
    COMMAND touch ${STAMP_DIR}/install.stamp
    DEPENDS ${STAMP_DIR}/test.stamp
    COMMENT "Installing OpenSSL library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
  )

  add_custom_target(${NAME}_install ALL
    DEPENDS ${STAMP_DIR}/install.stamp
  )

  add_library(${NAME} INTERFACE)
  target_link_libraries(${NAME} INTERFACE ${INSTALL_DIR}/lib64/libssl.a ${INSTALL_DIR}/lib64/libcrypto.a)
  target_include_directories(${NAME} INTERFACE ${INSTALL_DIR}/include)
  add_dependencies(${NAME} ${NAME}_install)
  
endfunction()

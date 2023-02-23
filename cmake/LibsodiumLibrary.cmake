set(MERGE_COUNTS_PY ${CMAKE_SOURCE_DIR}/scripts/merge_counts.py)
set(SYMBOLIZE_SH ${CMAKE_SOURCE_DIR}/scripts/srclocs2.sh)

function(libsodium_library NAME)
  set(multi_value_args DEPENDS PASS CPPFLAGS CFLAGS LDFLAGS LLVMFLAGS CLOUFLAGS CONFIGURE_OPTIONS)
  cmake_parse_arguments(LIBSODIUM "" "" "${multi_value_args}" ${ARGN})

  FetchContent_GetProperties(Libsodium SOURCE_DIR Libsodium_SOURCE_DIR)
  set(SOURCE_DIR ${Libsodium_SOURCE_DIR})
  set(PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/${NAME})
  set(BUILD_DIR ${PREFIX_DIR}/build)
  set(STAMP_DIR ${PREFIX_DIR}/stamp)
  set(INSTALL_DIR ${PREFIX_DIR})
  set(LOG_DIR ${PREFIX_DIR}/logs)

  make_directory(${INSTALL_DIR}/include)
  make_directory(${BUILD_DIR})
  make_directory(${LOG_DIR})

  # Use ld.lld
  list(APPEND LIBSODIUM_LDFLAGS -fuse-ld=${LLVM_BINARY_DIR}/bin/ld.lld)

  # TESTING -- CET
  if(0)
    list(APPEND LIBSODIUM_LDFLAGS -Wl,-rpath,$<TARGET_FILE_DIR:cet> -L$<TARGET_FILE_DIR:cet> -lcet)
    list(APPEND LIBSODIUM_DEPENDS cet)
  endif(0)
  list(APPEND LIBSODIUM_CFLAGS -fcf-protection=branch)


  # TODO: log
  list(APPEND LIBSODIUM_LLVMFLAGS -clou-log=${LOG_DIR})

  # Add arguments for pass
  list(APPEND LIBSODIUM_CPPFLAGS -flegacy-pass-manager)
  
  foreach(pass IN LISTS LIBSODIUM_PASS)
    set(flags -Xclang -load -Xclang $<TARGET_FILE:${pass}>)
    list(APPEND LIBSODIUM_CPPFLAGS ${flags})
    list(APPEND LIBSODIUM_DEPENDS ${pass})
  endforeach()

  foreach(llvmflag IN LISTS LIBSODIUM_LLVMFLAGS)
    list(APPEND LIBSODIUM_CFLAGS -mllvm ${llvmflag})
  endforeach()

  foreach(clouflag IN LISTS LIBSODIUM_CLOUFLAGS)
    list(APPEND LIBSODIUM_CPPFLAGS -mllvm ${clouflag})
  endforeach()

  list(APPEND LIBSODIUM_LDFLAGS -pthread)

  # Experimental: include sct.h
  # list(APPEND LIBSODIUM_CFLAGS -include "${LIBSODIUM_DIR}/sct.h")

  list(JOIN LIBSODIUM_CFLAGS " " LIBSODIUM_CFLAGS)
  list(JOIN LIBSODIUM_CPPFLAGS " " LIBSODIUM_CPPFLAGS)
  list(JOIN LIBSODIUM_LDFLAGS " " LIBSODIUM_LDFLAGS)
  
  # configure command
  add_custom_command(OUTPUT ${STAMP_DIR}/configure.stamp
    COMMAND ${SOURCE_DIR}/configure --quiet --prefix=${PREFIX_DIR} CC=${LLVM_BINARY_DIR}/bin/clang LD=${LLVM_BINARY_DIR}/bin/ld.lld "CPPFLAGS=${LIBSODIUM_CPPFLAGS}" "CFLAGS=${LIBSODIUM_CFLAGS}" "LDFLAGS=${LIBSODIUM_LDFLAGS}" ${LIBSODIUM_CONFIGURE_OPTIONS}
    COMMAND touch ${STAMP_DIR}/configure.stamp
    COMMENT "Configuring libsodium library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
    DEPENDS ${LIBSODIUM_DEPENDS} ${LLVM_BINARY_DIR}/bin/clang ${LLVM_BINARY_DIR}/bin/ld.lld
  )

  # clean step
  add_custom_command(OUTPUT ${STAMP_DIR}/clean.stamp
    COMMAND make --quiet clean
    COMMAND rm -rf ${LOG_DIR}/*
    COMMAND touch ${STAMP_DIR}/clean.stamp
    DEPENDS ${STAMP_DIR}/configure.stamp
    COMMENT "Cleaning libsodium library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
  )

  # build step
  add_custom_command(OUTPUT ${STAMP_DIR}/build.stamp #  ${BUILD_DIR}/build.log
    COMMAND make --quiet -j8 2>&1 # | tee ${BUILD_DIR}/build.log
    COMMAND touch ${STAMP_DIR}/build.stamp
    DEPENDS ${STAMP_DIR}/clean.stamp
    COMMENT "Building libsodium library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
  )

  # test step
  add_custom_command(OUTPUT ${STAMP_DIR}/test.stamp
    COMMAND make --quiet check
    COMMAND touch ${STAMP_DIR}/test.stamp
    DEPENDS ${STAMP_DIR}/build.stamp
    COMMENT "Testing libsodium library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
  )

  # install step
  add_custom_command(OUTPUT ${STAMP_DIR}/install.stamp ${INSTALL_DIR}/lib/libsodium.so ${INSTALL_DIR}/include/sodium.h
    COMMAND make --quiet install
    COMMAND touch ${STAMP_DIR}/install.stamp
    DEPENDS ${STAMP_DIR}/test.stamp
    COMMENT "Installing libsodium library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
  )

  add_custom_target(${NAME}_install
    DEPENDS ${STAMP_DIR}/install.stamp
  )

  add_library(${NAME} INTERFACE)
  target_link_libraries(${NAME} INTERFACE ${INSTALL_DIR}/lib/libsodium.so)
  target_include_directories(${NAME} INTERFACE ${INSTALL_DIR}/include)
  add_dependencies(${NAME} ${NAME}_install)

  set_target_properties(${NAME} PROPERTIES CLOU_LOGS ${LOG_DIR})

endfunction()

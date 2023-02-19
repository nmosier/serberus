function(add_configure_library LIBNAME LIBRARY HEADER SRCDIR NAME)
  set(multi_value_args DEPENDS PASS CPPFLAGS CFLAGS LDFLAGS LLVMFLAGS CLOUFLAGS CONFIGURE_OPTIONS)
  cmake_parse_arguments(ARG "" "" "${multi_value_args}" ${ARGN})

  set(PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/${NAME})
  set(BUILD_DIR ${PREFIX_DIR}/build)
  set(STAMP_DIR ${PREFIX_DIR}/stamp)
  set(INSTALL_DIR ${PREFIX_DIR})
  set(LOG_DIR ${PREFIX_DIR}/logs)

  make_directory(${INSTALL_DIR}/include)
  make_directory(${BUILD_DIR})
  make_directory(${LOG_DIR})

  list(APPEND ARG_LDFLAGS -fuse-ld=${LLVM_BINARY_DIR}/bin/ld.lld)
  list(APPEND ARG_CFLAGS -fcf-protection=branch)
  list(APPEND ARG_LLVMFLAGS -clou-log=${LOG_DIR})
  list(APPEND ARG_CPPFLAGS -flegacy-pass-manager)
  list(APPEND ARG_DEPENDS ${LLVM_BINARY_DIR}/bin/ld.lld ${LLVM_BINARY_DIR}/bin/clang)
  
  foreach(pass IN LISTS ARG_PASS)
    list(APPEND ARG_CFLAGS -Xclang -load -Xclang $<TARGET_FILE:${pass}>)
    list(APPEND ARG_DEPENDS ${pass})
  endforeach()

  foreach(llvmflag IN LISTS ARG_LLVMFLAGS ARG_CLOUFLAGS)
    list(APPEND ARG_CFLAGS -mllvm ${llvmflag})
  endforeach()

  list(APPEND ARG_LDFLAGS -pthread) # is this still necessary?

  list(JOIN ARG_CFLAGS " " ARG_CFLAGS)
  list(JOIN ARG_CPPFLAGS " " ARG_CPPFLAGS)
  list(JOIN ARG_LDFLAGS " " ARG_LDFLAGS)
  
  # configure command
  add_custom_command(OUTPUT ${STAMP_DIR}/configure.stamp
    COMMAND ${SRCDIR}/configure --quiet --prefix=${PREFIX_DIR} CC=${LLVM_BINARY_DIR}/bin/clang LD=${LLVM_BINARY_DIR}/bin/ld.lld "CPPFLAGS=${ARG_CPPFLAGS}" "CFLAGS=${ARG_CFLAGS}" "LDFLAGS=${ARG_LDFLAGS}" ${ARG_CONFIGURE_OPTIONS}
    COMMAND touch ${STAMP_DIR}/configure.stamp
    COMMENT "Configuring ${LIBNAME} library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
    DEPENDS ${ARG_DEPENDS}
  )

  # clean step
  add_custom_command(OUTPUT ${STAMP_DIR}/clean.stamp
    COMMAND make --quiet clean
    COMMAND rm -f ${LOG_DIR}/*
    COMMAND touch ${STAMP_DIR}/clean.stamp
    DEPENDS ${STAMP_DIR}/configure.stamp
    COMMENT "Cleaning ${LIBNAME} library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
  )

  # build step
  add_custom_command(OUTPUT ${STAMP_DIR}/build.stamp ${BUILD_DIR}/build.log
    COMMAND rm -f ${LOG_DIR}/*
    COMMAND make --quiet 2>&1 # | tee ${BUILD_DIR}/build.log
    COMMAND touch ${STAMP_DIR}/build.stamp
    DEPENDS ${STAMP_DIR}/clean.stamp
    COMMENT "Building ${LIBNAME} library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
  )

  # test step
  add_custom_command(OUTPUT ${STAMP_DIR}/test.stamp
    COMMAND make --quiet check
    COMMAND touch ${STAMP_DIR}/test.stamp
    DEPENDS ${STAMP_DIR}/build.stamp
    COMMENT "Testing ${LIBNAME} library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
  )

  # install step
  add_custom_command(OUTPUT ${STAMP_DIR}/install.stamp ${INSTALL_DIR}/lib/${LIBRARY} ${INSTALL_DIR}/include/${HEADER}
    COMMAND make --quiet install
    COMMAND touch ${STAMP_DIR}/install.stamp
    DEPENDS ${STAMP_DIR}/test.stamp
    COMMENT "Installing ${LIBNAME} library ${NAME}"
    WORKING_DIRECTORY ${BUILD_DIR}
  )

  add_custom_target(${NAME}_install
    DEPENDS ${STAMP_DIR}/install.stamp
  )

  add_library(${NAME} INTERFACE)
  target_link_libraries(${NAME} INTERFACE ${INSTALL_DIR}/lib/${LIBRARY})
  target_include_directories(${NAME} INTERFACE ${INSTALL_DIR}/include)
  add_dependencies(${NAME} ${NAME}_install)
  
  set_target_properties(${NAME} PROPERTIES CLOU_LOGS ${LOG_DIR})
  
endfunction()


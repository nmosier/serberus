cmake_minimum_required(VERSION 3.25)

project(llsct)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(LLSCT_REQUIRE_CET On CACHE BOOL "Require CET to be enabled at runtime")

if(DEFINED LIBSODIUM_DIR)
  get_filename_component(LIBSODIUM_DIR ${LIBSODIUM_DIR} ABSOLUTE ${CMAKE_BINARY_DIR})
endif()
if(DEFINED OPENSSL_DIR)
  message("get_filename_component(OPENSSL_DIR ${OPENSSL_DIR} ABSOLUTE ${CMAKE_BINARY_DIR})")
endif()
if(DEFINED PIN_DIR)
  get_filename_component(PIN_DIR ${PIN_DIR} ABSOLUTE ${CMAKE_BINARY_DIR})
endif()

add_subdirectory(extern)

if(NOT DEFINED LLSCT_LLVM_DIR)
  message(FATAL_ERROR "LLSCT_LLVM_DIR unset. This should point to the install directory of llsct-llvm.")
endif()
list(APPEND CMAKE_MODULE_PATH ${LLSCT_LLVM_DIR}/lib/cmake)
include(${LLSCT_LLVM_DIR}/lib/cmake/llvm/LLVMConfig.cmake)

# TODO: Replace uses of LLVM_DIR
get_filename_component(LLVM_DIR "${LLCST_LLVM_DIR}" ABSOLUTE ${CMAKE_BINARY_DIR})

find_package(PkgConfig REQUIRED)
pkg_check_modules(Libprofiler libprofiler)
# FIXME: Might not need this anymore.
pkg_check_modules(Libunwind libunwind-generic)
# pkg_check_modules(GoogleBenchmark REQUIRED benchmark)
find_package(Python3 COMPONENTS Interpreter)

# FIXME: Why do I have these?
# set(Libunwind_INCLUDE_DIRS "/usr/lib/x86_64-linux-gnu/include")
# set(Libunwind_LIBRARY_DIRS "/usr/lib/x86_64-linux-gnu/lib")

find_package(Z3)

if(0)
  if(NOT DEFINED LLVM_TEST_SUITE_DIR)
    message(FATAL_ERROR "LLVM_TEST_SUITE_DIR not defined. Please define it.")
  endif()
endif()

add_compile_options(-Wall -Wextra -pedantic)

# For testing infrastructure
find_program(MAKE_EXE REQUIRED NAMES gmake nmake make)
include(ExternalProject)

enable_testing()

find_program(AWK_EXE REQUIRED NAMES awk gawk nawk)
find_program(CAT_EXE REQUIRED NAMES cat)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_subdirectory(src)
add_subdirectory(runtime)
add_subdirectory(tools)
add_subdirectory(scripts)
add_subdirectory(test)
add_subdirectory(bench)

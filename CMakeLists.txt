cmake_minimum_required(VERSION 3.16)

project(clou2)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(LLVM REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
# pkg_check_modules(Libprofiler libprofiler)
pkg_check_modules(Libunwind REQUIRED libunwind-generic)
# pkg_check_modules(GoogleBenchmark REQUIRED benchmark)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# message("GoogleBenchmark @ ${GoogleBenchmark_LIBRARY_DIRS}")
# pkg_check_modules(Capstone REQUIRED capstone)

if(DEFINED Z3_DIR)
  # TODO: Figure out why LLVM'ds F
  find_library(Z3_LIBRARIES PATHS ${Z3_DIR}/bin ${Z3_DIR}/lib REQUIRED NAMES z3)
  set(Z3_INCLUDE_DIR ${Z3_DIR}/include)
  set(Z3_INCLUDE_DIRS ${Z3_INCLUDE_DIR})
else()
  find_package(Z3 REQUIRED)
endif()

if("${CMAKE_GENERATOR}" STREQUAL Xcode)
  add_link_options(-w)
endif()

if(NOT DEFINED LIBSODIUM_DIR)
  message(FATAL_ERROR "LIBSODIUM_DIR is not defined. Please define it.")
endif()
get_filename_component(LIBSODIUM_DIR ${LIBSODIUM_DIR} ABSOLUTE BASE_DIR ${CMAKE_BINARY_DIR})

if(NOT DEFINED OPENSSL_DIR)
  message(FATAL_ERROR "OPENSSL_DIR is not defined. Please define it.")
endif()
get_filename_component(OPENSSL_DIR ${OPENSSL_DIR} ABSOLUTE BASE_DIR ${CMAKE_BINARY_DIR})

add_compile_options(-Wall -Wextra -pedantic)

# For testing infrastructure
find_program(MAKE_EXE REQUIRED NAMES gmake nmake make)
include(ExternalProject)

enable_testing()

find_program(AWK_EXE REQUIRED NAMES awk gawk nawk)
find_program(CAT_EXE REQUIRED NAMES cat)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

get_filename_component(LLVM_DIR ${LLVM_DIR} ABSOLUTE ${CMAKE_BINARY_DIR})

message("Z3_INCLUDE_DIR=${Z3_INCLUDE_DIR}")

add_subdirectory(extern)
add_subdirectory(src)
add_subdirectory(runtime)
add_subdirectory(tools)
add_subdirectory(scripts)
add_subdirectory(test)
add_subdirectory(bench)

message("LLVM_DIR=${LLVM_DIR}")

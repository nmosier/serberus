cmake_minimum_required(VERSION 3.16)

project(clou2)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(LLVM REQUIRED CONFIG)
find_package(Z3 REQUIRED)
find_package(Boost REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(Libprofiler libprofiler)
pkg_check_modules(Libunwind REQUIRED libunwind-ptrace)
pkg_check_modules(Capstone REQUIRED capstone)

if("${CMAKE_GENERATOR}" STREQUAL Xcode)
  add_link_options(-w)
endif()

add_compile_options(-Wall -Wextra -pedantic)

# For testing infrastructure
find_program(MAKE_EXE REQUIRED NAMES gmake nmake make)
include(ExternalProject)

enable_testing()

find_program(AWK_EXE REQUIRED NAMES awk gawk nawk)
find_program(CAT_EXE REQUIRED NAMES cat)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

get_filename_component(LLVM_DIR ${LLVM_DIR} ABSOLUTE ${CMAKE_BINARY_DIR})

add_subdirectory(src)
add_subdirectory(runtime)
add_subdirectory(tools)
add_subdirectory(scripts)
add_subdirectory(test)

message("LLVM_DIR=${LLVM_DIR}")

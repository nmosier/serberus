cmake_minimum_required(VERSION 3.16)

project(clou2)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(LLVM REQUIRED CONFIG)
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

add_subdirectory(src)
add_subdirectory(runtime)
add_subdirectory(test)

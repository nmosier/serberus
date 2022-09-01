cmake_minimum_required(VERSION 3.16)

project(clou2)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(LLVM REQUIRED CONFIG)
find_package(Boost REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(Libprofiler libprofiler)

if("${CMAKE_GENERATOR}" STREQUAL Xcode)
  add_link_options(-w)
endif()

add_compile_options(-Wall -Wextra -pedantic)

add_subdirectory(src)

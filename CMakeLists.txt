cmake_minimum_required(VERSION 3.20)

project(clou2)

set(CMAKE_CXX_STANDARD 20 REQUIRED)
find_package(LLVM REQUIRED CONFIG)
find_package(Boost REQUIRED)
pkg_check_modules(Libprofiler REQUIRED libprofiler)

if("${CMAKE_GENERATOR}" STREQUAL Xcode)
  add_link_options(-w)
endif()

add_compile_options(-Wall -Wextra -pedantic)

add_subdirectory(src)

cmake_minimum_required(VERSION 3.20)

project(clou2)

set(CMAKE_CXX_STANDARD 20 REQUIRED)
find_package(LLVM REQUIRED CONFIG)

if("${CMAKE_GENERATOR}" STREQUAL Xcode)
  add_link_options(-w)
endif()

add_compile_options(-Wall -Wextra -pedantic)

add_subdirectory(src)

cmake_minimum_required(VERSION 2.8.12)
project(lockfree_queue)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

macro(add_flags var)
    string(REPLACE ";" " " _flags "${ARGN}")
    set(${var} "${${var}} ${_flags}")
endmacro(add_flags)

find_package(Threads REQUIRED)

add_flags(CMAKE_C_FLAGS -Wall -Wextra -Werror -Wformat=2 -Wundef
        -Werror-implicit-function-declaration -Wno-unused-parameter -D_GNU_SOURCE
        -O3 -g -Wstrict-prototypes -Wdeclaration-after-statement -Wno-tautological-compare)

add_subdirectory(src)
include(CTest)

cmake_minimum_required(VERSION 2.8.12)
project(lockfree_queue)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")


macro(add_flags var)
    string(REPLACE ";" " " _flags "${ARGN}")
    set(${var} "${${var}} ${_flags}")
endmacro(add_flags)

set(COMMON_FLAGS -Wall -Wextra -Werror -Wformat=2 -Wundef -g
        -Werror-implicit-function-declaration -Wno-unused-parameter
        -D_GNU_SOURCE)
add_flags(CMAKE_C_FLAGS ${COMMON_FLAGS} -Wstrict-prototypes
        -Wdeclaration-after-statement -Wno-tautological-compare)

set(SOURCE_FILES main.c lf_queue.c)
add_executable(lf_queue_test ${SOURCE_FILES})
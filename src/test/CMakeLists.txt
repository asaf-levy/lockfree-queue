include_directories(${PROJECT_SOURCE_DIR}/src/include)

enable_testing()

set(SOURCE_FILES lf_queue_test.c)
add_executable(lf_queue_test ${SOURCE_FILES})
target_link_libraries(lf_queue_test ${CMAKE_THREAD_LIBS_INIT} rt lf_queue)
add_test(lf_queue_test lf_queue_test)


include_directories(include)

set(SOURCE_FILES lf_queue.c)
add_library(lf_queue SHARED ${SOURCE_FILES})

install(TARGETS lf_queue
        LIBRARY DESTINATION lib64)
install(DIRECTORY include/ DESTINATION /usr/include/lf-queue
        FILES_MATCHING PATTERN "*.h")

enable_testing()
add_subdirectory(test)

cmake_minimum_required(VERSION 3.16)

add_executable(fake2d_hello main.cpp)

target_link_libraries(fake2d_hello PRIVATE fake2d::fake2d)

# Copy sample scripts next to the binary for convenient local runs.
add_custom_command(TARGET fake2d_hello POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:fake2d_hello>/scripts
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/scripts
            $<TARGET_FILE_DIR:fake2d_hello>/scripts
)

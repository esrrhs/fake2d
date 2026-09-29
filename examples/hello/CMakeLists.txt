cmake_minimum_required(VERSION 3.16)

add_executable(fake2d_hello main.cpp)

target_include_directories(fake2d_hello PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)

# stb_image_write is vendored code and still uses sprintf internally.
target_compile_options(fake2d_hello PRIVATE -Wno-deprecated-declarations)

target_link_libraries(fake2d_hello PRIVATE fake2d::fake2d)
set_target_properties(fake2d_hello PROPERTIES ENABLE_EXPORTS ON)

# Sync sample scripts on every build (POST_BUILD only fires on relink).
set(FAKE2D_HELLO_BIN_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
if (NOT FAKE2D_HELLO_BIN_DIR)
    set(FAKE2D_HELLO_BIN_DIR "${CMAKE_BINARY_DIR}")
endif ()
add_custom_target(fake2d_copy_scripts ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/scripts
            ${FAKE2D_HELLO_BIN_DIR}/scripts
)

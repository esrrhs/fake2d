cmake_minimum_required(VERSION 3.16)

add_executable(fake2d_hello main.cpp)

# The benchmark uses the renderer's existing GL portability shim for context info.
target_include_directories(fake2d_hello PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb
                                              ${CMAKE_SOURCE_DIR}/src)

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

# --- CTest: one entry per headless self-test suite --------------------------
# This is the shared suite list the Build workflow runs on Linux, macOS and
# Windows (30 --<suite>-test binaries, both UI stress seeds, and --ui-bench).
# Everything runs from the repository root so the suites find assets/, scripts/
# and the generated first-run files exactly as the demos do. Linux runners have
# no display, so wrap the binary in xvfb-run when it is available; macOS uses
# the CGL headless path and Windows uses Mesa llvmpipe. The suites still create
# a GL context headlessly.
if (FAKE2D_BUILD_TESTS)
    find_program(FAKE2D_XVFB_RUN xvfb-run)

    set(FAKE2D_TEST_SUITES
        art audio city combat contra determinism fog font fx gameplay launcher
        light map-object path phys-extended phys-filter phys-query phys-rigid
        platform post replay rt rts shmup skeleton snooker tile timestep ui vn)

    foreach (suite IN LISTS FAKE2D_TEST_SUITES)
        set(_prefix)
        if (FAKE2D_XVFB_RUN)
            list(APPEND _prefix "${FAKE2D_XVFB_RUN}" -a
                 -s "-screen 0 1024x768x24")
        endif ()
        add_test(NAME "fake2d-${suite}"
            COMMAND ${_prefix} $<TARGET_FILE:fake2d_hello>
                    --headless "--${suite}-test")
        set_tests_properties("fake2d-${suite}" PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            TIMEOUT 900
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
    endforeach ()
    # Extended seeded runs and the benchmark gate COUNTERS, never wall-clock thresholds.
    foreach (seed IN ITEMS 42 1492026)
        add_test(NAME "fake2d-ui-stress-${seed}"
            COMMAND ${_prefix} $<TARGET_FILE:fake2d_hello> --headless --ui-stress
                    --frames 300 --ui-stress-seed ${seed})
        set_tests_properties("fake2d-ui-stress-${seed}" PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 900
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
    endforeach ()
    add_test(NAME fake2d-ui-bench
        COMMAND ${_prefix} $<TARGET_FILE:fake2d_hello> --headless --ui-bench)
    set_tests_properties(fake2d-ui-bench PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 900
        ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
endif ()

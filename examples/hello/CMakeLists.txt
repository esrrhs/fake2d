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
# Sources live in examples/hello/scripts/; the engine still loads runtime
# path scripts/ from the working directory. Tests and the documented
# "./build/bin/fake2d_hello" workflow run from the repository root, so copy
# both next to the binary and into CMAKE_SOURCE_DIR/scripts (gitignored).
set(FAKE2D_HELLO_BIN_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
if (NOT FAKE2D_HELLO_BIN_DIR)
    set(FAKE2D_HELLO_BIN_DIR "${CMAKE_BINARY_DIR}")
endif ()
add_custom_target(fake2d_copy_scripts ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/examples/hello/scripts
            ${FAKE2D_HELLO_BIN_DIR}/scripts
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/examples/hello/scripts
            ${CMAKE_SOURCE_DIR}/scripts
)
add_dependencies(fake2d_hello fake2d_copy_scripts)

# --- CTest: one entry per headless self-test suite --------------------------
# This is the shared suite list the Build workflow runs on Linux, macOS and
# Windows (31 --<suite>-test binaries, both UI stress seeds, and the Google Benchmark UI target).
# Everything runs from the repository root so the suites find assets/, the
# runtime scripts/ copy, and the generated first-run files exactly as the demos do. Linux runners have
# no display, so wrap the binary in xvfb-run when it is available; macOS uses
# the CGL headless path and Windows uses Mesa llvmpipe. The suites still create
# a GL context headlessly.
if (FAKE2D_BUILD_TESTS)
    add_executable(fake2d_engine_tests ../../tests/engine_contracts.cpp)
    target_include_directories(fake2d_engine_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party/stb)
    target_link_libraries(fake2d_engine_tests PRIVATE fake2d::fake2d GTest::gtest_main)
    set_target_properties(fake2d_engine_tests PROPERTIES ENABLE_EXPORTS ON)
    add_executable(fake2d_benchmarks ../../tests/benchmark_engine.cpp)
    target_include_directories(fake2d_benchmarks PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party/stb)
    target_link_libraries(fake2d_benchmarks PRIVATE fake2d::fake2d benchmark::benchmark)
    set_target_properties(fake2d_benchmarks PROPERTIES ENABLE_EXPORTS ON)
    add_dependencies(fake2d_engine_tests fake2d_copy_scripts)
    add_dependencies(fake2d_benchmarks fake2d_copy_scripts)
    if(FAKE2D_COVERAGE)
        target_compile_options(fake2d_engine_tests PRIVATE --coverage -O0 -g)
        target_compile_options(fake2d_benchmarks PRIVATE --coverage -O0 -g)
    endif()
    find_program(FAKE2D_XVFB_RUN xvfb-run)

    set(FAKE2D_TEST_SUITES
        art audio city combat contra determinism flow fog font fx gameplay launcher
        light map-object path phys-extended phys-filter phys-query phys-rigid
        platform post replay rt rts shmup skeleton snooker tile timestep ui vn)

    foreach (suite IN LISTS FAKE2D_TEST_SUITES)
        set(_prefix)
        if (FAKE2D_XVFB_RUN)
            list(APPEND _prefix "${FAKE2D_XVFB_RUN}" -a
                 -s "-screen 0 1024x768x24")
        endif ()
        if(suite STREQUAL "launcher")
            set(_filter "Launcher.catalog")
        else()
            string(REPLACE "-" "_" _case "${suite}")
            set(_filter "EngineContracts.${_case}")
        endif()
        add_test(NAME "fake2d-${suite}"
            COMMAND ${_prefix} $<TARGET_FILE:fake2d_engine_tests>
                    "--gtest_filter=${_filter}"
                    "--gtest_output=xml:${CMAKE_BINARY_DIR}/test-results/${suite}.xml")
        set_tests_properties("fake2d-${suite}" PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            TIMEOUT 900
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
    endforeach ()
    # Extended seeded runs and the benchmark gate COUNTERS, never wall-clock thresholds.
    foreach (seed IN ITEMS 42 1492026)
        add_test(NAME "fake2d-ui-stress-${seed}"
            COMMAND ${_prefix} $<TARGET_FILE:fake2d_engine_tests>
                    "--gtest_filter=Seeds/UIStress.states/Seed${seed}"
                    "--gtest_output=xml:${CMAKE_BINARY_DIR}/test-results/ui-stress-${seed}.xml")
        set_tests_properties("fake2d-ui-stress-${seed}" PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 900
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
    endforeach ()
endif ()

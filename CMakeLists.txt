cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(fake2d VERSION 1.55.0 LANGUAGES CXX)
if (APPLE)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR})

include(cmake/CPM.cmake)

option(FAKE2D_BUILD_EXAMPLES "Build example apps" ON)
option(FAKE2D_WITH_FAKELUA "Build with FakeLua scripting" ON)
# Optional Steamworks backend. The proprietary SDK is never bundled; when OFF
# the adapter compiles to a no-op stub with the same API.
option(FAKE2D_WITH_STEAM "Build the Steamworks adapter (needs the Steam SDK)" OFF)
# CI-only: compile src/platform/steam.cpp against tests/fake_steam, proving the
# SDK branch stays buildable without the proprietary SDK.
option(FAKE2D_TEST_STEAM_HEADERS "Compile-check the Steam backend with stub headers" OFF)
# Generate install rules so other projects consume fake2d via find_package.
option(FAKE2D_INSTALL "Generate install rules for fake2d" ON)
# Register the headless self-test suites with CTest (needs the example app).
option(FAKE2D_BUILD_TESTS "Register headless test suites with CTest" ON)

# Instrument owned targets only; dependency code is outside the coverage scope.
option(FAKE2D_COVERAGE "Instrument Linux/GCC core library line coverage" OFF)
if (FAKE2D_COVERAGE)
    if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "FAKE2D_COVERAGE requires Linux and GCC/gcov")
    endif ()
endif ()

if (FAKE2D_WITH_FAKELUA)
    find_package(fakelua REQUIRED)
    message(STATUS "Found FakeLua: ${fakelua_DIR}")
else ()
    message(STATUS "FakeLua scripting disabled (FAKE2D_WITH_FAKELUA=OFF)")
endif ()

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
CPMAddPackage(
    NAME glfw
    GITHUB_REPOSITORY glfw/glfw
    GIT_TAG 3.4
)

CPMAddPackage(
    NAME nlohmann_json
    GITHUB_REPOSITORY nlohmann/json
    VERSION 3.11.3
    OPTIONS "JSON_BuildTests OFF" "JSON_Install OFF"
)

find_package(OpenGL REQUIRED)

add_subdirectory(src)
if (FAKE2D_COVERAGE)
    target_compile_options(fake2d PRIVATE --coverage -O0 -g)
    target_link_options(fake2d PUBLIC --coverage)
    # Compile every public header with emitted inline bodies. Without this,
    # never-used inline functions silently disappear from the denominator.
    file(GLOB _coverage_headers RELATIVE "${CMAKE_SOURCE_DIR}/include" "include/fake2d/*.h")
    set(_coverage_includes "")
    foreach (_header IN LISTS _coverage_headers)
        string(APPEND _coverage_includes "#include \"${_header}\"\n")
    endforeach ()
    file(GLOB_RECURSE _coverage_private_headers RELATIVE "${CMAKE_SOURCE_DIR}/src" "src/*.h")
    foreach (_header IN LISTS _coverage_private_headers)
        string(APPEND _coverage_includes "#include \"${_header}\"\n")
    endforeach ()
    file(WRITE "${CMAKE_BINARY_DIR}/coverage_headers.cpp" "${_coverage_includes}")
    add_library(fake2d_coverage_headers OBJECT "${CMAKE_BINARY_DIR}/coverage_headers.cpp")
    target_include_directories(fake2d_coverage_headers PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(fake2d_coverage_headers PRIVATE fake2d::fake2d)
    target_compile_options(fake2d_coverage_headers PRIVATE --coverage -O0 -g -fkeep-inline-functions)
endif ()

if (FAKE2D_BUILD_TESTS)
    include(cmake/TestFrameworks.cmake)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_executable(fake2d_integration_tests tests/integration_tests.cpp)
    target_link_libraries(fake2d_integration_tests PRIVATE GTest::gtest nlohmann_json::nlohmann_json)
    if(WIN32)
        target_link_libraries(fake2d_integration_tests PRIVATE shell32)
    endif()
    target_compile_definitions(fake2d_integration_tests PRIVATE
        FAKE2D_PYTHON="${Python3_EXECUTABLE}"
        FAKE2D_TRANSPORT="${CMAKE_SOURCE_DIR}/tools/integration_transport.py"
        FAKE2D_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
    if(FAKE2D_COVERAGE)
        set(_coverage_gcov "$ENV{GCOV}")
        if(_coverage_gcov STREQUAL "")
            set(_coverage_gcov gcov)
        endif()
        add_test(NAME fake2d-coverage-reporter
            COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/test_coverage_blocks.py
                    --compiler ${CMAKE_CXX_COMPILER} --gcov ${_coverage_gcov}
                    --test-binary $<TARGET_FILE:fake2d_integration_tests>)

    endif()
    # Compile-and-run guard: inject windef.h's near/far macros, include the
    # same header chain as hello, then use `auto near` / member `far`. This
    # is its own executable so it is not counted as a hello self-test suite.
    # CI runs it through CTest on Linux, macOS and Windows.
    add_executable(fake2d_win_macro_compat tests/win_macro_compat.cpp)
    target_include_directories(fake2d_win_macro_compat PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(fake2d_win_macro_compat PRIVATE fake2d::fake2d GTest::gtest_main)
    add_test(NAME fake2d-win-macro-compat COMMAND fake2d_win_macro_compat)
    add_executable(fake2d_ui_benchmark tests/benchmark_ui.cpp)
    set_target_properties(fake2d_ui_benchmark PROPERTIES ENABLE_EXPORTS ON)
    target_include_directories(fake2d_ui_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(fake2d_ui_benchmark PRIVATE fake2d::fake2d benchmark::benchmark)
    if (FAKE2D_COVERAGE)
        target_compile_options(fake2d_ui_benchmark PRIVATE --coverage -O0 -g)
    endif ()
    add_executable(fake2d_coverage_regression tests/coverage_regression.cpp tests/gtest_main.cpp)
    target_include_directories(fake2d_coverage_regression PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(fake2d_coverage_regression PRIVATE GTest::gtest fake2d::fake2d)
    if (FAKE2D_COVERAGE)
        target_compile_options(fake2d_coverage_regression PRIVATE --coverage -O0 -g)
    endif ()
    find_program(FAKE2D_TEST_XVFB xvfb-run)
    set(_regression_prefix)
    if (FAKE2D_TEST_XVFB)
        # A GoogleTest process closes one fixture's last GL client before the
        # next creates its context. Do not reset the X server in that gap.
        list(APPEND _regression_prefix "${FAKE2D_TEST_XVFB}" -a
             -s "-screen 0 1280x1024x24 -noreset")
    endif ()
    set(_benchmark_prefix)
    if (FAKE2D_TEST_XVFB)
        list(APPEND _benchmark_prefix "${FAKE2D_TEST_XVFB}" -a -s "-screen 0 1024x768x24")
    endif ()
    add_test(NAME fake2d-ui-bench COMMAND ${_benchmark_prefix} $<TARGET_FILE:fake2d_ui_benchmark>)
    set_tests_properties(fake2d-ui-bench PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        TIMEOUT 900 ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
    add_test(NAME fake2d-coverage-regression
        COMMAND ${_regression_prefix} $<TARGET_FILE:fake2d_coverage_regression>)
    set_tests_properties(fake2d-coverage-regression PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 120
        ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
    add_executable(fake2d_coverage_gaps tests/coverage_gap_tests.cpp tests/gtest_main.cpp)
    set_target_properties(fake2d_coverage_gaps PROPERTIES ENABLE_EXPORTS ON)
    target_include_directories(fake2d_coverage_gaps PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(fake2d_coverage_gaps PRIVATE GTest::gtest fake2d::fake2d nlohmann_json::nlohmann_json)
    if (FAKE2D_COVERAGE)
        target_compile_options(fake2d_coverage_gaps PRIVATE --coverage -O0 -g)
    endif ()
    add_test(NAME fake2d-coverage-gaps COMMAND ${_regression_prefix} $<TARGET_FILE:fake2d_coverage_gaps>)
    set_tests_properties(fake2d-coverage-gaps PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 180
        ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
    add_executable(fake2d_coverage_deep tests/coverage_deep_tests.cpp tests/gtest_main.cpp)
    set_target_properties(fake2d_coverage_deep PROPERTIES ENABLE_EXPORTS ON)
    target_include_directories(fake2d_coverage_deep PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(fake2d_coverage_deep PRIVATE GTest::gtest fake2d::fake2d nlohmann_json::nlohmann_json)
    if (FAKE2D_COVERAGE)
        target_compile_options(fake2d_coverage_deep PRIVATE --coverage -O0 -g)
        # Observe genuine child crash counters before the real signal/_Exit.
        # Wrappers delegate unchanged; no product hooks or fabricated counts.
        target_compile_definitions(fake2d_coverage_deep PRIVATE FAKE2D_TEST_CRASH_PROFILE=1)
        target_link_options(fake2d_coverage_deep PRIVATE -Wl,--wrap=raise -Wl,--wrap=_Exit)
    endif ()
    add_test(NAME fake2d-coverage-deep COMMAND ${_regression_prefix} $<TARGET_FILE:fake2d_coverage_deep>)
    set_tests_properties(fake2d-coverage-deep PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 300
        ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
endif ()

if (FAKE2D_BUILD_EXAMPLES)
    add_subdirectory(examples/hello)
    if (FAKE2D_COVERAGE)
        target_compile_options(fake2d_hello PRIVATE --coverage -O0 -g)
    endif ()
endif ()

# --- Install: make fake2d consumable by other projects via find_package ---
# Ships the CPM-built GLFW archive alongside libfake2d.a so consumers link
# only fake2d::fake2d and every transitive dependency resolves locally.
if (FAKE2D_INSTALL)
    include(CMakePackageConfigHelpers)

    # Imported targets referenced from the exported interface must be globally
    # visible for export() to accept them.
    set_target_properties(OpenGL::GL PROPERTIES IMPORTED_GLOBAL TRUE)
    if (TARGET Threads::Threads)
        set_target_properties(Threads::Threads PROPERTIES IMPORTED_GLOBAL TRUE)
    endif ()
    if (TARGET fakelua::fakelua)
        set_target_properties(fakelua::fakelua PROPERTIES IMPORTED_GLOBAL TRUE)
    endif ()

    # GLFW declares its platform frameworks PRIVATE, which keeps them out of
    # an exported static library's link interface; surface them for consumers.
    if (APPLE)
        set_property(TARGET glfw APPEND PROPERTY INTERFACE_LINK_LIBRARIES
            "-framework Cocoa" "-framework IOKit" "-framework CoreFoundation")
    endif ()

    install(TARGETS fake2d glfw EXPORT fake2dTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY ${glfw_SOURCE_DIR}/include/GLFW
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT fake2dTargets
        NAMESPACE fake2d::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fake2d)

    configure_package_config_file(cmake/fake2dConfig.cmake.in
        "${CMAKE_BINARY_DIR}/cmake/fake2dConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fake2d)
    write_basic_package_version_file(
        "${CMAKE_BINARY_DIR}/cmake/fake2dConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    install(FILES
        "${CMAKE_BINARY_DIR}/cmake/fake2dConfig.cmake"
        "${CMAKE_BINARY_DIR}/cmake/fake2dConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fake2d)
endif ()

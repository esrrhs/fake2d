cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(fake2d VERSION 0.1.0 LANGUAGES CXX)

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
option(FAKE2D_WITH_FAKELUA "Build with FakeLua scripting (requires fakelua source)" ON)
set(FAKELUA_SOURCE_DIR "" CACHE PATH "Path to a local FakeLua checkout")

# Auto-detect sibling fakelua
if (FAKELUA_SOURCE_DIR STREQUAL "" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../fakelua/CMakeLists.txt")
    set(FAKELUA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../fakelua" CACHE PATH "" FORCE)
endif ()

if (FAKE2D_WITH_FAKELUA)
    if (NOT FAKELUA_SOURCE_DIR OR NOT EXISTS "${FAKELUA_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "FAKE2D_WITH_FAKELUA=ON but FakeLua not found.\n"
            "  Clone https://github.com/esrrhs/fakelua next to this repo, or pass\n"
            "  -DFAKELUA_SOURCE_DIR=/path/to/fakelua\n"
            "  Or configure with -DFAKE2D_WITH_FAKELUA=OFF for a window-only skeleton build.")
    endif ()
    message(STATUS "FakeLua: ${FAKELUA_SOURCE_DIR}")
    set(FAKELUA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FAKELUA_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${FAKELUA_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/fakelua" EXCLUDE_FROM_ALL)
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

find_package(OpenGL REQUIRED)

add_subdirectory(src)

if (FAKE2D_BUILD_EXAMPLES)
    add_subdirectory(examples/hello)
endif ()

cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(fake2d VERSION 0.4.0 LANGUAGES CXX)

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

if (FAKE2D_BUILD_EXAMPLES)
    add_subdirectory(examples/hello)
endif ()

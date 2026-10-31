cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(fake2d VERSION 1.41.0 LANGUAGES CXX)

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

cmake_minimum_required(VERSION 3.16)

set(FAKE2D_SOURCES
    core/engine.cpp
    core/resource_manager.cpp
    core/save_store.cpp
    core/entity_store.cpp
    core/i18n.cpp
    core/app_paths.cpp
    core/log.cpp
    core/achievements.cpp
    core/mod.cpp
    core/rng.cpp
    render/renderer.cpp
    render/shader.cpp
    render/texture.cpp
    render/camera.cpp
    render/sprite_batch.cpp
    render/lighting.cpp
    render/skeleton.cpp
    render/atlas.cpp
    render/font.cpp
    render/particle.cpp
    render/animation.cpp
    render/gl_loader.cpp
    render/tilemap.cpp
    render/render_target.cpp
    render/image_write.cpp
    scene/node.cpp
    scene/scene.cpp
    platform/window.cpp
    platform/input.cpp
    platform/input_recording.cpp
    platform/steam.cpp
    audio/audio.cpp
    physics/physics_world.cpp
    gameplay/character.cpp
    ui/ui.cpp
)

if (FAKE2D_WITH_FAKELUA)
    list(APPEND FAKE2D_SOURCES
        script/script_host.cpp
        script/script_bindings.cpp
    )
else ()
    list(APPEND FAKE2D_SOURCES script/script_host_stub.cpp)
endif ()

add_library(fake2d STATIC ${FAKE2D_SOURCES})
add_library(fake2d::fake2d ALIAS fake2d)

# The headers need C++23 (std::clamp in math.h, constexpr handle types), and
# CMAKE_CXX_STANDARD is a directory property that stops at this project's edge:
# an installed-target consumer would otherwise compile them at its own default
# and fail on the first include. Exporting the feature makes it travel with
# fake2d::fake2d. Guarded because cxx_std_23 is only known to CMake >= 3.21
# while this project still declares 3.16.
if(NOT CMAKE_VERSION VERSION_LESS 3.21)
    target_compile_features(fake2d PUBLIC cxx_std_23)
endif()

target_include_directories(fake2d
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/third_party
        ${CMAKE_SOURCE_DIR}/third_party/stb
)

target_link_libraries(fake2d
    PUBLIC
        glfw
        OpenGL::GL
        $<$<PLATFORM_ID:Linux>:m>
)

target_compile_definitions(fake2d
    PUBLIC
        $<$<NOT:$<PLATFORM_ID:Darwin>>:GL_GLEXT_PROTOTYPES>
)

if (FAKE2D_WITH_FAKELUA)
    target_link_libraries(fake2d PUBLIC fakelua::fakelua)
    target_compile_definitions(fake2d PUBLIC FAKE2D_WITH_FAKELUA=1)
endif ()

# Optional Steamworks backend (proprietary SDK; never bundled). The default
# build links the no-op stub in platform/steam.cpp.
if (FAKE2D_WITH_STEAM)
    set(FAKE2D_STEAM_SDK "" CACHE PATH "Steamworks SDK root")
    if (NOT FAKE2D_STEAM_SDK)
        message(FATAL_ERROR
            "FAKE2D_WITH_STEAM=ON needs -DFAKE2D_STEAM_SDK=/path/to/steamworks/sdk")
    endif ()
    target_include_directories(fake2d PRIVATE "${FAKE2D_STEAM_SDK}/public")
    target_compile_definitions(fake2d PUBLIC FAKE2D_WITH_STEAM_SDK=1)
    if (APPLE)
        set(FAKE2D_STEAM_LIB "${FAKE2D_STEAM_SDK}/redistributable_bin/osx/libsteam_api.dylib")
    elseif (WIN32)
        set(FAKE2D_STEAM_LIB "${FAKE2D_STEAM_SDK}/redistributable_bin/win64/steam_api64.lib")
    else ()
        set(FAKE2D_STEAM_LIB "${FAKE2D_STEAM_SDK}/redistributable_bin/linux64/libsteam_api.so")
    endif ()
    if (NOT EXISTS "${FAKE2D_STEAM_LIB}")
        message(FATAL_ERROR "Steamworks library not found: ${FAKE2D_STEAM_LIB}")
    endif ()
    target_link_libraries(fake2d PUBLIC "${FAKE2D_STEAM_LIB}")
endif ()

# CI-only compile check: the SDK branch of the adapter must build against the
# stub headers in tests/fake_steam (no proprietary SDK, no link).
if (FAKE2D_TEST_STEAM_HEADERS)
    add_library(fake2d_steam_compile_check OBJECT platform/steam.cpp)
    target_include_directories(fake2d_steam_compile_check PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fake_steam)
    target_compile_definitions(fake2d_steam_compile_check PRIVATE
        FAKE2D_WITH_STEAM_SDK=1)
endif ()

# JSON parsing for TexturePacker atlases (header-only, kept out of the public API).
# BUILD_INTERFACE keeps it out of the exported link interface: consumers never
# need json symbols at link time, and the target itself is not exported.
target_link_libraries(fake2d PRIVATE $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>)

# miniaudio platform backends.
if (APPLE)
    target_link_libraries(fake2d PUBLIC
        "-framework Foundation"
        "-framework CoreAudio"
        "-framework AudioToolbox")
elseif (UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(fake2d PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif ()

target_compile_options(fake2d PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wno-unused-parameter>
)

# Vendored single-header libraries (miniaudio, stb_truetype) trip -Wextra.
set_source_files_properties(
    audio/audio.cpp
    render/font.cpp
    PROPERTIES
        COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-deprecated-declarations;-Wno-unused-but-set-variable>"
)

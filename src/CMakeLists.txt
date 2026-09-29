cmake_minimum_required(VERSION 3.16)

set(FAKE2D_SOURCES
    core/engine.cpp
    core/resource_manager.cpp
    render/renderer.cpp
    render/shader.cpp
    render/texture.cpp
    render/camera.cpp
    render/sprite_batch.cpp
    render/atlas.cpp
    scene/node.cpp
    scene/scene.cpp
    platform/window.cpp
)

if (FAKE2D_WITH_FAKELUA)
    list(APPEND FAKE2D_SOURCES script/script_host.cpp)
else ()
    list(APPEND FAKE2D_SOURCES script/script_host_stub.cpp)
endif ()

add_library(fake2d STATIC ${FAKE2D_SOURCES})
add_library(fake2d::fake2d ALIAS fake2d)

target_include_directories(fake2d
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/third_party
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

# JSON parsing for TexturePacker atlases (header-only, kept out of the public API).
target_link_libraries(fake2d PRIVATE nlohmann_json::nlohmann_json)

target_compile_options(fake2d PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wno-unused-parameter>
)

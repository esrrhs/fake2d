cmake_minimum_required(VERSION 3.16)
project(fake2d_consumer CXX)
find_package(fake2d 1.55 REQUIRED CONFIG)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/CPM.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/GoogleTest.cmake)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE fake2d::fake2d GTest::gtest_main)

# Test-harness metadata only; never part of the installed library interface.
target_compile_definitions(consumer PRIVATE
    TEST_CONSUMER_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
    TEST_PACKAGE_TARGETS="${fake2d_DIR}/fake2dTargets.cmake")

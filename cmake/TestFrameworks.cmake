# Test/benchmark-only dependencies: immutable commits from official release tags.
# Never included with FAKE2D_BUILD_TESTS=OFF; no framework install/export rules.
include(${CMAKE_CURRENT_LIST_DIR}/GoogleTest.cmake)
CPMAddPackage(
    NAME googlebenchmark
    GITHUB_REPOSITORY google/benchmark
    GIT_TAG 31c66f47f60d06790a4885a261d682ba339bd100 # v1.9.4
    OPTIONS "BENCHMARK_ENABLE_TESTING OFF" "BENCHMARK_ENABLE_GTEST_TESTS OFF"
            "BENCHMARK_DOWNLOAD_DEPENDENCIES OFF" "BENCHMARK_ENABLE_INSTALL OFF"
)

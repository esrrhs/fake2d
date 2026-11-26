# Official test-only Google Test release pin; installation stays disabled.
CPMAddPackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723 # v1.17.0
    OPTIONS "INSTALL_GTEST OFF" "BUILD_GMOCK OFF" "gtest_force_shared_crt ON"
)

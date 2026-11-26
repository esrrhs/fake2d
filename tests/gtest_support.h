#pragma once
#include <gtest/gtest.h>
#include <memory>

namespace fake2d::test {
// Existing regressions use assertions inside non-void helpers and Lua callbacks.
// Native EXPECT_TRUE reports their original location; the standard Google Test
// AssertionException unwinds the helper, just as the old throwing checks did.
// Do not throw for framework-generated exception diagnostics (no source file),
// which would recursively throw while Google Test is handling an exception.
class UnwindFailedHelper final : public testing::EmptyTestEventListener {
    void OnTestPartResult(const testing::TestPartResult &result) override {
        if (result.failed() && result.file_name())
            throw testing::AssertionException(result);
    }
};

template <class Files> class FilesEnvironment final : public testing::Environment {
public:
    // Match the old main-local lifetime: a forked child calling std::exit
    // must not remove its parent's shared fixture directory via a global
    // Google Test environment destructor. Only normal test tear-down owns it.
    Files *files = nullptr;
    void SetUp() override { files = new Files; }
    void TearDown() override { delete files; files = nullptr; }
};
} // namespace fake2d::test

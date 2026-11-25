#include "gtest_support.h"
#include <cstdio>

// Each regression translation unit retains its original assertion counter and
// registers only its own temporary-file environment (if any).
void RegisterFake2dTestEnvironment();
int Fake2dAssertionCount();

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    testing::UnitTest::GetInstance()->listeners().Append(new fake2d::test::UnwindFailedHelper);
    RegisterFake2dTestEnvironment();
    const int result = RUN_ALL_TESTS();
    std::printf("native assertion evaluations: %d\n", Fake2dAssertionCount());
    return result;
}

#include "fake2d/engine.h"
#include "fake2d/version.h"
#include <cstdio>
#include <string>
#include <gtest/gtest.h>
TEST(InstalledConsumer, PublicAPI) {
    fake2d::EngineConfig cfg;
    fake2d::Engine engine;
    auto &ui = engine.GetUI();
    ASSERT_TRUE(ui.ConfigureVariableList("api", {"stable-a", "stable-b"}, 24, "v1"));
    ASSERT_TRUE(ui.SetVariableRowText("api", "stable-a", "UTF-8 田", .6f));
    ui.SetVariableMeasureBudget(64);
    ASSERT_EQ(ui.UnscopedWarnings(), 0);
    ui.SetTextValue("edit", "UTF-8 田");
    ui.NewFrame();
    ASSERT_TRUE(ui.BeginFrameUI() && ui.EndFrameUI());
    ASSERT_TRUE(ui.VariableListTotal("api") == 48 && ui.TextValue("edit") == "UTF-8 田");
    testing::internal::CaptureStdout();
    std::printf("consumer: %s, ai_input default %d, frame %llu\n",
                FAKE2D_VERSION_STRING, cfg.ai_input ? 1 : 0,
                (unsigned long long)engine.FrameIndex());
    const std::string printed=testing::internal::GetCapturedStdout();
    std::fwrite(printed.data(),1,printed.size(),stdout);
    EXPECT_NE(printed.find("consumer: 1.55"),std::string::npos);
}

// These are the same three former shell metadata/version predicates.
#include <filesystem>
#include <fstream>
#include <string>
TEST(InstalledConsumer, BuildMetadata) {
    auto read=[](const std::string &name){
        std::filesystem::path path(std::u8string(reinterpret_cast<const char8_t*>(name.data()),name.size()));
        std::ifstream file(path);EXPECT_TRUE(file.is_open())<<name;
        return std::string((std::istreambuf_iterator<char>(file)),{});
    };
    EXPECT_EQ(read(TEST_CONSUMER_SOURCE).find("CXX_STANDARD"),std::string::npos)
        <<"consumer must declare no explicit C++ standard";
    EXPECT_NE(read(TEST_PACKAGE_TARGETS).find("cxx_std_23"),std::string::npos);
}

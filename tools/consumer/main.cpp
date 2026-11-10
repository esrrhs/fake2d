#include "fake2d/engine.h"
#include "fake2d/version.h"
#include <cstdio>
int main() {
    fake2d::EngineConfig cfg;
    fake2d::Engine engine;
    auto &ui = engine.GetUI();
    if (!ui.ConfigureVariableList("api", {"stable-a", "stable-b"}, 24, "v1")) return 2;
    if (!ui.SetVariableRowText("api", "stable-a", "UTF-8 田", .6f)) return 5;
    ui.SetVariableMeasureBudget(64);
    if (ui.UnscopedWarnings() != 0) return 6;
    ui.SetTextValue("edit", "UTF-8 田");
    ui.NewFrame();
    if (!ui.BeginFrameUI() || !ui.EndFrameUI()) return 3;
    if (ui.VariableListTotal("api") != 48 || ui.TextValue("edit") != "UTF-8 田") return 4;
    std::printf("consumer: %s, ai_input default %d, frame %llu\n",
                FAKE2D_VERSION_STRING, cfg.ai_input ? 1 : 0,
                (unsigned long long)engine.FrameIndex());
    return 0;
}

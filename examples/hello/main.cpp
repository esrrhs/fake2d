#include "fake2d/engine.h"

#include <cstdio>

int main() {
    fake2d::EngineConfig cfg;
    cfg.title = "fake2d hello";
    cfg.width = 960;
    cfg.height = 540;
    cfg.script_entry = "scripts/main.lua";

    fake2d::Engine engine;
    if (!engine.Init(cfg)) {
        std::fprintf(stderr, "fake2d_hello: engine init failed\n");
        return 1;
    }
    return engine.Run();
}

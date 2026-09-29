#include "fake2d/engine.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>

int main(int argc, char **argv) {
    fake2d::EngineConfig cfg;
    cfg.title = "fake2d hello";
    cfg.width = 960;
    cfg.height = 540;
    cfg.script_entry = "scripts/main.lua";

    int max_frames = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--headless") {
            cfg.headless = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            max_frames = std::atoi(argv[++i]);
        }
    }

    fake2d::Engine engine;
    if (!engine.Init(cfg)) {
        std::fprintf(stderr, "fake2d_hello: engine init failed\n");
        return 1;
    }
    return engine.Run(max_frames);
}

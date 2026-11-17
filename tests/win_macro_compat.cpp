// Regression for MinGW/MSVC windef.h macros (`near`/`far` expand to empty).
//
// Windows CI failed in examples/hello/main.cpp at `auto near =` after
// render/gl.h pulled in windows.h. This file injects the same macros,
// repeats that include order (public headers, then a later GL/windows
// include), and uses the colliding identifiers so Linux/macOS catch the
// break without waiting for a Windows job.

#include <cmath>
#include <cstdlib>

#define far
#define near
#define FAR far
#define NEAR near

#include "fake2d/engine.h"
#include "fake2d/i18n.h"
#include "fake2d/lighting.h"
#include "fake2d/log.h"
#include "fake2d/object_snapshot.h"
#include "fake2d/procedural_tex.h"
#include "fake2d/province_overlay.h"
#include "fake2d/renderer.h"
#include "fake2d/save_store.h"
#include "fake2d/ui.h"
#include "fake2d/ui_test_runner.h"
#ifdef FAKE2D_WITH_FAKELUA
#include "fake2d/lua_ui.h"
#endif

// A later windows.h (the gl.h path on MinGW) would re-define the macros
// after the public headers have already been processed.
#define far
#define near
#define FAR far
#define NEAR near
#include "render/gl.h"

int main() {
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
    struct ShadowPoint {
        float near;
        float far;
    };
    const ShadowPoint p{1.0f, 2.0f};
    const bool ok = near(p.near, 1.0f) && near(p.far, 2.0f);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

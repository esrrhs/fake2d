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
#include "fake2d/lighting.h"
#include "fake2d/renderer.h"

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

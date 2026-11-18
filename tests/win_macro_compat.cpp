// Regression for MinGW/MSVC windef.h/wingdi.h macros (`near`/`far` expand
// to empty, `GetObject` becomes GetObjectA/GetObjectW).
//
// Windows CI failed in examples/hello/main.cpp at `auto near =` after
// render/gl.h pulled in windows.h, and later in coverage_regression when
// NativeObject::GetObject was rewritten to GetObjectA. This file injects
// the same macros, repeats that include order (public headers, then a
// later GL/windows include), and uses the colliding identifiers so
// Linux/macOS catch the break without waiting for a Windows job.

#include <cmath>
#include <cstdlib>

#define far
#define near
#define FAR far
#define NEAR near
#define GetObject GetObjectA

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
#define GetObject GetObjectA
#include "render/gl.h"

int main() {
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
    struct ShadowPoint {
        float near;
        float far;
    };
    const ShadowPoint p{1.0f, 2.0f};
    // If GetObject is still #defined to GetObjectA, this member pointer
    // looks up a missing GetObjectA and fails to compile — the same
    // rewrite that left an undefined reference on Windows CI.
    struct NativeProbe {
        void *GetObject() { return nullptr; }
    };
    void *(NativeProbe::*get_object)() = &NativeProbe::GetObject;
    const bool ok = near(p.near, 1.0f) && near(p.far, 2.0f) && get_object != nullptr;
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

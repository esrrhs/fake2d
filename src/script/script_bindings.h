#pragma once

#include "fakelua.h"

namespace fake2d {

class Engine;

/// Registers every fake2d native function on a FakeLua state:
/// draw primitives, camera, sprite, input, and time modules.
void RegisterScriptApi(fakelua::State *state, Engine *engine);

} // namespace fake2d

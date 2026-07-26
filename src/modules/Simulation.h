#pragma once
#include "flecs.h"

namespace Simulation {

// The fixed-step phases, listed in the order the game loop runs them.
//
// flecs orders systems inside one phase by registration order, and
// buildPipeline() does no topological sort, so a phase shared by two modules
// used to make the result depend on the order main.cpp imports them. Where two
// modules genuinely need a specific order, they now declare different phases:
// the phase IS the ordering contract, and it is checked by the compiler.

struct PrePhysics {};  // write gameplay intent into the physics world
struct PhysicsStep {}; // advance box2d

// Physics results are read back into Position here...
struct PostPhysics {};
// ...and only then may sensor events from that step be consumed (Stairs needs
// the synced positions).
struct PostPhysicsEvents {};

// Gameplay that owns Position, e.g. clamping the player inside the world...
struct FixedUpdate {};
// ...and gameplay that must observe the final Position of this tick.
struct FixedUpdateLate {};

} // namespace Simulation

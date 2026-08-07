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

struct ResolveMovement {}; // convert the latest control intent into requested velocity
struct PrePhysics {};      // write gameplay intent into the physics world
struct PhysicsStep {}; // advance box2d

// Physics results are read back into Position here...
struct PostPhysics {};
// ...and only then may sensor events from that step be consumed (Stairs needs
// the synced positions).
struct PostPhysicsEvents {};

// Gameplay that consumes the synced physics result...
struct FixedUpdate {};
// ...and gameplay that must observe the final Position of this tick.
struct FixedUpdateLate {};

// Runs one complete fixed simulation transaction. Systems still register into
// the phases above; callers do not need to know or reproduce their ordering.
void RunFixedTick(flecs::world &world, float deltaTime);

} // namespace Simulation

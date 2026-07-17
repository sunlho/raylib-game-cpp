#pragma once
#include "flecs.h"

namespace Simulation {

struct PreUpdate {};
struct PrePhysics {};
struct PhysicsStep {};
struct PostPhysics {};
struct FixedUpdate {};
struct PostUpdate {};

struct TestUpdate {};

} // namespace Simulation

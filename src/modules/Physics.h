#pragma once

#include "box2d/box2d.h"
#include "flecs.h"

namespace Physics {

typedef struct PhysicsWorld {
  b2WorldId id;
  float timeStep;
} PhysicsWorld;

typedef struct PhysicsBody {
  b2BodyId id;
} PhysicsBody;

extern b2WorldId Id;

struct module {
  module(flecs::world &world);
};

void changeDebugDrawSystemEnabled();

} // namespace Physics

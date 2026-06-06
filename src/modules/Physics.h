#include "flecs.h"

namespace Physics {

typedef struct PhysicsWorld {
  b2WorldId id;
  float timeStep;
} PhysicsWorld;

void Import(flecs::world &world);
void CreateBox2DWorld(flecs::world &world, float step);
} // namespace Physics

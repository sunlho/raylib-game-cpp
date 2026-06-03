#include "flecs.h"

namespace Physics {

typedef struct PhysicsWorld {
  b2WorldId id;
} PhysicsWorld;

void Import(flecs::world &world);
void CreateBox2DWorld(flecs::world &world);
} // namespace Physics

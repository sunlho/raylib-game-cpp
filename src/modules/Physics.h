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

void Import(flecs::world &world);
void CreateBox2DWorld(flecs::world &world, float step);
} // namespace Physics

#include "box2d/box2d.h"

#include "Physics.h"
#include "Reflection.h"
#include "Simulation.h"

namespace Physics {
namespace {

} // namespace

b2WorldId Id = b2_nullWorldId;

void Import(flecs::world &world) {
  Reflection::Register<b2WorldId>(world);
  Reflection::Register<b2BodyId>(world);
  Reflection::Register<PhysicsWorld>(world);

  CreateBox2DWorld(world, 1.0f / 60.0f);

  world.system<const PhysicsWorld>("fixed update")
      .kind<Simulation::FixedUpdate>()
      .each([](flecs::iter &it, size_t i, const PhysicsWorld &world) {
        b2World_Step(world.id, world.timeStep, 4);
      });
}

void CreateBox2DWorld(flecs::world &world, float step) {
  if (b2World_IsValid(Id)) {
    return;
  }

  b2WorldDef worldDef = b2DefaultWorldDef();
  worldDef.gravity = b2Vec2{0.0f, 0.0f};
  const auto worldId = b2CreateWorld(&worldDef);

  Physics::Id = worldId;

  world.entity("box2d world")
      .add<PhysicsWorld>()
      .set<PhysicsWorld>({worldId, step});
}

} // namespace Physics

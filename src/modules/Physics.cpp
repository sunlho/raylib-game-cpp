#include "box2d/box2d.h"

#include "Physics.h"
#include "Reflection.h"

namespace Physics {
namespace {

float timeStep = 1.0f / 60.0f;
float accumulator = 0.0f;

void fixedUpdate(flecs::iter &it, size_t i, const PhysicsWorld &world) {
  accumulator += it.delta_time();
  while (accumulator >= timeStep) {
    b2World_Step(world.id, timeStep, 1);
    accumulator -= timeStep;
  }
}

} // namespace

void Import(flecs::world &world) {
  world.system<const PhysicsWorld>("fixed update")
      .kind(flecs::PreUpdate)
      .each(fixedUpdate);
}

void CreateBox2DWorld(flecs::world &world) {
  Reflection::Register<b2WorldId>(world);
  Reflection::Register<PhysicsWorld>(world);

  b2WorldDef worldDef = b2DefaultWorldDef();
  b2WorldId worldId = b2CreateWorld(&worldDef);

  world.entity("box2d world")
      .add<PhysicsWorld>()
      .set<PhysicsWorld>({worldId});
}

} // namespace Physics

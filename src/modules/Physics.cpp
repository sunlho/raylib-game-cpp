#include "box2d/box2d.h"

#include "Physics.h"
#include "Reflection.h"
#include "Simulation.h"

namespace Physics {
namespace {

} // namespace

void Import(flecs::world &world) {
  world.system<const PhysicsWorld>("fixed update")
      .kind<Simulation::FixedUpdate>()
      .each([](flecs::iter &it, size_t i, const PhysicsWorld &world) {
        b2World_Step(world.id, world.timeStep, 1);
      });
}

void CreateBox2DWorld(flecs::world &world, float step) {
  Reflection::Register<b2WorldId>(world);
  Reflection::Register<PhysicsWorld>(world);

  b2WorldDef worldDef = b2DefaultWorldDef();
  b2WorldId worldId = b2CreateWorld(&worldDef);

  world.entity("box2d world")
      .add<PhysicsWorld>()
      .set<PhysicsWorld>({worldId, step});
}

} // namespace Physics

#include <memory.h>

#include "box2d/box2d.h"

#include "Debug/PhysicsDebugDraw.h"
#include "Physics.h"
#include "Reflection.h"
#include "Rendering.h"
#include "Simulation.h"

namespace Physics {
namespace {

bool disableDebugDraw = true;
flecs::system drawDebugSystem;
static std::unique_ptr<b2DebugDraw> debugDraw;

} // namespace

b2WorldId Id = b2_nullWorldId;

module::module(flecs::world &world) {
  Reflection::Register<b2WorldId>(world);
  Reflection::Register<b2BodyId>(world);
  Reflection::Register<PhysicsWorld>(world);

  if (!b2World_IsValid(Id)) {
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = b2Vec2{0.0f, 0.0f};
    const auto worldId = b2CreateWorld(&worldDef);

    Physics::Id = worldId;

    world.entity("Box2d World")
        .add<PhysicsWorld>()
        .set<PhysicsWorld>({worldId, 1.0f / 60.0f});
  }

  world.system<const PhysicsWorld>("Fixed Update")
      .kind<Simulation::FixedUpdate>()
      .each([](flecs::iter &it, size_t i, const PhysicsWorld &world) {
        b2World_Step(world.id, world.timeStep, 1);
      });

  world.observer<b2BodyId>("Destroy Body Observer")
      .event(flecs::OnRemove)
      .each([](flecs::entity, const b2BodyId &bodyId) {
        b2DestroyBody(bodyId);
      });
}

void DebugDraw() {
#if !defined(NDEBUG)
  if (!debugDraw) {
    debugDraw = std::make_unique<b2DebugDraw>(PhysicsDebugDraw::CreateDebugDraw());
  }

  b2World_Draw(Physics::Id, debugDraw.get());
#endif
}

} // namespace Physics

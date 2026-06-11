#include "box2d/box2d.h"

#include "Debug/PhysicsDebugDraw.h"
#include "Physics.h"
#include "Reflection.h"
#include "Rendering.h"
#include "Simulation.h"

namespace Physics {
namespace {

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
        b2World_Step(world.id, world.timeStep, 4);
      });

  world.observer<b2BodyId>("Destroy Body Observer")
      .event(flecs::OnRemove)
      .each([](flecs::entity, const b2BodyId &bodyId) {
        b2DestroyBody(bodyId);
      });

#if !defined(NDEBUG)
  world.component<b2DebugDraw>()
      .add(flecs::Singleton);
  world.set<b2DebugDraw>(PhysicsDebugDraw::CreateDebugDraw());

  world.system("Draw Physics Debug World")
      .kind<Rendering::Phases::Draw>()
      .run([](flecs::iter &it) {
        auto world = it.world();
        auto debugDraw = world.get<b2DebugDraw>();
        b2World_Draw(Physics::Id, &debugDraw);
      });
#endif
}

} // namespace Physics

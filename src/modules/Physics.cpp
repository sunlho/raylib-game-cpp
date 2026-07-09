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

void *EncodeEntityUserData(flecs::entity_t entityId) {
  static_assert(sizeof(flecs::entity_t) <= sizeof(uintptr_t));
  return reinterpret_cast<void *>(static_cast<uintptr_t>(entityId));
}

flecs::entity_t DecodeEntityUserData(void *userData) {
  return static_cast<flecs::entity_t>(reinterpret_cast<uintptr_t>(userData));
}

void AttachEntityUserData(const PhysicsBody &physicsBody, flecs::entity_t entityId) {
  void *userData = EncodeEntityUserData(entityId);

  if (b2Body_IsValid(physicsBody.id)) {
    b2Body_SetUserData(physicsBody.id, userData);
  }

  if (b2Shape_IsValid(physicsBody.shapeId)) {
    b2Shape_SetUserData(physicsBody.shapeId, userData);
  }
}

flecs::entity_t GetEntityFromShape(b2ShapeId shapeId) {
  if (!b2Shape_IsValid(shapeId)) {
    return 0;
  }

  void *shapeData = b2Shape_GetUserData(shapeId);
  if (shapeData) {
    return DecodeEntityUserData(shapeData);
  }

  const b2BodyId bodyId = b2Shape_GetBody(shapeId);
  if (!b2Body_IsValid(bodyId)) {
    return 0;
  }

  return DecodeEntityUserData(b2Body_GetUserData(bodyId));
}

module::module(flecs::world &world) {
  debugDraw = std::make_unique<b2DebugDraw>(PhysicsDebugDraw::CreateDebugDraw());

  Reflection::Register<b2WorldId>(world);
  Reflection::Register<b2BodyId>(world);
  Reflection::Register<PhysicsWorld>(world);
  Reflection::Register<PhysicsBody>(world);

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
        b2World_Step(world.id, world.timeStep, 2);
      });

  world.observer<PhysicsBody>("Destroy Body Observer")
      .event(flecs::OnRemove)
      .each([](flecs::iter &it, size_t i, PhysicsBody &physicsBody) {
        if (b2Body_IsValid(physicsBody.id)) {
          b2DestroyBody(physicsBody.id);
        }
      });

  world.observer<PhysicsBody>("Attach Physics Body User Data")
      .event(flecs::OnSet)
      .each([](flecs::entity entity, PhysicsBody &physicsBody) {
        AttachEntityUserData(physicsBody, entity.id());
      });
}

void DebugDraw() {
#if !defined(NDEBUG)
  b2World_Draw(Physics::Id, debugDraw.get());
#endif
}

} // namespace Physics

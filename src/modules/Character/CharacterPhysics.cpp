#include "box2d/box2d.h"

#include "CharacterInternal.h"
#include "modules/Physics.h"
#include "modules/Rendering.h"

namespace Character {

void RegisterCharacterPhysics(flecs::world &world) {
  world.observer<Physics::PhysicsBody, Rendering::Position>("Create Character Physics Observer")
      .with<PlayerTag>()
      .event(flecs::OnSet)
      .each([](Physics::PhysicsBody &physicsBody, Rendering::Position &position) {
        if (b2Body_IsValid(physicsBody.id)) {
          b2Body_SetTransform(physicsBody.id, b2Vec2{position.value.x, position.value.y}, b2Rot{1.0f, 0.0f});
          return;
        }
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = b2Vec2{position.value.x, position.value.y};
        bodyDef.fixedRotation = true;

        b2Circle circle = {0.0f, 10.0f, 10.0f};
        b2BodyId body = b2CreateBody(Physics::Id, &bodyDef);
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = 1.0f;
        shapeDef.material.friction = 0.3f;
        b2ShapeId shapeId = b2CreateCircleShape(body, &shapeDef, &circle);
        physicsBody.id = body;
        physicsBody.shapeId = shapeId;
      });
}

void TestChangeCharacterPhysicsShapeCenter(flecs::world &world) {
  world.each([](flecs::entity e, Physics::PhysicsBody &physicsBody, Rendering::Position &pos) {
    if (b2Body_IsValid(physicsBody.id)) {
      const auto shape = b2Shape_GetCircle(physicsBody.shapeId);

      if (shape.center.y == 0.0f) {
        b2Circle circle = {0.0f, 10.0f, shape.radius};
        b2Shape_SetCircle(physicsBody.shapeId, &circle);
      } else {
        b2Circle circle = {0.0f, 0.0f, shape.radius};
        b2Shape_SetCircle(physicsBody.shapeId, &circle);
      }
    }
  });
}

} // namespace Character

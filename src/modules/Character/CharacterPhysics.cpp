#include "box2d/box2d.h"

#include "CharacterInternal.h"
#include "modules/Physics.h"
#include "modules/Rendering.h"

namespace Character {

void RegisterCharacterPhysics(flecs::world &world) {
  world.observer<CharacterInfo, SpriteSet, Rendering::Position, Physics::PhysicsBody>("Create Character Physics")
      .event(flecs::OnSet)
      .each([](CharacterInfo &info, SpriteSet &spriteSet, Rendering::Position &position, Physics::PhysicsBody &physicsBody) {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = b2Vec2{position.value.x, position.value.y};

        b2Circle circle = {0.0f, 0.0f, 10.0f};
        b2BodyId body = b2CreateBody(Physics::Id, &bodyDef);
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = 1.0f;
        shapeDef.material.friction = 0.3f;
        b2CreateCircleShape(body, &shapeDef, &circle);
        physicsBody.id = body;
      });
}

} // namespace Character

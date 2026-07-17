#pragma once

#include "box2d/box2d.h"
#include "flecs.h"

namespace Physics {

typedef struct PhysicsWorld {
  b2WorldId id;
} PhysicsWorld;

typedef struct PhysicsBody {
  b2BodyId id;
  b2ShapeId shapeId;
} PhysicsBody;

extern b2WorldId Id;

struct module {
  module(flecs::world &world);
};

void *EncodeEntityUserData(flecs::entity_t entityId);
flecs::entity_t DecodeEntityUserData(void *userData);
void AttachEntityUserData(const PhysicsBody &physicsBody, flecs::entity_t entityId);
flecs::entity_t GetEntityFromShape(b2ShapeId shapeId);

void DebugDraw();

} // namespace Physics

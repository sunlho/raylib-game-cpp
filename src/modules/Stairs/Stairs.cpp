#include "Stairs.h"

#include <algorithm>
#include <cmath>

#include "raymath.h"

#include "modules/Physics.h"
#include "modules/Reflection.h"
#include "modules/Rendering.h"
#include "modules/Simulation.h"

namespace Stairs {
namespace {

float ResolveDirectionX(const StairData &stair) {
  if (stair.directionX > 0.0f) {
    return 1.0f;
  }

  if (stair.directionX < 0.0f) {
    return -1.0f;
  }

  return 0.0f;
}

Vector2 ResolveLowEnd(const StairData &stair) {
  const float directionX = ResolveDirectionX(stair);
  const float left = stair.bounds.x;
  const float right = stair.bounds.x + stair.bounds.width;
  const float bottom = stair.bounds.y + stair.bounds.height;
  const float centerX = stair.bounds.x + stair.bounds.width * 0.5f;

  if (directionX > 0.0f) {
    return Vector2{left, bottom};
  }

  if (directionX < 0.0f) {
    return Vector2{right, bottom};
  }

  return Vector2{centerX, bottom};
}

Vector2 ResolveHighEnd(const StairData &stair) {
  const float directionX = ResolveDirectionX(stair);
  const float left = stair.bounds.x;
  const float right = stair.bounds.x + stair.bounds.width;
  const float top = stair.bounds.y;
  const float centerX = stair.bounds.x + stair.bounds.width * 0.5f;

  if (directionX > 0.0f) {
    return Vector2{right, top};
  }

  if (directionX < 0.0f) {
    return Vector2{left, top};
  }

  return Vector2{centerX, top};
}

void ResetToBase(FloorState &state) {
  state.floor = state.baseFloor;
  state.onStair = false;
}

void ApplyStair(const Vector2 &samplePoint, const StairData &stair, FloorState &state) {
  const Vector2 lowEnd = ResolveLowEnd(stair);
  const Vector2 highEnd = ResolveHighEnd(stair);
  const Vector2 delta = Vector2Subtract(highEnd, lowEnd);
  const float length = std::max(1.0f, Vector2Length(delta));
  const Vector2 axis = Vector2Scale(delta, 1.0f / length);
  const Vector2 rel = Vector2Subtract(samplePoint, lowEnd);
  const float t = Clamp(Vector2DotProduct(rel, axis) / length, 0.0f, 1.0f);

  state.floor = t > stair.floorSwitchT ? stair.highFloor : stair.lowFloor;
  state.onStair = true;
}

bool ContainsStair(const FloorState &state, flecs::entity_t stairId) {
  return std::find(state.overlappingStairs.begin(), state.overlappingStairs.end(), stairId) != state.overlappingStairs.end();
}

void AddStairOverlap(FloorState &state, flecs::entity_t stairId) {
  if (!ContainsStair(state, stairId)) {
    state.overlappingStairs.push_back(stairId);
  }

  state.currentStair = stairId;
}

void RemoveStairOverlap(FloorState &state, flecs::entity_t stairId) {
  state.overlappingStairs.erase(
      std::remove(state.overlappingStairs.begin(), state.overlappingStairs.end(), stairId),
      state.overlappingStairs.end());

  if (state.overlappingStairs.empty()) {
    state.baseFloor = state.floor;
  }

  if (state.currentStair == stairId) {
    state.currentStair = state.overlappingStairs.empty() ? 0 : state.overlappingStairs.back();
  }
}

flecs::entity WrapEntity(flecs::world world, flecs::entity_t entityId) {
  return flecs::entity(world.c_ptr(), entityId);
}

void HandleSensorBegin(flecs::world &world, b2ShapeId sensorShapeId, b2ShapeId visitorShapeId) {
  const flecs::entity_t stairId = Physics::GetEntityFromShape(sensorShapeId);
  const flecs::entity_t visitorId = Physics::GetEntityFromShape(visitorShapeId);

  if (stairId == 0 || visitorId == 0 || !ecs_is_alive(world.c_ptr(), stairId) || !ecs_is_alive(world.c_ptr(), visitorId)) {
    return;
  }

  flecs::entity stairEntity = WrapEntity(world, stairId);
  flecs::entity visitorEntity = WrapEntity(world, visitorId);

  if (!stairEntity.try_get<StairData>()) {
    return;
  }

  FloorState *state = visitorEntity.try_get_mut<FloorState>();
  if (!state) {
    return;
  }

  AddStairOverlap(*state, stairId);
  visitorEntity.modified<FloorState>();
}

void HandleSensorEnd(flecs::world &world, b2ShapeId sensorShapeId, b2ShapeId visitorShapeId) {
  const flecs::entity_t stairId = Physics::GetEntityFromShape(sensorShapeId);
  const flecs::entity_t visitorId = Physics::GetEntityFromShape(visitorShapeId);

  if (stairId == 0 || visitorId == 0 || !ecs_is_alive(world.c_ptr(), visitorId)) {
    return;
  }

  flecs::entity visitorEntity = WrapEntity(world, visitorId);
  FloorState *state = visitorEntity.try_get_mut<FloorState>();
  if (!state) {
    return;
  }

  RemoveStairOverlap(*state, stairId);
  visitorEntity.modified<FloorState>();
}

b2BodyId CreateStairSensorBody(const StairData &stair, b2ShapeId &shapeId) {
  shapeId = b2_nullShapeId;
  if (!stair.enabled || stair.bounds.width <= 0.0f || stair.bounds.height <= 0.0f) {
    return b2_nullBodyId;
  }

  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = b2_staticBody;
  bodyDef.position = b2Vec2{
      stair.bounds.x + stair.bounds.width * 0.5f,
      stair.bounds.y + stair.bounds.height * 0.5f};

  b2BodyId bodyId = b2CreateBody(Physics::Id, &bodyDef);

  b2ShapeDef shapeDef = b2DefaultShapeDef();
  shapeDef.isSensor = true;
  shapeDef.enableSensorEvents = true;
  shapeDef.density = 0.0f;

  const b2Polygon box = b2MakeBox(
      std::max(stair.bounds.width * 0.5f, 0.5f),
      std::max(stair.bounds.height * 0.5f, 0.5f));
  shapeId = b2CreatePolygonShape(bodyId, &shapeDef, &box);
  return bodyId;
}

} // namespace

module::module(flecs::world &world) {
  Reflection::Register<FloorState>(world);
  Reflection::Register<StairData>(world);

  world.observer<StairData>("Create Stair Sensor Body")
      .event(flecs::OnSet)
      .each([](flecs::entity entity, StairData &stair) {
        if (const auto *existingBody = entity.try_get<Physics::PhysicsBody>()) {
          if (b2Body_IsValid(existingBody->id)) {
            b2DestroyBody(existingBody->id);
          }
        }

        b2ShapeId shapeId = b2_nullShapeId;
        const b2BodyId bodyId = CreateStairSensorBody(stair, shapeId);
        Physics::PhysicsBody physicsBody{bodyId, shapeId};
        Physics::AttachEntityUserData(physicsBody, entity.id());
        entity.set<Physics::PhysicsBody>(physicsBody);
      });

  world.system("Process Stair Sensor Events")
      .kind<Simulation::FixedUpdate>()
      .run([](flecs::iter &it) {
        flecs::world world = it.world();
        const b2SensorEvents events = b2World_GetSensorEvents(Physics::Id);

        for (int i = 0; i < events.beginCount; ++i) {
          HandleSensorBegin(world, events.beginEvents[i].sensorShapeId, events.beginEvents[i].visitorShapeId);
        }

        for (int i = 0; i < events.endCount; ++i) {
          HandleSensorEnd(world, events.endEvents[i].sensorShapeId, events.endEvents[i].visitorShapeId);
        }
      });

  world.system<const Rendering::Position, FloorState>("Update Stair Floor State")
      .kind<Simulation::FixedUpdate>()
      .each([](flecs::iter &it, size_t, const Rendering::Position &position, FloorState &state) {
        const Vector2 samplePoint = Vector2Add(position.value, state.sampleOffset);

        ResetToBase(state);

        if (state.currentStair == 0 || !ecs_is_alive(it.world().c_ptr(), state.currentStair)) {
          state.currentStair = 0;
          state.overlappingStairs.clear();
          return;
        }

        flecs::entity stairEntity = WrapEntity(it.world(), state.currentStair);
        const StairData *stair = stairEntity.try_get<StairData>();
        if (!stair || !stair->enabled) {
          state.currentStair = 0;
          state.overlappingStairs.clear();
          return;
        }

        ApplyStair(samplePoint, *stair, state);
      });

  world.system<const FloorState, Rendering::RenderComponent>("Sync Floor State To Render Component")
      .kind<Simulation::FixedUpdate>()
      .each([](const FloorState &state, Rendering::RenderComponent &renderComponent) {
        renderComponent.floor = state.floor;
      });

  world.system<const FloorState, const Physics::PhysicsBody>("Change Character Physics Shape Center Based On Floor State")
      .kind<Simulation::FixedUpdate>()
      .each([](const FloorState &state, const Physics::PhysicsBody &physicsBody) {
        if (!b2Body_IsValid(physicsBody.id)) {
          return;
        }

        const auto shape = b2Shape_GetCircle(physicsBody.shapeId);
        const float yOffset = (state.floor - 1.5f) * 10.0f;
        b2Circle circle = {0.0f, yOffset, shape.radius};
        b2Shape_SetCircle(physicsBody.shapeId, &circle);
      });
}

} // namespace Stairs

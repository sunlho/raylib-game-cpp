#include "Stairs.h"

#include <algorithm>
#include <cmath>

#include "raymath.h"

#include "modules/Physics.h"
#include "modules/Reflection.h"
#include "modules/Rendering.h"
#include "modules/Runtime/RuntimePhases.h"

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

void HandleSensorBegin(flecs::world &world, flecs::entity_t stairId, flecs::entity_t visitorId) {
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

void HandleSensorEnd(flecs::world &world, flecs::entity_t stairId, flecs::entity_t visitorId) {
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

} // namespace

module::module(flecs::world &world) {
  Reflection::Register<FloorState>(world);
  Reflection::Register<StairData>(world);

  world.observer<StairData>("Create Stair Sensor Body")
      .event(flecs::OnSet)
      .each([](flecs::entity entity, StairData &stair) {
        if (!stair.enabled) {
          Physics::DestroyBody(entity);
          return;
        }

        Physics::CreateBoxSensor(entity, stair.bounds);
      });

  world.system("Process Stair Sensor Events")
      .kind<Runtime::Phases::PostPhysics>()
      .run([](flecs::iter &it) {
        flecs::world world = it.world();
        for (const auto &event : Physics::SensorEvents(world)) {
          if (event.kind == Physics::SensorEventKind::Begin) {
            HandleSensorBegin(world, event.sensor, event.visitor);
          } else {
            HandleSensorEnd(world, event.sensor, event.visitor);
          }
        }
      });

  world.system<const Rendering::Position, FloorState>("Update Stair Floor State")
      .kind<Runtime::Phases::FixedGameplay>()
      .each([](flecs::entity entity, const Rendering::Position &position, FloorState &state) {
        const Vector2 samplePoint = Vector2Add(position.value, state.sampleOffset);

        ResetToBase(state);

        if (state.currentStair == 0 || !ecs_is_alive(entity.world().c_ptr(), state.currentStair)) {
          state.currentStair = 0;
          state.overlappingStairs.clear();
        } else {
          flecs::entity stairEntity = WrapEntity(entity.world(), state.currentStair);
          const StairData *stair = stairEntity.try_get<StairData>();
          if (!stair || !stair->enabled) {
            state.currentStair = 0;
            state.overlappingStairs.clear();
          } else {
            ApplyStair(samplePoint, *stair, state);
          }
        }

        if (auto *renderComponent = entity.try_get_mut<Rendering::RenderComponent>()) {
          renderComponent->floor = state.floor;
          entity.modified<Rendering::RenderComponent>();
        }

        if (const auto *physicsBody = entity.try_get<Physics::PhysicsBody>()) {
          const float yOffset = (state.floor - 1.5f) * 10.0f;
          Physics::SetCircleCenter(*physicsBody, Vector2{0.0f, yOffset});
        }
      });
}

} // namespace Stairs

#pragma once

#include <cstdint>
#include <span>

#include "flecs.h"
#include "raylib.h"

namespace Physics {

struct PhysicsAccess;

struct PhysicsBody {
  PhysicsBody() = default;

private:
  std::uint64_t bodyToken = 0;
  std::uint64_t primaryShapeToken = 0;

  friend struct PhysicsAccess;
};

enum class StaticCollisionShape {
  Box,
  Ellipse,
  Polygon,
  Polyline,
};

enum class SensorEventKind {
  Begin,
  End,
};

struct SensorEvent {
  SensorEventKind kind = SensorEventKind::Begin;
  flecs::entity_t sensor = 0;
  flecs::entity_t visitor = 0;
};

struct module {
  module(flecs::world &world);
};

void CreateDynamicCircle(flecs::entity entity, Vector2 position, Vector2 center, float radius);
void CreateStaticCollision(
    flecs::entity entity,
    StaticCollisionShape shape,
    Rectangle worldBounds,
    std::span<const Vector2> worldPoints = {},
    float rotationDegrees = 0.0f);
void CreateBoxSensor(flecs::entity entity, Rectangle worldBounds);
void DestroyBody(flecs::entity entity);

void Relocate(const PhysicsBody &body, Vector2 position, bool clearVelocity = false);
void SetCircleCenter(const PhysicsBody &body, Vector2 center);

std::span<const SensorEvent> SensorEvents(flecs::world world);
void DebugDraw(flecs::world world);

} // namespace Physics

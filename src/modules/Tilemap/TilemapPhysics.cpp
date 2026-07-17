#include <algorithm>

#include "Tilemap.h"
#include "TilemapInternal.h"
#include "modules/Physics.h"

namespace Tilemap {
namespace {

Physics::StaticCollisionShape ToPhysicsShape(Tilemap::CollisionShape shape) {
  switch (shape) {
  case Tilemap::CollisionShape::Ellipse:
    return Physics::StaticCollisionShape::Ellipse;
  case Tilemap::CollisionShape::Polygon:
    return Physics::StaticCollisionShape::Polygon;
  case Tilemap::CollisionShape::Polyline:
    return Physics::StaticCollisionShape::Polyline;
  case Tilemap::CollisionShape::Rectangle:
  case Tilemap::CollisionShape::Point:
  case Tilemap::CollisionShape::Text:
  default:
    return Physics::StaticCollisionShape::Box;
  }
}

Rectangle BuildWorldRect(const Tilemap::CollisionData &collision, const Rectangle &tileRect) {
  const float width = std::max(collision.AABB.width, 1.0f);
  const float height = std::max(collision.AABB.height, 1.0f);

  return Rectangle{
      tileRect.x + collision.AABB.x,
      tileRect.y + collision.AABB.y,
      width,
      height};
}

std::vector<Vector2> BuildWorldPoints(const Tilemap::CollisionData &collision, const Rectangle &tileRect) {
  std::vector<Vector2> points;
  points.reserve(collision.points.size());

  const Vector2 origin = {
      tileRect.x + collision.position.x,
      tileRect.y + collision.position.y};

  for (const auto &point : collision.points) {
    points.push_back(Vector2{origin.x + point.x, origin.y + point.y});
  }

  return points;
}

Tilemap::CollisionData BuildCollisionEntityData(const Tilemap::CollisionData &collision, const Rectangle &tileRect, int layerIndex) {
  Tilemap::CollisionData data;
  data.shape = collision.shape;
  data.layerIndex = layerIndex;
  data.rotation = collision.rotation;
  data.position = collision.position;

  switch (collision.shape) {
  case Tilemap::CollisionShape::Rectangle:
  case Tilemap::CollisionShape::Ellipse:
    data.worldRect = BuildWorldRect(collision, tileRect);
    break;
  case Tilemap::CollisionShape::Polygon:
  case Tilemap::CollisionShape::Polyline:
    data.worldRect = BuildWorldRect(collision, tileRect);
    data.worldPoints = BuildWorldPoints(collision, tileRect);
    break;
  case Tilemap::CollisionShape::Point:
  case Tilemap::CollisionShape::Text:
  default:
    data.worldRect = Rectangle{
        tileRect.x + collision.position.x,
        tileRect.y + collision.position.y,
        1.0f,
        1.0f};
    break;
  }

  return data;
}

} // namespace

void CreateCollisionEntity(flecs::world &world, const std::vector<Tilemap::CollisionData> &collisions, const Rectangle &tileRect, int layerIndex, flecs::entity layerGroup) {

  for (const auto &collision : collisions) {
    auto collisionEntity = world.entity().add<Tilemap::CollisionData>();

    if (layerGroup.is_valid()) {
      collisionEntity.add(flecs::ChildOf, layerGroup);
    }

    const auto collisionData = BuildCollisionEntityData(collision, tileRect, layerIndex);
    collisionEntity.set<Tilemap::CollisionData>(collisionData);
    Physics::CreateStaticCollision(
        collisionEntity,
        ToPhysicsShape(collisionData.shape),
        collisionData.worldRect,
        collisionData.worldPoints,
        collisionData.rotation);
  }
}

} // namespace Tilemap

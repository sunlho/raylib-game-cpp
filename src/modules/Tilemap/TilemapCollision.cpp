#include <algorithm>
#include <cmath>
#include <utility>

#include "box2d/box2d.h"

#include "Tilemap.h"
#include "TilemapInternal.h"
#include "modules/Physics.h"

namespace Tilemap {

namespace {

std::vector<b2Vec2> BuildRelativePoints(const std::vector<Vector2> &points, const Vector2 &origin) {
  std::vector<b2Vec2> result;
  result.reserve(points.size());

  for (const auto &point : points) {
    result.push_back(b2Vec2{point.x - origin.x, point.y - origin.y});
  }

  return result;
}

std::vector<b2Vec2> BuildEllipsePoints(const Rectangle &worldRect) {
  constexpr int segments = 16;
  std::vector<b2Vec2> points;
  points.reserve(segments);

  for (int i = 0; i < segments; ++i) {
    const float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * PI;
    points.push_back(b2Vec2{
        std::cos(angle) * (worldRect.width * 0.5f),
        std::sin(angle) * (worldRect.height * 0.5f)});
  }

  return points;
}

b2BodyId CreatePhysicsBody(const Tilemap::CollisionData &data, const b2WorldId &physicsWorld) {
  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = b2_staticBody;
  bodyDef.position = b2Vec2{data.worldRect.x + (data.worldRect.width * 0.5f), data.worldRect.y + (data.worldRect.height * 0.5f)};
  bodyDef.rotation = b2MakeRot(data.rotation * DEG2RAD);

  b2BodyId bodyId = b2CreateBody(physicsWorld, &bodyDef);
  b2ShapeDef shapeDef = b2DefaultShapeDef();

  switch (data.shape) {
  case Tilemap::CollisionShape::Rectangle:
  case Tilemap::CollisionShape::Point:
  case Tilemap::CollisionShape::Text: {
    const b2Polygon box = b2MakeBox(std::max(data.worldRect.width * 0.5f, 0.5f), std::max(data.worldRect.height * 0.5f, 0.5f));
    b2CreatePolygonShape(bodyId, &shapeDef, &box);
    break;
  }
  case Tilemap::CollisionShape::Ellipse: {
    const auto points = BuildEllipsePoints(data.worldRect);
    b2ChainDef chainDef = b2DefaultChainDef();
    chainDef.points = points.data();
    chainDef.count = static_cast<int>(points.size());
    chainDef.isLoop = true;
    b2CreateChain(bodyId, &chainDef);
    break;
  }
  case Tilemap::CollisionShape::Polygon: {
    const Vector2 center = {data.worldRect.x + (data.worldRect.width * 0.5f), data.worldRect.y + (data.worldRect.height * 0.5f)};
    const auto points = BuildRelativePoints(data.worldPoints, center);
    if (!points.empty()) {
      b2ChainDef chainDef = b2DefaultChainDef();
      chainDef.points = points.data();
      chainDef.count = static_cast<int>(points.size());
      chainDef.isLoop = true;
      b2CreateChain(bodyId, &chainDef);
    }
    break;
  }
  case Tilemap::CollisionShape::Polyline: {
    const Vector2 center = {data.worldRect.x + (data.worldRect.width * 0.5f), data.worldRect.y + (data.worldRect.height * 0.5f)};
    const auto points = BuildRelativePoints(data.worldPoints, center);
    if (!points.empty()) {
      b2ChainDef chainDef = b2DefaultChainDef();
      chainDef.points = points.data();
      chainDef.count = static_cast<int>(points.size());
      chainDef.isLoop = false;
      b2CreateChain(bodyId, &chainDef);
    }
    break;
  }
  default:
    break;
  }

  return bodyId;
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

void CreateCollisionEntity(flecs::world &world, b2WorldId physicsWorld, const std::vector<Tilemap::CollisionData> &collisions, const Rectangle &tileRect, int layerIndex, flecs::entity layerGroup) {

  for (const auto &collision : collisions) {
    auto collisionEntity = world.entity().add<Tilemap::CollisionData>().add<Physics::PhysicsBody>();

    if (layerGroup.is_valid()) {
      collisionEntity.add(flecs::ChildOf, layerGroup);
    }

    const auto collisionData = BuildCollisionEntityData(collision, tileRect, layerIndex);
    const b2BodyId bodyId = CreatePhysicsBody(collisionData, physicsWorld);
    collisionEntity.set<Physics::PhysicsBody>({bodyId});
    collisionEntity.set<Tilemap::CollisionData>(BuildCollisionEntityData(collision, tileRect, layerIndex));
  }
}

} // namespace Tilemap

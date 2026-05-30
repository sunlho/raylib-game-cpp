#include <algorithm>
#include <cmath>
#include <utility>

#include "box2d/box2d.h"

#include "TilemapInternal.h"

namespace TilemapInternal {

namespace {

struct TilemapCollisionData {
  TileCollisionShape shape = TileCollisionShape::Rectangle;
  Rectangle worldRect = {0};
  std::vector<Vector2> worldPoints;
  Vector2 localPosition = {0.0f, 0.0f};
  float rotation = 0.0f;
  int layerIndex = 0;
};

b2Vec2 ToB2Vec2(const Vector2 &value) {
  return b2Vec2{value.x, value.y};
}

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

b2BodyId CreatePhysicsBody(const TilemapCollisionData &data, const b2WorldId &physicsWorld) {
  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = b2_staticBody;
  bodyDef.position = ToB2Vec2(Vector2{data.worldRect.x + (data.worldRect.width * 0.5f), data.worldRect.y + (data.worldRect.height * 0.5f)});
  bodyDef.rotation = b2MakeRot(data.rotation * DEG2RAD);

  b2BodyId bodyId = b2CreateBody(physicsWorld, &bodyDef);
  b2ShapeDef shapeDef = b2DefaultShapeDef();

  switch (data.shape) {
  case TileCollisionShape::Rectangle:
  case TileCollisionShape::Point:
  case TileCollisionShape::Text: {
    const b2Polygon box = b2MakeBox(std::max(data.worldRect.width * 0.5f, 0.5f), std::max(data.worldRect.height * 0.5f, 0.5f));
    b2CreatePolygonShape(bodyId, &shapeDef, &box);
    break;
  }
  case TileCollisionShape::Ellipse: {
    const auto points = BuildEllipsePoints(data.worldRect);
    b2ChainDef chainDef = b2DefaultChainDef();
    chainDef.points = points.data();
    chainDef.count = static_cast<int>(points.size());
    chainDef.isLoop = true;
    b2CreateChain(bodyId, &chainDef);
    break;
  }
  case TileCollisionShape::Polygon: {
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
  case TileCollisionShape::Polyline: {
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

Rectangle BuildWorldRect(const TileCollision &collision, const Rectangle &tileRect) {
  const float width = std::max(collision.AABB.width, 1.0f);
  const float height = std::max(collision.AABB.height, 1.0f);

  return Rectangle{
      tileRect.x + collision.AABB.x,
      tileRect.y + collision.AABB.y,
      width,
      height};
}

std::vector<Vector2> BuildWorldPoints(const TileCollision &collision, const Rectangle &tileRect) {
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

TilemapCollisionData BuildCollisionEntityData(const TileCollision &collision, const Rectangle &tileRect, int layerIndex) {
  TilemapCollisionData data;
  data.shape = collision.shape;
  data.layerIndex = layerIndex;
  data.rotation = collision.rotation;
  data.localPosition = collision.position;

  switch (collision.shape) {
  case TileCollisionShape::Rectangle:
  case TileCollisionShape::Ellipse:
    data.worldRect = BuildWorldRect(collision, tileRect);
    break;
  case TileCollisionShape::Polygon:
  case TileCollisionShape::Polyline:
    data.worldRect = BuildWorldRect(collision, tileRect);
    data.worldPoints = BuildWorldPoints(collision, tileRect);
    break;
  case TileCollisionShape::Point:
  case TileCollisionShape::Text:
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

void RegisterCollisionObservers(flecs::world &world) {
  world.observer<b2BodyId>("Destroy Tilemap Collision Body")
      .event(flecs::OnRemove)
      .each([](flecs::entity, const b2BodyId &bodyId) {
        b2DestroyBody(bodyId);
      });

  world.observer<const TilemapCollisionData>("Create Tilemap Collision Body")
      .event(flecs::OnAdd)
      .each([](flecs::entity entity, const TilemapCollisionData &data) {
        auto worldEntity = entity.world().singleton<b2WorldId>();
        if (!worldEntity.is_valid()) {
          return;
        }

        const b2WorldId physicsWorld = worldEntity.get<b2WorldId>();
        const b2BodyId bodyId = CreatePhysicsBody(data, physicsWorld);
        entity.set<b2BodyId>(bodyId);
      });
}

void CreateCollisionEntity(flecs::world &world, const std::vector<TileCollision> &collisions, const Rectangle &tileRect, int layerIndex, flecs::entity layerGroup) {
  world.component<TilemapCollisionData>();

  for (const auto &collision : collisions) {
    auto collisionEntity = world.entity();

    if (layerGroup.is_valid()) {
      collisionEntity.add(flecs::ChildOf, layerGroup);
    }

    collisionEntity.set<TilemapCollisionData>(BuildCollisionEntityData(collision, tileRect, layerIndex));
  }
}

} // namespace TilemapInternal

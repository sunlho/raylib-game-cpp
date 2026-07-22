#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

#include "box2d/box2d.h"

#include "Debug/PhysicsDebugDraw.h"
#include "Movement.h"
#include "Physics.h"
#include "Rendering.h"
#include "Simulation.h"

namespace Physics {
namespace {

constexpr float LengthUnitsPerMeter = 48.0f;
constexpr int EllipseSegments = 16;

struct PhysicsWorld {
  b2WorldId id = b2_nullWorldId;
  std::vector<SensorEvent> sensorEvents;
};

std::unique_ptr<b2DebugDraw> debugDraw;

template <typename Id>
std::uint64_t EncodeId(Id id) {
  static_assert(std::is_trivially_copyable_v<Id>);
  static_assert(sizeof(Id) <= sizeof(std::uint64_t));

  std::uint64_t token = 0;
  std::memcpy(&token, &id, sizeof(Id));
  return token;
}

template <typename Id>
Id DecodeId(std::uint64_t token) {
  static_assert(std::is_trivially_copyable_v<Id>);
  static_assert(sizeof(Id) <= sizeof(std::uint64_t));

  Id id{};
  std::memcpy(&id, &token, sizeof(Id));
  return id;
}

} // namespace

struct PhysicsAccess {
  static PhysicsBody Create(b2BodyId bodyId, b2ShapeId shapeId) {
    PhysicsBody body;
    body.bodyToken = EncodeId(bodyId);
    body.primaryShapeToken = EncodeId(shapeId);
    return body;
  }

  static b2BodyId BodyId(const PhysicsBody &body) {
    return DecodeId<b2BodyId>(body.bodyToken);
  }

  static b2ShapeId ShapeId(const PhysicsBody &body) {
    return DecodeId<b2ShapeId>(body.primaryShapeToken);
  }
};

namespace {

void *EncodeEntity(flecs::entity_t entity) {
  static_assert(sizeof(flecs::entity_t) <= sizeof(std::uintptr_t));
  return reinterpret_cast<void *>(static_cast<std::uintptr_t>(entity));
}

flecs::entity_t DecodeEntity(void *userData) {
  return static_cast<flecs::entity_t>(reinterpret_cast<std::uintptr_t>(userData));
}

flecs::entity_t GetEntityFromShape(b2ShapeId shapeId) {
  if (!b2Shape_IsValid(shapeId)) {
    return 0;
  }

  if (void *shapeData = b2Shape_GetUserData(shapeId)) {
    return DecodeEntity(shapeData);
  }

  const b2BodyId bodyId = b2Shape_GetBody(shapeId);
  if (!b2Body_IsValid(bodyId)) {
    return 0;
  }

  return DecodeEntity(b2Body_GetUserData(bodyId));
}

void AttachEntity(b2BodyId bodyId, b2ShapeId shapeId, flecs::entity_t entity) {
  void *userData = EncodeEntity(entity);
  if (b2Body_IsValid(bodyId)) {
    b2Body_SetUserData(bodyId, userData);
  }
  if (b2Shape_IsValid(shapeId)) {
    b2Shape_SetUserData(shapeId, userData);
  }
}

PhysicsWorld &GetWorld(flecs::world world) {
  return world.get_mut<PhysicsWorld>();
}

void DestroyRawBody(const PhysicsBody &body);

void ReplaceBody(flecs::entity entity, b2BodyId bodyId, b2ShapeId shapeId) {
  if (const auto *existing = entity.try_get<PhysicsBody>()) {
    DestroyRawBody(*existing);
  }

  AttachEntity(bodyId, shapeId, entity.id());
  entity.set<PhysicsBody>(PhysicsAccess::Create(bodyId, shapeId));
}

std::vector<b2Vec2> BuildRelativePoints(std::span<const Vector2> points, Vector2 origin) {
  std::vector<b2Vec2> result;
  result.reserve(points.size());
  for (const auto &point : points) {
    result.push_back(b2Vec2{point.x - origin.x, point.y - origin.y});
  }
  return result;
}

std::vector<b2Vec2> BuildEllipsePoints(Rectangle bounds) {
  std::vector<b2Vec2> points;
  points.reserve(EllipseSegments);
  for (int i = 0; i < EllipseSegments; ++i) {
    const float angle = static_cast<float>(i) / static_cast<float>(EllipseSegments) * 2.0f * PI;
    points.push_back(b2Vec2{
        std::cos(angle) * (bounds.width * 0.5f),
        std::sin(angle) * (bounds.height * 0.5f)});
  }
  return points;
}

void AppendSensorEvent(
    std::vector<SensorEvent> &events,
    SensorEventKind kind,
    b2ShapeId sensorShapeId,
    b2ShapeId visitorShapeId) {
  const flecs::entity_t sensor = GetEntityFromShape(sensorShapeId);
  const flecs::entity_t visitor = GetEntityFromShape(visitorShapeId);
  if (sensor == 0 || visitor == 0) {
    return;
  }

  events.push_back(SensorEvent{kind, sensor, visitor});
}

void CaptureSensorEvents(PhysicsWorld &world) {
  const b2SensorEvents source = b2World_GetSensorEvents(world.id);
  world.sensorEvents.clear();
  world.sensorEvents.reserve(static_cast<std::size_t>(source.beginCount + source.endCount));

  for (int i = 0; i < source.beginCount; ++i) {
    AppendSensorEvent(
        world.sensorEvents,
        SensorEventKind::Begin,
        source.beginEvents[i].sensorShapeId,
        source.beginEvents[i].visitorShapeId);
  }

  for (int i = 0; i < source.endCount; ++i) {
    AppendSensorEvent(
        world.sensorEvents,
        SensorEventKind::End,
        source.endEvents[i].sensorShapeId,
        source.endEvents[i].visitorShapeId);
  }
}

void DestroyRawBody(const PhysicsBody &body) {
  const b2BodyId bodyId = PhysicsAccess::BodyId(body);
  if (b2Body_IsValid(bodyId)) {
    b2DestroyBody(bodyId);
  }
}

} // namespace

module::module(flecs::world &world) {
  debugDraw = std::make_unique<b2DebugDraw>(PhysicsDebugDraw::CreateDebugDraw());

  world.component<PhysicsWorld>();
  world.component<PhysicsBody>();

  world.observer<PhysicsBody>("Destroy Physics Body")
      .event(flecs::OnRemove)
      .each([](PhysicsBody &body) {
        DestroyRawBody(body);
      });

  world.observer<PhysicsWorld>("Destroy Physics World")
      .event(flecs::OnRemove)
      .each([](PhysicsWorld &physicsWorld) {
        if (b2World_IsValid(physicsWorld.id)) {
          b2DestroyWorld(physicsWorld.id);
          physicsWorld.id = b2_nullWorldId;
        }
        physicsWorld.sensorEvents.clear();
      });

  b2SetLengthUnitsPerMeter(LengthUnitsPerMeter);
  b2WorldDef worldDef = b2DefaultWorldDef();
  worldDef.gravity = b2Vec2{0.0f, 0.0f};
  world.set<PhysicsWorld>({b2CreateWorld(&worldDef), {}});

  world.system<const Movement::Velocity, const PhysicsBody>("Apply Entity Velocity")
      .kind<Simulation::PrePhysics>()
      .each([](const Movement::Velocity &velocity, const PhysicsBody &body) {
        const b2BodyId bodyId = PhysicsAccess::BodyId(body);
        if (b2Body_IsValid(bodyId)) {
          b2Body_SetLinearVelocity(bodyId, b2Vec2{velocity.value.x, velocity.value.y});
        }
      });

  world.system("Step Physics World")
      .kind<Simulation::PhysicsStep>()
      .run([](flecs::iter &it) {
        auto &physicsWorld = GetWorld(it.world());
        if (!b2World_IsValid(physicsWorld.id)) {
          physicsWorld.sensorEvents.clear();
          return;
        }

        b2World_Step(physicsWorld.id, it.delta_time(), 2);
        CaptureSensorEvents(physicsWorld);
      });

  world.system<Rendering::Position, const PhysicsBody>("Sync Physics Positions")
      .kind<Simulation::PostPhysics>()
      .each([](Rendering::Position &position, const PhysicsBody &body) {
        const b2BodyId bodyId = PhysicsAccess::BodyId(body);
        if (!b2Body_IsValid(bodyId)) {
          return;
        }

        const b2Vec2 bodyPosition = b2Body_GetPosition(bodyId);
        position.value = Vector2{bodyPosition.x, bodyPosition.y};
      });
}

void CreateDynamicCircle(flecs::entity entity, Vector2 position, Vector2 center, float radius) {
  auto &physicsWorld = GetWorld(entity.world());
  if (!b2World_IsValid(physicsWorld.id) || radius <= 0.0f) {
    DestroyBody(entity);
    return;
  }

  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = b2_dynamicBody;
  bodyDef.position = b2Vec2{position.x, position.y};
  bodyDef.fixedRotation = true;

  const b2BodyId bodyId = b2CreateBody(physicsWorld.id, &bodyDef);
  b2ShapeDef shapeDef = b2DefaultShapeDef();
  shapeDef.density = 1.0f;
  shapeDef.material.friction = 0.3f;
  shapeDef.enableSensorEvents = true;

  const b2Circle circle = {center.x, center.y, radius};
  const b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
  ReplaceBody(entity, bodyId, shapeId);
}

void CreateStaticCollision(
    flecs::entity entity,
    StaticCollisionShape shape,
    Rectangle worldBounds,
    std::span<const Vector2> worldPoints,
    float rotationDegrees) {
  auto &physicsWorld = GetWorld(entity.world());
  if (!b2World_IsValid(physicsWorld.id)) {
    DestroyBody(entity);
    return;
  }

  const Vector2 center = {
      worldBounds.x + worldBounds.width * 0.5f,
      worldBounds.y + worldBounds.height * 0.5f};

  if ((shape == StaticCollisionShape::Polygon && worldPoints.size() < 3) ||
      (shape == StaticCollisionShape::Polyline && worldPoints.size() < 2)) {
    DestroyBody(entity);
    return;
  }

  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = b2_staticBody;
  bodyDef.position = b2Vec2{center.x, center.y};
  bodyDef.rotation = b2MakeRot(rotationDegrees * DEG2RAD);

  const b2BodyId bodyId = b2CreateBody(physicsWorld.id, &bodyDef);
  b2ShapeId primaryShapeId = b2_nullShapeId;
  b2ShapeDef shapeDef = b2DefaultShapeDef();

  if (shape == StaticCollisionShape::Box) {
    const b2Polygon box = b2MakeBox(
        std::max(worldBounds.width * 0.5f, 0.5f),
        std::max(worldBounds.height * 0.5f, 0.5f));
    primaryShapeId = b2CreatePolygonShape(bodyId, &shapeDef, &box);
  } else {
    std::vector<b2Vec2> points = shape == StaticCollisionShape::Ellipse
                                     ? BuildEllipsePoints(worldBounds)
                                     : BuildRelativePoints(worldPoints, center);

    b2ChainDef chainDef = b2DefaultChainDef();
    chainDef.points = points.data();
    chainDef.count = static_cast<int>(points.size());
    chainDef.isLoop = shape != StaticCollisionShape::Polyline;
    b2CreateChain(bodyId, &chainDef);
  }

  ReplaceBody(entity, bodyId, primaryShapeId);
}

void CreateBoxSensor(flecs::entity entity, Rectangle worldBounds) {
  auto &physicsWorld = GetWorld(entity.world());
  if (!b2World_IsValid(physicsWorld.id) || worldBounds.width <= 0.0f || worldBounds.height <= 0.0f) {
    DestroyBody(entity);
    return;
  }

  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = b2_staticBody;
  bodyDef.position = b2Vec2{
      worldBounds.x + worldBounds.width * 0.5f,
      worldBounds.y + worldBounds.height * 0.5f};

  const b2BodyId bodyId = b2CreateBody(physicsWorld.id, &bodyDef);
  b2ShapeDef shapeDef = b2DefaultShapeDef();
  shapeDef.isSensor = true;
  shapeDef.enableSensorEvents = true;
  shapeDef.density = 0.0f;

  const b2Polygon box = b2MakeBox(
      std::max(worldBounds.width * 0.5f, 0.5f),
      std::max(worldBounds.height * 0.5f, 0.5f));
  const b2ShapeId shapeId = b2CreatePolygonShape(bodyId, &shapeDef, &box);
  ReplaceBody(entity, bodyId, shapeId);
}

void DestroyBody(flecs::entity entity) {
  if (entity.has<PhysicsBody>()) {
    entity.remove<PhysicsBody>();
  }
}

void Relocate(const PhysicsBody &body, Vector2 position, bool clearVelocity) {
  const b2BodyId bodyId = PhysicsAccess::BodyId(body);
  if (!b2Body_IsValid(bodyId)) {
    return;
  }

  b2Body_SetTransform(bodyId, b2Vec2{position.x, position.y}, b2Body_GetRotation(bodyId));
  if (clearVelocity) {
    b2Body_SetLinearVelocity(bodyId, b2Vec2{0.0f, 0.0f});
  }
}

void SetCircleCenter(const PhysicsBody &body, Vector2 center) {
  const b2ShapeId shapeId = PhysicsAccess::ShapeId(body);
  if (!b2Shape_IsValid(shapeId)) {
    return;
  }

  b2Circle circle = b2Shape_GetCircle(shapeId);
  circle.center = b2Vec2{center.x, center.y};
  b2Shape_SetCircle(shapeId, &circle);
}

std::span<const SensorEvent> SensorEvents(flecs::world world) {
  const auto &events = world.get<PhysicsWorld>().sensorEvents;
  return std::span<const SensorEvent>(events.data(), events.size());
}

void DebugDraw(flecs::world world) {
#if !defined(NDEBUG)
  const auto &physicsWorld = world.get<PhysicsWorld>();
  if (b2World_IsValid(physicsWorld.id)) {
    b2World_Draw(physicsWorld.id, debugDraw.get());
  }
#else
  (void)world;
#endif
}

} // namespace Physics

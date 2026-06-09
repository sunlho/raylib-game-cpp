#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "box2d/box2d.h"
#include "raylib.h"

#include "PhysicsDebugDraw.h"
#include "modules/Physics.h"
#include "modules/Reflection.h"
#include "modules/Rendering.h"

namespace {

Color ToRaylibColor(b2HexColor color) {
  return Color{
      static_cast<unsigned char>((color >> 16) & 0xFF),
      static_cast<unsigned char>((color >> 8) & 0xFF),
      static_cast<unsigned char>(color & 0xFF),
      192};
}

Vector2 ToVector2(b2Vec2 value) {
  return Vector2{value.x, value.y};
}

Vector2 TransformPoint(b2Transform transform, b2Vec2 localPoint) {
  return Vector2{
      transform.p.x + (transform.q.c * localPoint.x - transform.q.s * localPoint.y),
      transform.p.y + (transform.q.s * localPoint.x + transform.q.c * localPoint.y)};
}

void DrawPolygonOutline(const b2Vec2 *vertices, int vertexCount, b2HexColor color, void *context) {
  (void)context;
  if (vertexCount < 2) {
    return;
  }

  const Color raylibColor = ToRaylibColor(color);
  for (int i = 0; i < vertexCount; ++i) {
    const int nextIndex = (i + 1) % vertexCount;
    DrawLineEx(ToVector2(vertices[i]), ToVector2(vertices[nextIndex]), 1.0f, raylibColor);
  }
}

void DrawPolygonFill(const b2Vec2 *vertices, int vertexCount, b2HexColor color) {
  if (vertexCount < 3) {
    return;
  }

  const Color raylibColor = ToRaylibColor(color);
  const Vector2 origin = ToVector2(vertices[0]);
  for (int i = 1; i < vertexCount - 1; ++i) {
    DrawTriangle(origin, ToVector2(vertices[i]), ToVector2(vertices[i + 1]), raylibColor);
  }
}

void DrawLineSegment(b2Vec2 p1, b2Vec2 p2, b2HexColor color, void *context) {
  (void)context;
  DrawLineEx(ToVector2(p1), ToVector2(p2), 1.0f, ToRaylibColor(color));
}

void DrawDebugPoint(b2Vec2 p, float size, b2HexColor color, void *context) {
  (void)context;
  DrawCircleV(ToVector2(p), std::max(size * 0.5f, 1.0f), ToRaylibColor(color));
}

void DrawDebugString(b2Vec2 p, const char *s, b2HexColor color, void *context) {
  (void)context;
  if (!s) {
    return;
  }

  DrawText(s, static_cast<int>(p.x), static_cast<int>(p.y) - 10, 10, ToRaylibColor(color));
}

void DrawDebugTransform(b2Transform transform, void *context) {
  (void)context;
  const Vector2 origin = ToVector2(transform.p);
  const float axisLength = 0.5f;

  const Vector2 xAxis = Vector2{
      origin.x + transform.q.c * axisLength,
      origin.y + transform.q.s * axisLength};
  const Vector2 yAxis = Vector2{
      origin.x - transform.q.s * axisLength,
      origin.y + transform.q.c * axisLength};

  DrawLineEx(origin, xAxis, 1.0f, RED);
  DrawLineEx(origin, yAxis, 1.0f, GREEN);
}

void DrawDebugCapsule(b2Vec2 p1, b2Vec2 p2, float radius, b2HexColor color, void *context) {
  (void)context;
  const Color raylibColor = ToRaylibColor(color);
  const Vector2 v1 = ToVector2(p1);
  const Vector2 v2 = ToVector2(p2);

  DrawLineEx(v1, v2, std::max(radius * 2.0f, 1.0f), raylibColor);
  DrawCircleV(v1, radius, raylibColor);
  DrawCircleV(v2, radius, raylibColor);
}

void DrawDebugSolidCapsule(b2Vec2 p1, b2Vec2 p2, float radius, b2HexColor color, void *context) {
  (void)context;
  const Color raylibColor = ToRaylibColor(color);
  const Vector2 v1 = ToVector2(p1);
  const Vector2 v2 = ToVector2(p2);

  DrawLineEx(v1, v2, std::max(radius * 2.0f, 1.0f), raylibColor);
  DrawCircleV(v1, radius, raylibColor);
  DrawCircleV(v2, radius, raylibColor);
}

void DrawDebugCircle(b2Vec2 center, float radius, b2HexColor color, void *context) {
  (void)context;
  DrawCircleLinesV(ToVector2(center), radius, ToRaylibColor(color));
}

void DrawDebugSolidCircle(b2Transform transform, float radius, b2HexColor color, void *context) {
  (void)context;
  const Color raylibColor = ToRaylibColor(color);
  const Vector2 center = ToVector2(transform.p);

  DrawCircleV(center, radius, raylibColor);

  const Vector2 axis = Vector2{
      center.x + transform.q.c * radius,
      center.y + transform.q.s * radius};
  DrawLineEx(center, axis, 1.0f, BLACK);
}

void DrawDebugSolidPolygon(b2Transform transform, const b2Vec2 *vertices, int vertexCount, float radius, b2HexColor color, void *context) {
  (void)radius;
  (void)context;

  if (vertexCount < 3) {
    return;
  }

  std::vector<b2Vec2> worldVertices;
  worldVertices.reserve(static_cast<std::size_t>(vertexCount));
  for (int i = 0; i < vertexCount; ++i) {
    const Vector2 worldPoint = TransformPoint(transform, vertices[i]);
    worldVertices.push_back(b2Vec2{worldPoint.x, worldPoint.y});
  }

  DrawPolygonFill(worldVertices.data(), vertexCount, color);
  DrawPolygonOutline(worldVertices.data(), vertexCount, color, nullptr);
}

b2DebugDraw CreateDebugDraw() {
  b2DebugDraw draw = b2DefaultDebugDraw();
  draw.DrawPolygonFcn = DrawPolygonOutline;
  draw.DrawSolidPolygonFcn = DrawDebugSolidPolygon;
  draw.DrawCircleFcn = DrawDebugCircle;
  draw.DrawSolidCircleFcn = DrawDebugSolidCircle;
  draw.DrawSolidCapsuleFcn = DrawDebugSolidCapsule;
  draw.DrawSegmentFcn = DrawLineSegment;
  draw.DrawTransformFcn = DrawDebugTransform;
  // draw.DrawPointFcn = DrawDebugPoint;
  draw.DrawStringFcn = DrawDebugString;
  draw.drawShapes = true;
  draw.drawJoints = true;
  draw.useDrawingBounds = false;
  draw.drawBounds = false;
  draw.drawMass = false;
  draw.drawBodyNames = false;
  draw.drawGraphColors = false;
  draw.drawContactFeatures = false;
  draw.drawContactNormals = false;
  draw.drawContactImpulses = false;
  draw.drawFrictionImpulses = false;
  draw.drawIslands = false;
  return draw;
}

#if !defined(NDEBUG)
void DrawPhysicsDebugWorld(flecs::iter &it) {
  auto world = it.world();
  auto debugDraw = world.get<b2DebugDraw>();
  b2World_Draw(Physics::Id, &debugDraw);
}
#endif

} // namespace

void PhysicsDebugDraw::Import(flecs::world &world) {
#if !defined(NDEBUG)
  world.component<b2DebugDraw>()
      .add(flecs::Singleton);
  world.set<b2DebugDraw>(CreateDebugDraw());

  world.system("Draw Physics Debug World")
      .kind<Rendering::Phases::Draw>()
      .run(DrawPhysicsDebugWorld);
#else
  (void)world;
#endif
}

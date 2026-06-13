
#include <functional>
#include <vector>

#include "raylib.h"

#include "DebugDraw.h"

namespace DebugDraw {
namespace {

static std::vector<DrawData> drawQueue = {};

} // namespace

void EnqueueDraw(const DrawData &data) {
  drawQueue.push_back(data);
}

void ProcessDrawQueue() {
  for (const auto &data : drawQueue) {
    switch (data.type) {
    case DrawType::Point:
      DrawPixelV(data.pos, data.color);
      break;
    case DrawType::Line:
      DrawLineEx(data.pos, data.endPos, data.lineThick, data.color);
      break;
    case DrawType::Circle:
      DrawCircleV(data.pos, data.radius, data.color);
      break;
    case DrawType::Rectangle:
      DrawRectanglePro(data.rect, Vector2{0.0f, 0.0f}, data.rotation, data.color);
      break;
    case DrawType::RectangleLines:
      DrawRectangleLinesEx(data.rect, data.lineThick, data.color);
      break;
    case DrawType::Text:
      DrawText(data.text.c_str(), static_cast<int>(data.pos.x), static_cast<int>(data.pos.y), data.fontSize, data.color);
      break;
    }
  }
  drawQueue.clear();
}

} // namespace DebugDraw

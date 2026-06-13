#include <string>

#include "raylib.h"

namespace DebugDraw {

enum class DrawType {
  Point,
  Line,
  Circle,
  Rectangle,
  RectangleLines,
  Text
};

struct DrawData {
  DrawType type = DrawType::Point;
  Vector2 pos = {0, 0};
  Vector2 endPos = {0, 0};
  Rectangle rect = {0, 0, 0, 0};
  float radius = 0.0f;
  float rotation = 0.0f;
  float lineThick = 0.5f;
  int fontSize = 10;
  std::string text = "";
  Color color = WHITE;
};

void EnqueueDraw(const DrawData &data);
void ProcessDrawQueue();

} // namespace DebugDraw

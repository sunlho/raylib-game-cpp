#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "flecs.h"
#include "raylib.h"

namespace Rendering {

struct Phases {
  struct PreDraw {};
  struct Background {};
  struct Draw {};
  struct PostDraw {};
};

struct Position {
  Vector2 value;
};

struct Renderable {
  virtual ~Renderable() = default;
  virtual void Draw(const Position &position) const = 0;
};

struct RenderComponent {
  std::shared_ptr<Renderable> object;
  float floor = 2.5f;
  int sortY = 0;
  bool visible = true;
};

struct SortableTag {};

struct RenderVisibility {
  bool visible = true;
};

struct MainWindow {};

struct RenderTargetSize {
  Vector2 dimension;
};

struct RenderTargetState {
  bool active = false;
};

static inline int GetSortYByLayer(int layerIndex, int posY) {
  return layerIndex * 10000 + posY;
}

struct module {
  module(flecs::world &world);
};

} // namespace Rendering

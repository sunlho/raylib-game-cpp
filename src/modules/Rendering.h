#pragma once

#include <memory>
#include <string>

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
  virtual void Draw(flecs::entity entity, const Position &position) const = 0;
};

struct RenderComponent {
  std::shared_ptr<Renderable> object;
  int sortY = 0;
  bool visible = true;
};

struct RenderSortTag {};

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

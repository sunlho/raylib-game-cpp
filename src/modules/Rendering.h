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
  virtual void Draw(const Position &position) const = 0;
};

struct RenderComponent {
  std::shared_ptr<Renderable> object;
  bool visible = true;
};

struct CircleRenderable final : Renderable {
  Color color;
  float radius;

  CircleRenderable(Color color, float radius)
      : color(color), radius(radius) {
  }

  void Draw(const Position &position) const override {
    DrawCircleV(position.value, radius, color);
  }
};

struct MainWindow {};

struct RenderTargetSize {
  Vector2 dimension;
};

struct RenderTargetState {
  bool active = false;
};

struct module {
  module(flecs::world &world);
};

} // namespace Rendering

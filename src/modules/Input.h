#pragma once

#include "flecs.h"
#include "raylib.h"

namespace Input {

// Device-independent intent sampled once per render frame. Movement consumes
// the latest value on the fixed simulation clock.
struct PlayerControlIntent {
  Vector2 movement = {0.0f, 0.0f};
  bool run = false;
};

void Update(flecs::world &world);

struct module {
  module(flecs::world &world);
};

} // namespace Input

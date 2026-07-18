#pragma once

#include "flecs.h"
#include "raylib.h"

namespace Movement {

struct Velocity {
  Vector2 value = {0.0f, 0.0f};
};

struct MoveSpeed {
  float value;
};

struct PlayerControlled {};
struct CameraFollowTag {};

struct module {
  module(flecs::world &world);
};

} // namespace Movement

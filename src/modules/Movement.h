#pragma once

#include "flecs.h"
#include "raylib.h"

namespace Movement {
struct Phases {
  struct Update {};
  struct CameraFollow {};
};

struct Velocity {
  Vector2 value = {0.0f, 0.0f};
};

struct MoveSpeed {
  float value = 180.0f;
};

struct PlayerControlled {};
struct CameraFollowTag {};

void Import(flecs::world &world);

} // namespace Movement

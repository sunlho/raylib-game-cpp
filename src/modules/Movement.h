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
  float value;
};

struct RunSettings {
  float speedMultiplier = 1.6f;
  float accelerationTime = 0.2f;
};

struct RunState {
  bool active = false;
  float progress = 0.0f;
};

struct PlayerControlled {};
struct CameraFollowTag {};

struct module {
  module(flecs::world &world);
};

} // namespace Movement

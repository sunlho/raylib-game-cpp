#pragma once

#include "flecs.h"
#include "raylib.h"

namespace Movement {

struct RequestedVelocity {
  Vector2 value = {0.0f, 0.0f};
};

struct PlayerMovementSettings {
  float walkSpeed = 100.0f;
  float runSpeedMultiplier = 1.6f;
  float runAccelerationTime = 0.2f;
};

void EnablePlayerMovement(
    flecs::entity player,
    PlayerMovementSettings settings = {});

struct module {
  module(flecs::world &world);
};

} // namespace Movement

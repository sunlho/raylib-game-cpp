#pragma once

#include "flecs.h"
#include "raylib.h"

namespace GameCamera {

struct MainCamera {
  Camera2D value = {
      Vector2{0.0f, 0.0f},
      Vector2{0.0f, 0.0f},
      0.0f,
      1.0f};
  bool enabled = true;
  bool autoCenterOffset = true;
  Vector2 followOffset = {0.0f, 0.0f};
  float followSpeed = 6.0f;
  bool snapTargetToPixel = true;
  Vector2 previousFollowTarget = {0.0f, 0.0f};
  bool hasPreviousFollowTarget = false;
  Vector2 followRenderPosition = {0.0f, 0.0f};
  bool useFollowRenderPosition = false;
};

void Begin2D(flecs::world &world);
void End2D(const flecs::world &world);

struct module {
  module(flecs::world &world);
};

} // namespace GameCamera

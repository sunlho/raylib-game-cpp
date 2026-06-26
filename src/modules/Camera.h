#pragma once

#include "flecs.h"
#include "raylib.h"

namespace GameCamera {

struct Phases {
  struct Begin2D {};
  struct End2D {};
};

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
};

struct module {
  module(flecs::world &world);
};

} // namespace GameCamera

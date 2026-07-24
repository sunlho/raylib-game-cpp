#pragma once

#include "flecs.h"
#include "raylib.h"

namespace GameCamera {

enum class FollowMotion { Idle, Walk, Attack, Run };

struct FocusProxy {
  Vector2 direction = {0.0f, 1.0f};
  Vector2 offset = {0.0f, 0.0f};
  float distance = 0.0f;
};

struct MainCamera {
  Camera2D value = {
      Vector2{0.0f, 0.0f},
      Vector2{0.0f, 0.0f},
      0.0f,
      1.0f};
  bool enabled = true;
  bool autoCenterOffset = true;
  Vector2 smoothTarget = {0.0f, 0.0f};
  Vector2 renderTarget = {0.0f, 0.0f};
  FocusProxy focus{};
  float followSpeed = 5.66f;
  float pixelsPerWorldUnit = 2.0f;
  bool snapToRenderGrid = true;
};

void Begin2D(flecs::world &world);
void End2D(const flecs::world &world);
void SnapTo(flecs::world &world, Vector2 focus);

struct module {
  module(flecs::world &world);
};

} // namespace GameCamera

#pragma once

#include <vector>

#include "flecs.h"
#include "raylib.h"

namespace Stairs {

struct FloorState {
  float floor = 0.0f;
  float baseFloor = 0.0f;
  bool onStair = false;
  Vector2 sampleOffset = {0.0f, 0.0f};
  flecs::entity_t currentStair = 0;
  std::vector<flecs::entity_t> overlappingStairs;
};

struct StairData {
  Rectangle bounds = {0.0f, 0.0f, 0.0f, 0.0f};
  float directionX = 0.0f;
  float lowFloor = 0.0f;
  float highFloor = 1.0f;
  float floorSwitchT = 0.5f;
  bool enabled = true;
};

struct module {
  module(flecs::world &world);
};

} // namespace Stairs

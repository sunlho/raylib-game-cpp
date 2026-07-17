#pragma once

#include <string>

#include "flecs.h"
#include "raylib.h"

namespace MapManager {

struct MapBounds {
  Vector2 dimension = {0.0f, 0.0f};
};

void SetMapPath(flecs::world &world, const std::string &path);
bool TransitionToMap(flecs::world &world, std::string path, std::string hint = "Loading map...");

struct module {
  module(flecs::world &world);
};

} // namespace MapManager

#pragma once

#include <string>

#include "flecs.h"
#include "raylib.h"

namespace MapManager {

// Both of these only record a request; the map is materialised later, by
// ProcessPendingMapLoad. The world extent of the loaded map is published as
// Core::WorldBounds.
void SetMapPath(flecs::world &world, const std::string &path);
bool TransitionToMap(flecs::world &world, std::string path, std::string hint = "Loading map...");

// Materialises a pending map request, if any.
//
// Loading destroys the previous map root and immediately recreates entities
// with the same names ("MapLayer_0", "MapStair_0", ...). Inside a system or
// observer flecs is in deferred mode, so the destruct is queued and the new
// lookups would resolve to the not-yet-destroyed old entities. Call this from
// the game loop, once per frame, outside of any system or observer; if it is
// ever called while deferred it keeps the request pending instead.
void ProcessPendingMapLoad(flecs::world &world);

struct module {
  module(flecs::world &world);
};

} // namespace MapManager

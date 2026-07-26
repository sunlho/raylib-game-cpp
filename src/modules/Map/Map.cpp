#include <string>
#include <utility>

#include "Map.h"
#include "MapInternal.h"

#include "modules/Reflection.h"
#include "modules/Tilemap/Tilemap.h"

namespace MapManager {

module::module(flecs::world &world) {
  // The loaded map's extent is published as Core::WorldBounds, registered by
  // Core::module.
  Reflection::Register<Tilemap::CollisionData>(world);
  Reflection::Register<Internal::MapPath>(world);
  Reflection::Register<Internal::ActiveMapData>(world)
      .add(flecs::Singleton)
      .set<Internal::ActiveMapData>({});
  Reflection::Register<Internal::MapCacheState>(world)
      .add(flecs::Singleton)
      .set<Internal::MapCacheState>({});
  Reflection::Register<Internal::MapState>(world)
      .add(flecs::Singleton)
      .set<Internal::MapState>({});

  Internal::RegisterMapRendering(world);
}

void SetMapPath(flecs::world &world, const std::string &path) {
  auto mapEntity = world.entity("Map");
  mapEntity.set<Internal::MapPath>(Internal::MapPath{path});
}

void ProcessPendingMapLoad(flecs::world &world) {
  // Never materialise a map while flecs is deferring: destruct() of the old map
  // root would be queued, and the freshly created "MapLayer_0" / "MapStair_0"
  // would collide with the old entities that are still alive. The request stays
  // pending, so the next call outside the deferred scope picks it up.
  if (world.is_deferred()) {
    return;
  }

  const auto mapEntity = world.lookup("Map");
  if (!mapEntity.is_valid()) {
    return;
  }

  const auto *mapPath = mapEntity.try_get<Internal::MapPath>();
  if (mapPath == nullptr || mapPath->value.empty()) {
    return;
  }

  // No-op when the requested map is already the loaded one.
  Internal::LoadMapFromPath(world, mapPath->value);
}

bool TransitionToMap(flecs::world &world, std::string path, std::string hint) {
  return Rendering::RunLoadingSequence(
      world,
      {{1.0f, hint, [path = std::move(path)](flecs::world &loadingWorld) {
          SetMapPath(loadingWorld, path);
        }}});
}

} // namespace MapManager

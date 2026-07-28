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
  mapEntity.set<Internal::MapPath>(Internal::MapPath{path, false});
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

  const std::string requestedPath = mapPath->value;
  const bool forceReload = mapPath->forceReload;
  Internal::LoadMapFromPath(world, requestedPath, forceReload);

  // A forced reload is a one-shot request. Clearing it after the attempt also
  // prevents a malformed TMX file from being reparsed and logged every frame.
  if (forceReload) {
    mapEntity.set<Internal::MapPath>(Internal::MapPath{requestedPath, false});
  }
}

bool TransitionToMap(flecs::world &world, std::string path, std::string hint) {
  return Rendering::RunLoadingSequence(
      world,
      {{1.0f, hint, [path = std::move(path)](flecs::world &loadingWorld) {
          SetMapPath(loadingWorld, path);
        }}});
}

bool ReloadCurrentMap(flecs::world &world, std::string hint) {
  const std::string currentPath = GetCurrentMapPath(world);
  if (currentPath.empty()) {
    return false;
  }

  return Rendering::RunLoadingSequence(
      world,
      {{1.0f, hint, [path = currentPath](flecs::world &loadingWorld) {
          auto mapEntity = loadingWorld.entity("Map");
          mapEntity.set<Internal::MapPath>(Internal::MapPath{path, true});
        }}});
}

std::string GetCurrentMapPath(const flecs::world &world) {
  return world.get<Internal::MapState>().currentPath;
}

bool FindSpawnPoint(const flecs::world &world, std::string_view name, Vector2 &position) {
  const auto &spawnPoints = world.get<Internal::ActiveMapData>().spawnPoints;
  for (const auto &spawnPoint : spawnPoints) {
    if (spawnPoint.name == name) {
      position = spawnPoint.position;
      return true;
    }
  }
  return false;
}

std::vector<std::string> GetSpawnPointNames(const flecs::world &world) {
  const auto &spawnPoints = world.get<Internal::ActiveMapData>().spawnPoints;
  std::vector<std::string> names;
  names.reserve(spawnPoints.size());
  for (const auto &spawnPoint : spawnPoints) {
    names.push_back(spawnPoint.name);
  }
  return names;
}

bool FindSpawnPoint(
    flecs::world &world,
    const std::string &mapPath,
    std::string_view name,
    Vector2 &position) {
  auto &cacheState = world.get_mut<Internal::MapCacheState>();
  const auto *loadedMap = Internal::GetOrLoadMap(cacheState, mapPath);
  if (loadedMap == nullptr) {
    return false;
  }

  for (const auto &spawnPoint : loadedMap->spawnPoints) {
    if (spawnPoint.name == name) {
      position = spawnPoint.position;
      return true;
    }
  }
  return false;
}

std::vector<std::string> GetSpawnPointNames(flecs::world &world, const std::string &mapPath) {
  auto &cacheState = world.get_mut<Internal::MapCacheState>();
  const auto *loadedMap = Internal::GetOrLoadMap(cacheState, mapPath);
  if (loadedMap == nullptr) {
    return {};
  }

  std::vector<std::string> names;
  names.reserve(loadedMap->spawnPoints.size());
  for (const auto &spawnPoint : loadedMap->spawnPoints) {
    names.push_back(spawnPoint.name);
  }
  return names;
}

} // namespace MapManager

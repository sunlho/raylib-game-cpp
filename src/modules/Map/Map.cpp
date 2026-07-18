#include <string>
#include <utility>

#include "Map.h"
#include "MapInternal.h"

#include "modules/Reflection.h"
#include "modules/Tilemap/Tilemap.h"

namespace MapManager {

module::module(flecs::world &world) {
  Reflection::Register<MapBounds>(world)
      .add(flecs::Singleton)
      .set<MapBounds>({});
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
  Internal::RegisterMapLoader(world);
}

void SetMapPath(flecs::world &world, const std::string &path) {
  auto mapEntity = world.entity("Map");
  mapEntity.set<Internal::MapPath>(Internal::MapPath{path});
}

bool TransitionToMap(flecs::world &world, std::string path, std::string hint) {
  return Rendering::RunLoadingSequence(
      world,
      {{1.0f, hint, [path = std::move(path)](flecs::world &loadingWorld) {
          SetMapPath(loadingWorld, path);
        }}});
}

} // namespace MapManager

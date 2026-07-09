#include <string>

#include "Map.h"
#include "MapInternal.h"

#include "modules/Reflection.h"
#include "modules/Tilemap/Tilemap.h"

namespace MapManager {

module::module(flecs::world &world) {
  Reflection::Register<Tilemap::MapBounds>(world)
      .add(flecs::Singleton)
      .set<Tilemap::MapBounds>({});
  Reflection::Register<Tilemap::CollisionData>(world);
  Reflection::Register<MapPath>(world);
  Reflection::Register<ActiveMapData>(world)
      .add(flecs::Singleton)
      .set<ActiveMapData>({});
  Reflection::Register<MapCacheState>(world)
      .add(flecs::Singleton)
      .set<MapCacheState>({});
  Reflection::Register<MapState>(world)
      .add(flecs::Singleton)
      .set<MapState>({});

  RegisterMapRendering(world);
  RegisterMapLoader(world);
}

void SetMapPath(flecs::world &world, const std::string &path) {
  auto mapEntity = world.entity("Map");
  mapEntity.set<MapPath>(MapPath{path});
}

} // namespace MapManager

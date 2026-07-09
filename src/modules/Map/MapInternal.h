#pragma once

#include <cstddef>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>

#include "flecs.h"

#include "Map.h"

#include "modules/Rendering.h"
#include "modules/Tilemap/Tilemap.h"

namespace MapManager {

struct CachedMapEntry {
  Tilemap::LoadedMap loadedMap;
  std::list<std::string>::iterator lruIt;
};

struct MapCacheState {
  std::unordered_map<std::string, CachedMapEntry> cache;
  std::list<std::string> usageOrder;
  std::size_t maxSize = 3;
  std::size_t hitCount = 0;
  std::size_t missCount = 0;
};

Tilemap::LoadedMap *GetOrLoadMap(MapCacheState &cacheState, const std::string &path);

class TileRenderable final : public Rendering::Renderable, Tilemap::ChunkTile {
public:
  TileRenderable(std::shared_ptr<const Tilemap::TilemapTextureBank> bank, const Tilemap::ChunkTile &tile);
  void Draw(const Rendering::Position &position) const override;

private:
  std::shared_ptr<const Tilemap::TilemapTextureBank> textureBank;
};

void RegisterMapLoader(flecs::world &world);
void RegisterMapRendering(flecs::world &world);

} // namespace MapManager

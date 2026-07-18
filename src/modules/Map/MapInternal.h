#pragma once

#include <cstddef>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "flecs.h"

#include "Map.h"

#include "modules/Rendering.h"
#include "modules/Tilemap/Tilemap.h"

namespace MapManager::Internal {

struct MapPath {
  std::string value;
};

struct MapState {
  flecs::entity mapRoot = {};
  std::string currentPath;
};

struct ChunkKey {
  int x;
  int y;

  bool operator==(const ChunkKey &other) const {
    return x == other.x && y == other.y;
  }
};

struct ChunkKeyHash {
  std::size_t operator()(const ChunkKey &key) const {
    return std::hash<int>()(key.x) ^ (std::hash<int>()(key.y) << 1);
  }
};

struct ActiveMapData {
  std::shared_ptr<Tilemap::TilemapTextureBank> textureBank;
  std::unordered_map<ChunkKey, std::vector<Tilemap::ChunkTile>, ChunkKeyHash> staticTiles;
  std::unordered_map<ChunkKey, std::vector<Rendering::RenderComponent>, ChunkKeyHash> sortableTiles;
  int tileWidth = 0;
  int tileHeight = 0;
  int chunkPixelWidth = 0;
  int chunkPixelHeight = 0;
};

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

} // namespace MapManager::Internal

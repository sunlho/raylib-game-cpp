#pragma once

#include <string>

#include "flecs.h"

#include "modules/Rendering.h"
#include "modules/Tilemap/Tilemap.h"

namespace MapManage {

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

void SetMapPath(flecs::world &world, const std::string &path);

struct module {
  module(flecs::world &world);
};

} // namespace MapManage

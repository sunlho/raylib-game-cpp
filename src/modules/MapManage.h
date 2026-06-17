#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "flecs.h"
#include "Tilemap/Tilemap.h"

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
  int layer;

  bool operator==(const ChunkKey &other) const {
    return x == other.x && y == other.y && layer == other.layer;
  }
};

struct ChunkKeyHash {
  std::size_t operator()(const ChunkKey &key) const {
    return std::hash<int>()(key.x) ^ (std::hash<int>()(key.y) << 1) ^ (std::hash<int>()(key.layer) << 2);
  }
};

struct SortableChunksState {
  std::unordered_map<ChunkKey, std::vector<Tilemap::ChunkTile>, ChunkKeyHash> sortableTiles;
};

void SetMapPath(flecs::world &world, const std::string &path);

struct module {
  module(flecs::world &world);
};

} // namespace MapManage

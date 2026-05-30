#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "flecs.h"
#include "raylib.h"

#include "modules/Rendering.h"

namespace Tilemap {
static constexpr int CHUNK_SIZE = 16;

struct TilemapPath {
  std::string value;
};

struct MapBounds {
  Vector2 dimension = {0.0f, 0.0f};
};

struct ChunkTile {
  std::uint32_t tileGid = 0;
  int textureIndex = -1;
  Rectangle srcRect = {0};
  Rectangle destRect = {0};
};

struct ChunkAnimFrame {
  std::uint32_t tileId = 0;
  float durationSeconds = 0.0f;
};

struct ChunkAnimTile {
  std::size_t tileIndex = 0;
  std::uint32_t currentFrame = 0;
  std::uint32_t firstGid = 0;
  float startTime = 0.0f;
  std::vector<ChunkAnimFrame> frames;
};

struct Chunk {
  int chunkX = 0;
  int chunkY = 0;
  Rectangle destRect = {0};
  int layerIndex = 0;

  std::vector<ChunkTile> tiles;
  std::vector<ChunkAnimTile> animTiles;

  RenderTexture2D texture = {0};
  bool isDirty = true;
  bool initialized = false;
};

struct ChunkDrawable {
  Rendering::Position position = {{0.0f, 0.0f}};
  Rendering::RenderComponent render = {};
  float sortY = 0.0f;
  float layerIndex = 0.0f;
};

struct ChunkObject {
  flecs::entity_t entity = 0;
  Rectangle destRect = {0};
};

struct ChunkIndex {
  std::unordered_map<std::uint64_t, std::vector<flecs::entity_t>> chunkEntityMap;
  std::vector<ChunkObject> objects;
  std::vector<flecs::entity_t> allChunkEntities;
};

struct TilemapState {
  flecs::entity mapRoot = {};
};

void Import(flecs::world &world);
void SetTilemapPath(flecs::world &world, const std::string &path);

} // namespace Tilemap

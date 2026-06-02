#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "raylib.h"

#include "modules/Rendering.h"

namespace TilemapInternal {
struct TilemapTextureBank;
}

namespace Tilemap {
static constexpr int CHUNK_SIZE = 16;

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

struct LoadedMap {
  MapBounds bounds = {};
  std::shared_ptr<TilemapInternal::TilemapTextureBank> textureBank;
  std::vector<Chunk> chunks;
};

bool LoadFromPath(const std::string &path, LoadedMap &loadedMap);

} // namespace Tilemap

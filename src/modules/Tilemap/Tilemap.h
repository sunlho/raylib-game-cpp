#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "box2d/box2d.h"
#include "raylib.h"
#include "tmxlite/ObjectGroup.hpp"

#include "modules/Rendering.h"

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
  bool needsYSort = false;
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

enum class CollisionShape {
  Rectangle,
  Ellipse,
  Point,
  Polygon,
  Polyline,
  Text
};

struct CollisionData {
  CollisionShape shape = CollisionShape::Rectangle;
  Rectangle AABB = {0};
  Rectangle worldRect = {0};
  std::vector<Vector2> points;
  std::vector<Vector2> worldPoints;
  Vector2 position = {0.0f, 0.0f};
  float rotation = 0.0f;
  int layerIndex = 0;
};

struct Chunk {
  int chunkX = 0;
  int chunkY = 0;
  Rectangle destRect = {0};
  int layerIndex = 0;
  bool isCollision = false;

  std::vector<ChunkTile> tiles;
  std::vector<ChunkAnimTile> animTiles;
  std::vector<CollisionData> collisions;

  bool isDirty = true;
  bool initialized = false;
};

struct TilemapTileObject {
  std::vector<Tilemap::CollisionData> collisions;
  std::vector<tmx::Property> properties;
};

struct TilemapTileset {
  Texture2D texture = {0};
  std::uint32_t firstGid = 0;
  std::uint32_t lastGid = 0;
  int tileWidth = 0;
  int tileHeight = 0;
  int columnCount = 0;
  int spacing = 0;
  int margin = 0;

  std::unordered_map<std::uint32_t, TilemapTileObject> tiles;

  const TilemapTileObject *getTile(uint32_t gid) const {
    const auto it = tiles.find(gid);

    if (it != tiles.end()) {
      return &it->second;
    }

    return nullptr;
  }
};

struct TilemapTextureBank {
  std::vector<TilemapTileset> tilesets;
  ~TilemapTextureBank();
};

struct LoadedMap {
  MapBounds bounds = {};
  std::shared_ptr<TilemapTextureBank> textureBank;
  std::vector<Chunk> chunks;
};

bool LoadFromPath(const std::string &path, LoadedMap &loadedMap);

int FindTilesetIndexByGid(const TilemapTextureBank &textureBank, std::uint32_t gid);
const TilemapTileset *FindTilesetByGid(const TilemapTextureBank &textureBank, std::uint32_t gid);

void CreateCollisionEntity(flecs::world &world, b2WorldId physicsWorld, const std::vector<Tilemap::CollisionData> &collisions, const Rectangle &tileRect, int layerIndex, flecs::entity layerGroup);

} // namespace Tilemap

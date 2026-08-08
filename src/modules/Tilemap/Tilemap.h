#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "raylib.h"
#include "tmxlite/ObjectGroup.hpp"

#include "modules/Rendering.h"
#include "modules/Stairs/Stairs.h"

namespace Tilemap {

static constexpr int CHUNK_SIZE = 16;

struct ChunkTile {
  std::uint32_t tileGid = 0;
  Rectangle srcRect = {0};
  Rectangle destRect = {0};
  float floor = 0.0f;
  bool needsYSort = false;
};

struct TileAnimationFrame {
  std::uint32_t tileGid = 0;
  float durationSeconds = 0.0f;
};

struct TileAnimation {
  std::vector<TileAnimationFrame> frames;
  std::size_t currentFrame = 0;
  float elapsedSeconds = 0.0f;
  float durationSeconds = 0.0f;
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
  std::vector<CollisionData> collisions;

  bool isDirty = true;
  bool initialized = false;
};

struct TilemapTileObject {
  std::string texturePath;
  Rectangle srcRect = {0};
  int tileWidth = 0;
  int tileHeight = 0;
  TileAnimation animation;
  std::vector<Tilemap::CollisionData> collisions;
  std::vector<tmx::Property> properties;
};

struct TilemapTextureBank {
  std::unordered_map<std::string, Texture2D> textureCache;
  std::unordered_map<std::uint32_t, TilemapTileObject> tiles;

  Texture2D getOrLoadTexture(const std::string &path);
  const Texture2D *getTexture(const std::string &path) const {
    auto it = textureCache.find(path);
    return it != textureCache.end() ? &it->second : nullptr;
  }
  const TilemapTileObject *getTile(std::uint32_t gid) const {
    auto it = tiles.find(gid);
    return it != tiles.end() ? &it->second : nullptr;
  }
  const TilemapTileObject *getTileForRendering(std::uint32_t gid) const;
  void updateAnimations(float deltaSeconds);
  void resetAnimations();
  ~TilemapTextureBank();
};

enum class SpawnDirection {
  Down,
  Up,
  Left,
  Right,
};

struct SpawnPoint {
  std::string name;
  Vector2 position = {0.0f, 0.0f};
  bool isDefault = false;
  std::optional<float> floor;
  std::optional<SpawnDirection> direction;
};

struct LoadedMap {
  Vector2 dimensions = {0.0f, 0.0f};
  std::shared_ptr<TilemapTextureBank> textureBank;
  std::vector<Chunk> chunks;
  std::vector<Stairs::StairData> stairs;
  std::vector<SpawnPoint> spawnPoints;
  std::vector<std::string> validationErrors;
  int tileWidth = 0;
  int tileHeight = 0;
  int chunkPixelWidth = 0;
  int chunkPixelHeight = 0;
};

bool LoadFromPath(const std::string &path, LoadedMap &loadedMap);

void CreateCollisionEntity(flecs::world &world, const std::vector<Tilemap::CollisionData> &collisions, const Rectangle &tileRect, int layerIndex, flecs::entity layerGroup);

} // namespace Tilemap

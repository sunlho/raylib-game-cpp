#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "raylib.h"
#include "tmxlite/Map.hpp"
#include "tmxlite/ObjectGroup.hpp"
#include "tmxlite/TileLayer.hpp"

#include "Tilemap.h"

namespace TilemapInternal {

enum class TileCollisionShape {
  Rectangle,
  Ellipse,
  Point,
  Polygon,
  Polyline,
  Text
};

struct TileCollision {
  TileCollisionShape shape;
  std::vector<Vector2> points;
  Rectangle AABB;
  Vector2 position;
  float rotation = 0.0f;
};

struct TilemapTileObject {
  std::vector<TileCollision> collisions;
};

struct TilemapTilesetTexture {
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
  std::vector<TilemapTilesetTexture> tilesets;
  ~TilemapTextureBank();
};

constexpr std::uint32_t TMX_FLIP_BITS_REMOVAL = 0x1FFFFFFFU;

int CeilDiv(int value, int divisor);
int FindTilesetIndexByGid(const TilemapTextureBank &textureBank, std::uint32_t gid);
Rectangle ComputeSourceRect(const TilemapTilesetTexture &tileset, std::uint32_t gid);

std::shared_ptr<TilemapTextureBank> LoadTilesetTextures(const tmx::Map &tilemap, const std::string &mapRelativePath);

void BuildLayerChunks(const tmx::Map &tilemap, const tmx::TileLayer &layer, int layerIndex, Tilemap::LoadedMap &loadedMap);

void BuildObjectChunks(const tmx::Map &tilemap, const tmx::ObjectGroup &objectGroup, int layerIndex, Tilemap::LoadedMap &loadedMap);

} // namespace TilemapInternal

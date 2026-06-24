#include "TilemapInternal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Tilemap {
namespace TilemapInternal {

int CeilDiv(int value, int divisor) {
  return (value + divisor - 1) / divisor;
}

void BuildLayerChunks(const tmx::Map &tilemap, const tmx::TileLayer &layer, int layerIndex, Tilemap::LoadedMap &loadedMap) {
  if (!loadedMap.textureBank) {
    return;
  }

  const auto &textureBank = loadedMap.textureBank;
  const auto mapTileCount = tilemap.getTileCount();
  const auto mapTileSize = tilemap.getTileSize();
  const auto &allTiles = layer.getTiles();

  const int width = static_cast<int>(mapTileCount.x);
  const int height = static_cast<int>(mapTileCount.y);
  const int tileWidth = static_cast<int>(mapTileSize.x);
  const int tileHeight = static_cast<int>(mapTileSize.y);

  const int chunkCountX = CeilDiv(width, Tilemap::CHUNK_SIZE);
  const int chunkCountY = CeilDiv(height, Tilemap::CHUNK_SIZE);

  for (int by = 0; by < chunkCountY; ++by) {
    for (int bx = 0; bx < chunkCountX; ++bx) {
      Tilemap::Chunk chunk;
      chunk.chunkX = bx;
      chunk.chunkY = by;
      chunk.layerIndex = layerIndex;

      const int tilesInChunkX = std::min(Tilemap::CHUNK_SIZE, width - bx * Tilemap::CHUNK_SIZE);
      const int tilesInChunkY = std::min(Tilemap::CHUNK_SIZE, height - by * Tilemap::CHUNK_SIZE);

      for (int y = 0; y < tilesInChunkY; ++y) {
        for (int x = 0; x < tilesInChunkX; ++x) {
          const int tileX = bx * Tilemap::CHUNK_SIZE + x;
          const int tileY = by * Tilemap::CHUNK_SIZE + y;
          const auto tileIndex = static_cast<std::size_t>(tileY * width + tileX);

          if (tileIndex >= allTiles.size()) {
            continue;
          }

          const std::uint32_t gid = allTiles[tileIndex].ID & TMX_FLIP_BITS_REMOVAL;
          if (gid == 0) {
            continue;
          }

          const int textureIndex = FindTilesetIndexByGid(*textureBank, gid);
          if (textureIndex < 0) {
            continue;
          }

          const auto &tileset = textureBank->tilesets[static_cast<std::size_t>(textureIndex)];

          const auto tile = tileset.getTile(gid);

          Tilemap::ChunkTile chunkTile;
          chunkTile.tileGid = gid;
          chunkTile.textureIndex = textureIndex;
          chunkTile.srcRect = ComputeSourceRect(tileset, gid);
          chunkTile.destRect = Rectangle{
              static_cast<float>(tileX * tileWidth),
              static_cast<float>(tileY * tileHeight),
              static_cast<float>(tileWidth),
              static_cast<float>(tileHeight)};

          const auto &properties = tile->properties;
          for (const auto &prop : properties) {
            if (prop.getName() == "needsYSort" && prop.getType() == tmx::Property::Type::Boolean) {
              chunkTile.needsYSort = prop.getBoolValue();
              break;
            }
          }

          chunk.tiles.push_back(chunkTile);
        }
      }

      if (chunk.tiles.empty()) {
        continue;
      }

      chunk.destRect = Rectangle{
          static_cast<float>(bx * Tilemap::CHUNK_SIZE * tileWidth),
          static_cast<float>(by * Tilemap::CHUNK_SIZE * tileHeight),
          static_cast<float>(Tilemap::CHUNK_SIZE * tileWidth),
          static_cast<float>(Tilemap::CHUNK_SIZE * tileHeight)};

      loadedMap.chunks.push_back(std::move(chunk));
    }
  }
}

void BuildObjectChunks(const tmx::Map &tilemap, const tmx::ObjectGroup &objectGroup, int layerIndex, Tilemap::LoadedMap &loadedMap) {
  if (!loadedMap.textureBank) {
    return;
  }

  const auto &textureBank = loadedMap.textureBank;

  for (const auto &object : objectGroup.getObjects()) {
    if (!object.visible()) {
      continue;
    }

    const std::uint32_t gid = object.getTileID() & TMX_FLIP_BITS_REMOVAL;
    if (gid == 0) {
      continue;
    }

    const int textureIndex = FindTilesetIndexByGid(*textureBank, gid);
    if (textureIndex < 0) {
      continue;
    }

    const auto &tileset = textureBank->tilesets[static_cast<std::size_t>(textureIndex)];
    const auto aabb = object.getAABB();
    const auto pos = object.getPosition();

    Tilemap::Chunk chunk;
    chunk.chunkX = static_cast<int>(pos.x + aabb.width / 2.0f);
    chunk.chunkY = static_cast<int>(pos.y + aabb.height / 2.0f);
    chunk.layerIndex = layerIndex;
    chunk.destRect = Rectangle{pos.x, pos.y, aabb.width, aabb.height};

    Tilemap::ChunkTile tile;
    tile.tileGid = gid;
    tile.textureIndex = textureIndex;
    tile.srcRect = ComputeSourceRect(tileset, gid);
    tile.destRect = chunk.destRect;

    const auto &properties = object.getProperties();
    for (const auto &prop : properties) {
      if (prop.getName() == "needsYSort" && prop.getType() == tmx::Property::Type::Boolean) {
        tile.needsYSort = prop.getBoolValue();
        break;
      }
    }

    chunk.tiles.push_back(tile);
    loadedMap.chunks.push_back(std::move(chunk));
  }
}

void BuildObjectCollisions(const tmx::Map &tilemap, const tmx::ObjectGroup &objectGroup, int layerIndex, Tilemap::LoadedMap &loadedMap) {
  for (const auto &object : objectGroup.getObjects()) {
    if (!object.visible()) {
      continue;
    }

    const auto aabb = object.getAABB();
    const auto pos = object.getPosition();

    Tilemap::Chunk chunk;
    chunk.chunkX = static_cast<int>(pos.x + aabb.width / 2.0f);
    chunk.chunkY = static_cast<int>(pos.y + aabb.height / 2.0f);
    chunk.layerIndex = layerIndex;
    chunk.isCollision = true;

    Tilemap::CollisionData collision;
    collision.shape = static_cast<Tilemap::CollisionShape>(object.getShape());
    collision.AABB = Rectangle{aabb.left, aabb.top, aabb.width, aabb.height};
    collision.position = Vector2{pos.x, pos.y};
    collision.rotation = object.getRotation();

    const auto &points = object.getPoints();
    collision.points.reserve(points.size());
    const auto classType = object.getClass();
    if (classType == "Internal") {
      for (auto it = points.rbegin(); it != points.rend(); ++it) {
        collision.points.emplace_back(it->x, it->y);
      }
    } else {
      for (const auto &point : points) {
        collision.points.emplace_back(point.x, point.y);
      }
    }

    chunk.collisions.push_back(collision);
    loadedMap.chunks.push_back(std::move(chunk));
  }
}

} // namespace TilemapInternal
} // namespace Tilemap

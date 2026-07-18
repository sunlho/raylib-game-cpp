#include "TilemapInternal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "modules/Stairs/Stairs.h"

namespace Tilemap::Internal {

int CeilDiv(int value, int divisor) {
  return (value + divisor - 1) / divisor;
}

float GetFloorProperty(const std::vector<tmx::Property> &properties) {
  for (const auto &prop : properties) {
    if (prop.getName() != "floor") {
      continue;
    }

    if (prop.getType() == tmx::Property::Type::Float) {
      return prop.getFloatValue();
    }

    if (prop.getType() == tmx::Property::Type::Int) {
      return static_cast<float>(prop.getIntValue());
    }
  }

  return 0.0f;
}

bool TryGetNumberProperty(const tmx::Property &prop, const char *name, float &value) {
  if (prop.getName() != name) {
    return false;
  }

  if (prop.getType() == tmx::Property::Type::Float) {
    value = prop.getFloatValue();
    return true;
  }

  if (prop.getType() == tmx::Property::Type::Int) {
    value = static_cast<float>(prop.getIntValue());
    return true;
  }

  return false;
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
  const float layerFloor = GetFloorProperty(layer.getProperties());

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

          const auto tile = textureBank->getTile(gid);
          if (!tile) {
            continue;
          }

          Tilemap::ChunkTile chunkTile;
          chunkTile.tileGid = gid;
          chunkTile.srcRect = tile->srcRect;
          chunkTile.floor = layerFloor;
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
          static_cast<float>(bx * loadedMap.chunkPixelWidth),
          static_cast<float>(by * loadedMap.chunkPixelHeight),
          static_cast<float>(loadedMap.chunkPixelWidth),
          static_cast<float>(loadedMap.chunkPixelHeight)};

      loadedMap.chunks.push_back(std::move(chunk));
    }
  }
}

void BuildObjectChunks(const tmx::Map &tilemap, const tmx::ObjectGroup &objectGroup, int layerIndex, Tilemap::LoadedMap &loadedMap) {
  if (!loadedMap.textureBank) {
    return;
  }

  const auto &textureBank = loadedMap.textureBank;
  const float layerFloor = GetFloorProperty(objectGroup.getProperties());

  for (const auto &object : objectGroup.getObjects()) {
    if (!object.visible()) {
      continue;
    }

    const std::uint32_t gid = object.getTileID() & TMX_FLIP_BITS_REMOVAL;
    if (gid == 0) {
      const auto className = object.getClass();
      if (className == "Stairs") {
        Stairs::StairData stairData;

        const auto aabb = object.getAABB();
        stairData.bounds = Rectangle{aabb.left, aabb.top, aabb.width, aabb.height};
        for (const auto &prop : object.getProperties()) {
          if (TryGetNumberProperty(prop, "directionX", stairData.directionX)) {
            continue;
          }

          if (TryGetNumberProperty(prop, "lowFloor", stairData.lowFloor)) {
            continue;
          }

          if (TryGetNumberProperty(prop, "highFloor", stairData.highFloor)) {
            continue;
          }

          if (TryGetNumberProperty(prop, "floorSwitchT", stairData.floorSwitchT)) {
            continue;
          }

          if (prop.getName() == "enabled" && prop.getType() == tmx::Property::Type::Boolean) {
            stairData.enabled = prop.getBoolValue();
          }
        }
        loadedMap.stairs.push_back(stairData);
      }
      continue;
    }

    const auto tile = textureBank->getTile(gid);
    if (!tile) {
      continue;
    }

    const auto mapTileSize = tilemap.getTileSize();
    const auto aabb = object.getAABB();
    const auto pos = object.getPosition();

    Tilemap::Chunk chunk;
    chunk.chunkX = static_cast<int>(std::floor(pos.x / loadedMap.chunkPixelWidth));
    chunk.chunkY = static_cast<int>(std::floor(pos.y / loadedMap.chunkPixelHeight));
    chunk.layerIndex = layerIndex;
    chunk.destRect = Rectangle{pos.x, pos.y - aabb.height, aabb.width, aabb.height};

    Tilemap::ChunkTile chunkTile;
    chunkTile.tileGid = gid;
    chunkTile.srcRect = tile->srcRect;
    chunkTile.destRect = chunk.destRect;
    chunkTile.floor = layerFloor;

    const auto &properties = tile->properties;
    for (const auto &prop : properties) {
      if (prop.getName() == "needsYSort" && prop.getType() == tmx::Property::Type::Boolean) {
        chunkTile.needsYSort = prop.getBoolValue();
        break;
      }
    }

    chunk.tiles.push_back(chunkTile);
    loadedMap.chunks.push_back(std::move(chunk));
  }
}

void BuildObjectCollisions(const tmx::Map &tilemap, const tmx::ObjectGroup &objectGroup, int layerIndex, Tilemap::LoadedMap &loadedMap) {
  for (const auto &object : objectGroup.getObjects()) {
    if (!object.visible()) {
      continue;
    }

    const auto mapTileSize = tilemap.getTileSize();
    const auto aabb = object.getAABB();
    const auto pos = object.getPosition();

    Tilemap::Chunk chunk;
    chunk.chunkX = static_cast<int>(std::floor(pos.x / loadedMap.chunkPixelWidth));
    chunk.chunkY = static_cast<int>(std::floor(pos.y / loadedMap.chunkPixelHeight));
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

} // namespace Tilemap::Internal

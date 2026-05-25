#include "TilemapInternal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace TilemapInternal {

class ChunkRenderable final : public Rendering::Renderable {
public:
  ChunkRenderable(std::vector<Tilemap::ChunkTile> tiles, std::shared_ptr<TilemapTextureBank> textureBank) : tiles_(std::move(tiles)), textureBank_(std::move(textureBank)) {
  }

  void Draw(const Rendering::Position &position) const override {
    (void)position;

    if (!textureBank_) {
      return;
    }

    for (const auto &tile : tiles_) {
      if (tile.textureIndex < 0 || tile.textureIndex >= static_cast<int>(textureBank_->tilesets.size())) {
        continue;
      }

      const auto &tileset = textureBank_->tilesets[static_cast<std::size_t>(tile.textureIndex)];
      if (tileset.texture.id == 0) {
        continue;
      }

      DrawTexturePro(
          tileset.texture,
          tile.srcRect,
          tile.destRect,
          Vector2{0.0f, 0.0f},
          0.0f,
          WHITE);
    }
  }

private:
  std::vector<Tilemap::ChunkTile> tiles_;
  std::shared_ptr<TilemapTextureBank> textureBank_;
};

int CeilDiv(int value, int divisor) {
  return (value + divisor - 1) / divisor;
}

std::uint64_t MakeChunkKey(int chunkX, int chunkY) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX)) << 32U) | static_cast<std::uint32_t>(chunkY);
}

flecs::entity CreateChunkEntity(flecs::world &world, Tilemap::Chunk chunk, float sortY, bool isObject, flecs::entity layerGroup, std::shared_ptr<TilemapTextureBank> textureBank, Tilemap::ChunkIndex &chunkIndex) {
  const int chunkX = chunk.chunkX;
  const int chunkY = chunk.chunkY;
  const Rectangle chunkRect = chunk.destRect;

  auto chunkEntity = world.entity();

  const float chunkWidth = chunkRect.width;
  const float chunkHeight = chunkRect.height;

  auto renderable = std::make_shared<ChunkRenderable>(chunk.tiles, textureBank);

  Rendering::RenderComponent renderComponent;
  renderComponent.object = renderable;
  renderComponent.visible = true;

  Tilemap::ChunkDrawable chunkDrawable;
  chunkDrawable.position = {{chunkRect.x + (chunkWidth * 0.5f), chunkRect.y + (chunkHeight * 0.5f)}};
  chunkDrawable.render = renderComponent;
  chunkDrawable.sortY = sortY;
  chunkDrawable.layerIndex = static_cast<float>(chunk.layerIndex);

  chunkEntity
      .set<Tilemap::Chunk>(std::move(chunk))
      .set<Tilemap::ChunkDrawable>(chunkDrawable)
      .set<Rendering::Position>(chunkDrawable.position)
      .set<Rendering::RenderComponent>(chunkDrawable.render);

  if (layerGroup.is_valid()) {
    chunkEntity.add(flecs::ChildOf, layerGroup);
  }

  chunkIndex.allChunkEntities.push_back(chunkEntity.id());

  if (isObject) {
    chunkIndex.objects.push_back(Tilemap::ChunkObject{chunkEntity.id(), chunkRect});
  } else {
    const auto key = MakeChunkKey(chunkX, chunkY);
    chunkIndex.chunkEntityMap[key].push_back(chunkEntity.id());
  }

  return chunkEntity;
}

void BuildLayerChunks(flecs::world &world, const tmx::Map &tilemap, const tmx::TileLayer &layer, int layerIndex, flecs::entity layerGroup, const std::shared_ptr<TilemapTextureBank> &textureBank, Tilemap::ChunkIndex &chunkIndex) {
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

          Tilemap::ChunkTile chunkTile;
          chunkTile.tileGid = gid;
          chunkTile.textureIndex = textureIndex;
          chunkTile.srcRect = ComputeSourceRect(tileset, gid);
          chunkTile.destRect = Rectangle{
              static_cast<float>(tileX * tileWidth),
              static_cast<float>(tileY * tileHeight),
              static_cast<float>(tileWidth),
              static_cast<float>(tileHeight)};

          chunk.tiles.push_back(chunkTile);

          const auto tile = tileset.origin->getTile(gid);
          const auto objects = tile->objectGroup.getObjects();
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

      const float sortY = chunk.destRect.y + chunk.destRect.height + static_cast<float>(layerIndex) * 100000.0f;
      CreateChunkEntity(world, std::move(chunk), sortY, false, layerGroup, textureBank, chunkIndex);
    }
  }
}

void BuildObjectChunks(flecs::world &world, const tmx::Map &tilemap, const tmx::ObjectGroup &objectGroup, int layerIndex, flecs::entity layerGroup, const std::shared_ptr<TilemapTextureBank> &textureBank, Tilemap::ChunkIndex &chunkIndex) {
  const auto mapTileSize = tilemap.getTileSize();
  const float mapChunkWidth = static_cast<float>(Tilemap::CHUNK_SIZE * static_cast<int>(mapTileSize.x));
  const float mapChunkHeight = static_cast<float>(Tilemap::CHUNK_SIZE * static_cast<int>(mapTileSize.y));

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

    const float tileWidth = aabb.width > 0.0f ? aabb.width : static_cast<float>(tileset.tileWidth);
    const float tileHeight = aabb.height > 0.0f ? aabb.height : static_cast<float>(tileset.tileHeight);

    const float posX = object.getPosition().x;
    const float posY = object.getPosition().y - tileHeight;

    Tilemap::Chunk chunk;
    chunk.chunkX = static_cast<int>(std::floor(posX / mapChunkWidth));
    chunk.chunkY = static_cast<int>(std::floor(posY / mapChunkHeight));
    chunk.layerIndex = layerIndex;
    chunk.destRect = Rectangle{posX, posY, tileWidth, tileHeight};

    Tilemap::ChunkTile tile;
    tile.tileGid = gid;
    tile.textureIndex = textureIndex;
    tile.srcRect = ComputeSourceRect(tileset, gid);
    tile.destRect = chunk.destRect;
    chunk.tiles.push_back(tile);

    const float sortY = posY + tileHeight + static_cast<float>(layerIndex) * 100000.0f;
    CreateChunkEntity(world, std::move(chunk), sortY, true, layerGroup, textureBank, chunkIndex);
  }
}

} // namespace TilemapInternal

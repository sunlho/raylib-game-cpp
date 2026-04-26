#include "flecs.h"
#include "raylib.h"
#include "tmxlite/Map.hpp"
#include "tmxlite/ObjectGroup.hpp"
#include "tmxlite/TileLayer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "Assets.h"
#include "Tilemap.h"

namespace {

struct TilemapTilesetTexture {
  Texture2D texture = {0};
  std::uint32_t firstGid = 0;
  std::uint32_t lastGid = 0;
  int tileWidth = 0;
  int tileHeight = 0;
  int columnCount = 0;
  int spacing = 0;
  int margin = 0;
};

struct TilemapTextureBank {
  std::vector<TilemapTilesetTexture> tilesets;

  ~TilemapTextureBank() {
    for (auto &tileset : tilesets) {
      if (tileset.texture.id != 0) {
        UnloadTexture(tileset.texture);
        tileset.texture = Texture2D{};
      }
    }
  }
};

class ChunkRenderable final : public Rendering::Renderable {
public:
  ChunkRenderable(std::vector<Tilemap::ChunkTile> tiles,
                  std::shared_ptr<TilemapTextureBank> textureBank)
      : tiles_(std::move(tiles)), textureBank_(std::move(textureBank)) {
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

constexpr std::uint32_t TMX_FLIP_BITS_REMOVAL = 0x1FFFFFFFU;

static int CeilDiv(int value, int divisor) {
  return (value + divisor - 1) / divisor;
}

static std::uint64_t MakeChunkKey(int chunkX, int chunkY) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX)) << 32U) | static_cast<std::uint32_t>(chunkY);
}

static int FindTilesetIndexByGid(const TilemapTextureBank &textureBank, std::uint32_t gid) {
  for (std::size_t i = 0; i < textureBank.tilesets.size(); ++i) {
    const auto &tileset = textureBank.tilesets[i];
    if (gid >= tileset.firstGid && gid <= tileset.lastGid) {
      return static_cast<int>(i);
    }
  }

  return -1;
}

static Rectangle ComputeSourceRect(const TilemapTilesetTexture &tileset, std::uint32_t gid) {
  const std::uint32_t localId = gid - tileset.firstGid;

  if (tileset.columnCount <= 0) {
    return Rectangle{0, 0, 0, 0};
  }

  const int column = static_cast<int>(localId % static_cast<std::uint32_t>(tileset.columnCount));
  const int row = static_cast<int>(localId / static_cast<std::uint32_t>(tileset.columnCount));

  const int stepX = tileset.tileWidth + tileset.spacing;
  const int stepY = tileset.tileHeight + tileset.spacing;

  return Rectangle{
      static_cast<float>(tileset.margin + column * stepX),
      static_cast<float>(tileset.margin + row * stepY),
      static_cast<float>(tileset.tileWidth),
      static_cast<float>(tileset.tileHeight)};
}

static std::shared_ptr<TilemapTextureBank> LoadTilesetTextures(const tmx::Map &tilemap,
                                                               const std::string &mapRelativePath) {
  auto textureBank = std::make_shared<TilemapTextureBank>();
  const auto &tilesets = tilemap.getTilesets();
  textureBank->tilesets.reserve(tilesets.size());

  for (std::size_t i = 0; i < tilesets.size(); ++i) {
    const auto &tileset = tilesets[i];
    const auto imagePath = tileset.getImagePath();

    if (imagePath.empty()) {
      TraceLog(LOG_WARNING, "Tileset[%d] has empty image path, skip", static_cast<int>(i));
      continue;
    }

    const auto mapDir = std::filesystem::path(mapRelativePath).parent_path();
    const auto textureRelative = (mapDir / imagePath).lexically_normal().generic_string();
    const auto texturePath = Assets::Path(textureRelative);

    if (!Assets::Exists(textureRelative)) {
      TraceLog(LOG_WARNING, "Tileset image not found: %s", texturePath.string().c_str());
      continue;
    }

    TilemapTilesetTexture loaded;
    loaded.texture = LoadTexture(texturePath.string().c_str());
    loaded.firstGid = tileset.getFirstGID();
    loaded.lastGid = tileset.getFirstGID() + tileset.getTileCount() - 1;
    loaded.tileWidth = static_cast<int>(tileset.getTileSize().x);
    loaded.tileHeight = static_cast<int>(tileset.getTileSize().y);
    loaded.columnCount = static_cast<int>(tileset.getColumnCount());
    loaded.spacing = static_cast<int>(tileset.getSpacing());
    loaded.margin = static_cast<int>(tileset.getMargin());

    textureBank->tilesets.push_back(loaded);
  }

  return textureBank;
}

static flecs::entity CreateChunkEntity(flecs::world &world,
                                       Tilemap::Chunk chunk,
                                       float sortY,
                                       bool isObject,
                                       std::shared_ptr<TilemapTextureBank> textureBank,
                                       Tilemap::ChunkIndex &chunkIndex) {
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

  chunkIndex.allChunkEntities.push_back(chunkEntity.id());

  if (isObject) {
    chunkIndex.objects.push_back(Tilemap::ChunkObject{chunkEntity.id(), chunkRect});
  } else {
    const auto key = MakeChunkKey(chunkX, chunkY);
    chunkIndex.chunkEntityMap[key].push_back(chunkEntity.id());
  }

  return chunkEntity;
}

static void BuildLayerChunks(flecs::world &world,
                             const tmx::Map &tilemap,
                             const tmx::TileLayer &layer,
                             int layerIndex,
                             const std::shared_ptr<TilemapTextureBank> &textureBank,
                             Tilemap::ChunkIndex &chunkIndex) {
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
      CreateChunkEntity(world, std::move(chunk), sortY, false, textureBank, chunkIndex);
    }
  }
}

static void BuildObjectChunks(flecs::world &world,
                              const tmx::Map &tilemap,
                              const tmx::ObjectGroup &objectGroup,
                              int layerIndex,
                              const std::shared_ptr<TilemapTextureBank> &textureBank,
                              Tilemap::ChunkIndex &chunkIndex) {
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
    CreateChunkEntity(world, std::move(chunk), sortY, true, textureBank, chunkIndex);
  }
}

} // namespace

Tilemap::Tilemap(flecs::world &world) {
  world.component<ChunkTile>();
  world.component<ChunkAnimFrame>();
  world.component<ChunkAnimTile>();
  world.component<Chunk>();
  world.component<ChunkDrawable>();
  world.component<ChunkObject>();
  world.component<ChunkIndex>()
      .add(flecs::Singleton);
  world.set<ChunkIndex>({});

  world.system<const TilemapPath>("Load Tilemap")
      .kind(flecs::OnStart)
      .each([&world](const TilemapPath &map) {
        const auto mapPath = Assets::Path(map.value);

        if (!Assets::Exists(map.value)) {
          TraceLog(LOG_WARNING, "Tilemap file not found: %s", mapPath.string().c_str());
          return;
        }

        tmx::Map tilemap;
        if (!tilemap.load(mapPath.string())) {
          TraceLog(LOG_WARNING, "Failed to load tilemap: %s", mapPath.string().c_str());
          return;
        }

        auto &chunkIndex = world.get_mut<ChunkIndex>();

        auto textureBank = LoadTilesetTextures(tilemap, map.value);

        int layerIndex = 0;
        for (const auto &layer : tilemap.getLayers()) {
          if (!layer || layer->getType() != tmx::Layer::Type::Tile) {
            if (layer && layer->getType() == tmx::Layer::Type::Object) {
              const auto &objectGroup = layer->getLayerAs<tmx::ObjectGroup>();
              BuildObjectChunks(world, tilemap, objectGroup, layerIndex, textureBank, chunkIndex);
            }

            layerIndex++;
            continue;
          }

          const auto &tileLayer = layer->getLayerAs<tmx::TileLayer>();
          BuildLayerChunks(world, tilemap, tileLayer, layerIndex, textureBank, chunkIndex);
          layerIndex++;
        }

        world.modified<ChunkIndex>();
        TraceLog(LOG_INFO, "Tilemap loaded and chunked from: %s", mapPath.string().c_str());
      });
}

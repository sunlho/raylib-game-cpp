#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include "raylib.h"

#include "Map.h"
#include "MapInternal.h"

#include "modules/Camera.h"
#include "modules/Rendering.h"
#include "modules/Tilemap/Tilemap.h"

namespace MapManager {

TileRenderable::TileRenderable(std::shared_ptr<const Tilemap::TilemapTextureBank> bank, const Tilemap::ChunkTile &tile)
    : textureBank(std::move(bank)) {
  tileGid = tile.tileGid;
  srcRect = tile.srcRect;
  destRect = tile.destRect;
}

void TileRenderable::Draw(const Rendering::Position &position) const {
  const auto tileObject = textureBank->getTile(tileGid);
  if (!tileObject || tileObject->texturePath.empty()) {
    return;
  }

  const Texture2D *texture = textureBank->getTexture(tileObject->texturePath);
  if (!texture) {
    return;
  }

  DrawTexturePro(
      *texture,
      srcRect,
      destRect,
      Vector2{0.0f, 0.0f},
      0.0f,
      WHITE);
}

namespace {

struct RenderableSortData {
  const Rendering::Position *position;
  const Rendering::RenderComponent *renderComponent;
};

bool HasNoStaticChunkData(const ActiveMapData &activeData) {
  return !activeData.textureBank || activeData.staticTiles.empty() || activeData.chunkPixelWidth <= 0 || activeData.chunkPixelHeight <= 0;
}

bool HasNoSortableChunkData(const ActiveMapData &activeData) {
  return !activeData.textureBank || activeData.sortableTiles.empty() || activeData.chunkPixelWidth <= 0 || activeData.chunkPixelHeight <= 0;
}

// Computes the chunk the camera is centred on.
ChunkKey CameraCenterChunk(const ActiveMapData &activeData, const GameCamera::MainCamera &mainCamera) {
  return ChunkKey{
      static_cast<int>(std::floor(mainCamera.value.target.x / activeData.chunkPixelWidth)),
      static_cast<int>(std::floor(mainCamera.value.target.y / activeData.chunkPixelHeight))};
}

} // namespace

void RegisterMapRendering(flecs::world &world) {
  world.system("Draw Static Chunks")
      .kind<Rendering::Phases::Background>()
      .run([](flecs::iter &it) {
        auto world = it.world();
        const auto &activeData = world.get<ActiveMapData>();

        if (HasNoStaticChunkData(activeData)) {
          return;
        }

        const auto &mainCamera = world.get<GameCamera::MainCamera>();
        const ChunkKey center = CameraCenterChunk(activeData, mainCamera);

        for (int dx = -1; dx <= 1; ++dx) {
          for (int dy = -1; dy <= 1; ++dy) {
            ChunkKey key{center.x + dx, center.y + dy};
            auto keyIt = activeData.staticTiles.find(key);
            if (keyIt == activeData.staticTiles.end()) {
              continue;
            }

            for (const auto &tile : keyIt->second) {
              const auto tileObject = activeData.textureBank->getTile(tile.tileGid);
              if (!tileObject || tileObject->texturePath.empty()) {
                continue;
              }

              Texture2D textureToUse = activeData.textureBank->getOrLoadTexture(tileObject->texturePath);
              if (textureToUse.id == 0) {
                continue;
              }

              DrawTexturePro(
                  textureToUse,
                  tile.srcRect,
                  tile.destRect,
                  Vector2{0.0f, 0.0f},
                  0.0f,
                  WHITE);
            }
          }
        }
      });

  world.system<const Rendering::Position, const Rendering::RenderComponent>("Draw Sort Chunks")
      .with<const Rendering::SortableTag>()
      .kind<Rendering::Phases::SortedWorld>()
      .run([](flecs::iter &it) {
        auto world = it.world();
        const auto &activeData = world.get<ActiveMapData>();

        std::vector<RenderableSortData> sortData;

        std::size_t entityCount = 0;
        while (it.next()) {
          entityCount += it.count();
          auto position = it.field<const Rendering::Position>(0);
          auto renderComponent = it.field<const Rendering::RenderComponent>(1);

          for (auto i : it) {
            sortData.push_back(RenderableSortData{&position[i], &renderComponent[i]});
          }
        }

        std::size_t tileCount = 0;
        ChunkKey center{};
        const bool hasSortableChunks = !HasNoSortableChunkData(activeData);
        if (hasSortableChunks) {
          const auto &mainCamera = world.get<GameCamera::MainCamera>();
          center = CameraCenterChunk(activeData, mainCamera);

          for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
              ChunkKey key{center.x + dx, center.y + dy};
              auto keyIt = activeData.sortableTiles.find(key);
              if (keyIt != activeData.sortableTiles.end()) {
                tileCount += keyIt->second.size();
              }
            }
          }
        }
        sortData.reserve(entityCount + tileCount);

        std::vector<Rendering::Position> tilePositions;
        tilePositions.reserve(tileCount);

        if (hasSortableChunks) {
          for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
              int chunkX = center.x + dx;
              int chunkY = center.y + dy;

              ChunkKey key{chunkX, chunkY};
              auto keyIt = activeData.sortableTiles.find(key);
              if (keyIt == activeData.sortableTiles.end()) {
                continue;
              }

              for (const auto &renderComponent : keyIt->second) {
                if (!renderComponent.object || !renderComponent.visible) {
                  continue;
                }

                Rendering::Position position;
                position.value.x = static_cast<float>(chunkX * activeData.chunkPixelWidth);
                position.value.y = static_cast<float>(chunkY * activeData.chunkPixelHeight);
                tilePositions.push_back(position);

                sortData.push_back(RenderableSortData{&tilePositions.back(), &renderComponent});
              }
            }
          }
        }

        std::sort(sortData.begin(), sortData.end(), [](const RenderableSortData &a, const RenderableSortData &b) {
          if (a.renderComponent->floor != b.renderComponent->floor) {
            return a.renderComponent->floor < b.renderComponent->floor;
          }

          return a.renderComponent->sortY < b.renderComponent->sortY;
        });

        for (const auto &data : sortData) {
          if (!data.renderComponent->object || !data.renderComponent->visible) {
            continue;
          }

          data.renderComponent->object->Draw(*data.position);
        }
      });
}

} // namespace MapManager

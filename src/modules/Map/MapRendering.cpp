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

namespace MapManager::Internal {

TileRenderable::TileRenderable(std::shared_ptr<const Tilemap::TilemapTextureBank> bank, const Tilemap::ChunkTile &tile)
    : textureBank(std::move(bank)) {
  tileGid = tile.tileGid;
  srcRect = tile.srcRect;
  destRect = tile.destRect;
}

// destRect is stored in world space, so there is nothing to offset by; the
// parameter only exists because Renderable::Draw is shared with entity-backed
// renderables such as CharacterRenderable.
void TileRenderable::Draw(const Core::Position &) const {
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
  // Only meaningful for entity-backed renderables; chunk tiles draw from their
  // own world-space destRect and leave this zeroed.
  Core::Position position;
  const Rendering::RenderComponent *renderComponent;
};

struct MapRenderScratch {
  std::vector<RenderableSortData> sortData;
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
  auto sortScratch = std::make_shared<MapRenderScratch>();

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

              const Texture2D *texture = activeData.textureBank->getTexture(tileObject->texturePath);
              if (!texture) {
                continue;
              }

              DrawTexturePro(
                  *texture,
                  tile.srcRect,
                  tile.destRect,
                  Vector2{0.0f, 0.0f},
                  0.0f,
                  WHITE);
            }
          }
        }
      });

  world.system<const Core::Position, const Rendering::RenderComponent>("Draw Sort Chunks")
      .with<const Rendering::SortableTag>()
      .kind<Rendering::Phases::SortedWorld>()
      .run([sortScratch = std::move(sortScratch)](flecs::iter &it) {
        auto world = it.world();
        const auto &activeData = world.get<ActiveMapData>();
        auto &sortData = sortScratch->sortData;
        sortData.clear();

        while (it.next()) {
          auto position = it.field<const Core::Position>(0);
          auto renderComponent = it.field<const Rendering::RenderComponent>(1);

          for (auto i : it) {
            // For dynamic entities, use the camera-relative quantised render
            // position (set by Rendering::PrepareRenderFrame) so they share the
            // same pixel grid as every other dynamic object.
            const Core::RenderPosition *rp = it.entity(i).try_get<Core::RenderPosition>();
            const Core::Position drawPos = rp
                                               ? Core::Position{rp->quantized}
                                               : position[i];
            sortData.push_back(RenderableSortData{drawPos, &renderComponent[i]});
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
        sortData.reserve(sortData.size() + tileCount);

        if (hasSortableChunks) {
          for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
              ChunkKey key{center.x + dx, center.y + dy};
              auto keyIt = activeData.sortableTiles.find(key);
              if (keyIt == activeData.sortableTiles.end()) {
                continue;
              }

              for (const auto &renderComponent : keyIt->second) {
                if (!renderComponent.object || !renderComponent.visible) {
                  continue;
                }

                // Zero position: these are TileRenderables, which draw from
                // their own world-space destRect and ignore the argument.
                sortData.push_back(RenderableSortData{Core::Position{}, &renderComponent});
              }
            }
          }
        }

        // stable_sort, not sort: entries that tie on both floor and sortY must
        // keep their insertion order, otherwise overlapping tiles swap depth
        // between frames and visibly flicker.
        std::stable_sort(sortData.begin(), sortData.end(), [](const RenderableSortData &a, const RenderableSortData &b) {
          if (a.renderComponent->floor != b.renderComponent->floor) {
            return a.renderComponent->floor < b.renderComponent->floor;
          }

          return a.renderComponent->sortY < b.renderComponent->sortY;
        });

        for (const auto &data : sortData) {
          if (!data.renderComponent->object || !data.renderComponent->visible) {
            continue;
          }

          data.renderComponent->object->Draw(data.position);
        }
      });
}

} // namespace MapManager::Internal

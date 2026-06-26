#include <algorithm>
#include <cstddef>
#include <format>
#include <list>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "MapManage.h"
#include "Physics.h"
#include "modules/Camera.h"
#include "modules/Debug/DebugDraw.h"
#include "modules/Reflection.h"
#include "modules/Rendering.h"
#include "modules/Tilemap/Tilemap.h"

namespace MapManage {

class TileRenderable final : public Rendering::Renderable, Tilemap::ChunkTile {
public:
  TileRenderable(std::shared_ptr<const Tilemap::TilemapTextureBank> bank, const Tilemap::ChunkTile &tile)
      : textureBank(std::move(bank)) {
    tileGid = tile.tileGid;
    srcRect = tile.srcRect;
    destRect = tile.destRect;
  }

  void Draw(const Rendering::Position &position) const override {

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

private:
  std::shared_ptr<const Tilemap::TilemapTextureBank> textureBank;
};

namespace {

bool disableDebugDraw = true;

struct CachedMapEntry {
  Tilemap::LoadedMap loadedMap;
  std::list<std::string>::iterator lruIt;
};

struct MapCacheState {
  std::unordered_map<std::string, CachedMapEntry> cache;
  std::list<std::string> usageOrder;
  std::size_t maxSize = 3;
  std::size_t hitCount = 0;
  std::size_t missCount = 0;
};

struct RenderableSortData {
  const Rendering::Position *position;
  const Rendering::RenderComponent *renderComponent;
};

void DestroyCurrentMap(MapManage::MapState &mapState) {
  if (mapState.mapRoot.is_valid()) {
    mapState.mapRoot.destruct();
    mapState.mapRoot = {};
  }
}

void ClearMapData(flecs::world &world) {
  auto &mapBounds = world.get_mut<Tilemap::MapBounds>();
  mapBounds.dimension = Vector2{0.0f, 0.0f};
  world.modified<Tilemap::MapBounds>();

  auto &activeData = world.get_mut<ActiveMapData>();
  activeData.textureBank.reset();
  activeData.staticTiles.clear();
  activeData.sortableTiles.clear();
  activeData.tileWidth = 0;
  activeData.tileHeight = 0;
}

Tilemap::LoadedMap *GetOrLoadMap(MapCacheState &cacheState, const std::string &path) {
  auto it = cacheState.cache.find(path);
  if (it != cacheState.cache.end()) {
    cacheState.hitCount++;
    cacheState.usageOrder.erase(it->second.lruIt);
    cacheState.usageOrder.push_front(path);
    it->second.lruIt = cacheState.usageOrder.begin();
    return &it->second.loadedMap;
  }

  cacheState.missCount++;
  Tilemap::LoadedMap loadedMap;
  if (!Tilemap::LoadFromPath(path, loadedMap)) {
    return nullptr;
  }

  cacheState.usageOrder.push_front(path);
  auto insertIt = cacheState.cache.emplace(std::piecewise_construct, std::forward_as_tuple(path), std::forward_as_tuple()).first;
  insertIt->second.loadedMap = std::move(loadedMap);
  insertIt->second.lruIt = cacheState.usageOrder.begin();

  if (cacheState.cache.size() > cacheState.maxSize) {
    const std::string evictPath = cacheState.usageOrder.back();
    cacheState.cache.erase(evictPath);
    cacheState.usageOrder.pop_back();
  }

  return &insertIt->second.loadedMap;
}

void LoadMapFromPath(flecs::world world, const MapManage::MapPath &mapPath) {
  auto &mapState = world.get_mut<MapManage::MapState>();
  if (mapState.currentPath == mapPath.value && mapState.mapRoot.is_valid()) {
    return;
  }

  auto &cacheState = world.get_mut<MapCacheState>();
  auto *loadedMap = GetOrLoadMap(cacheState, mapPath.value);
  if (!loadedMap) {
    return;
  }

  DestroyCurrentMap(mapState);
  ClearMapData(world);

  mapState.mapRoot = world.entity("MapRoot");
  mapState.currentPath = mapPath.value;

  auto &mapBounds = world.get_mut<Tilemap::MapBounds>();
  mapBounds = loadedMap->bounds;
  world.modified<Tilemap::MapBounds>();

  auto &activeData = world.get_mut<ActiveMapData>();
  activeData.textureBank = loadedMap->textureBank;
  activeData.tileWidth = loadedMap->tileWidth;
  activeData.tileHeight = loadedMap->tileHeight;
  activeData.chunkPixelWidth = loadedMap->chunkPixelWidth;
  activeData.chunkPixelHeight = loadedMap->chunkPixelHeight;

  if (activeData.textureBank && !activeData.textureBank->tiles.empty()) {
    const auto &firstTile = activeData.textureBank->tiles.begin()->second;
  }

  std::unordered_map<int, flecs::entity> layerGroups;
  layerGroups.reserve(8);

  for (auto &chunk : loadedMap->chunks) {
    if (chunk.isCollision) {
      Tilemap::CreateCollisionEntity(world, Physics::Id, chunk.collisions, chunk.destRect, chunk.layerIndex, mapState.mapRoot);
      continue;
    }

    auto [groupIt, inserted] = layerGroups.try_emplace(chunk.layerIndex);
    if (inserted) {
      const std::string layerName = "MapLayer_" + std::to_string(chunk.layerIndex);
      auto layerEntity = world.entity(layerName.c_str());

      if (mapState.mapRoot.is_valid()) {
        layerEntity.add(flecs::ChildOf, mapState.mapRoot);
      }

      groupIt->second = layerEntity;
    }

    for (const auto &chunkTile : chunk.tiles) {
      const auto tileObject = activeData.textureBank->getTile(chunkTile.tileGid);
      if (tileObject && !tileObject->collisions.empty()) {
        Tilemap::CreateCollisionEntity(world, Physics::Id, tileObject->collisions, chunkTile.destRect, chunk.layerIndex, groupIt->second);
      }

      ChunkKey key{chunk.chunkX, chunk.chunkY};
      if (chunkTile.needsYSort) {
        TileRenderable *renderable = new TileRenderable(activeData.textureBank, chunkTile);
        Rendering::RenderComponent renderComponent;
        renderComponent.object = std::shared_ptr<Rendering::Renderable>(renderable);
        renderComponent.sortY = static_cast<int>(chunkTile.destRect.y + chunkTile.destRect.height);
        renderComponent.visible = true;

        activeData.sortableTiles[key].push_back(std::move(renderComponent));
      } else {
        activeData.staticTiles[key].push_back(chunkTile);
      }
    }
  }
}

} // namespace

module::module(flecs::world &world) {
  Reflection::Register<Tilemap::MapBounds>(world)
      .add(flecs::Singleton)
      .set<Tilemap::MapBounds>({});
  Reflection::Register<Tilemap::CollisionData>(world);
  Reflection::Register<MapPath>(world);
  Reflection::Register<ActiveMapData>(world)
      .add(flecs::Singleton)
      .set<ActiveMapData>({});
  Reflection::Register<MapCacheState>(world)
      .add(flecs::Singleton)
      .set<MapCacheState>({});
  Reflection::Register<MapState>(world)
      .add(flecs::Singleton)
      .set<MapState>({});

  world.system("Draw Static Chunks")
      .kind<Rendering::Phases::Background>()
      .run([](flecs::iter &it) {
        auto world = it.world();
        const auto &activeData = world.get<ActiveMapData>();

        if (!activeData.textureBank || activeData.staticTiles.empty() || activeData.chunkPixelWidth <= 0 || activeData.chunkPixelHeight <= 0) {
          return;
        }

        const auto &mainCamera = world.get<GameCamera::MainCamera>();

        int centerChunkX = static_cast<int>(std::floor(mainCamera.value.target.x / activeData.chunkPixelWidth));
        int centerChunkY = static_cast<int>(std::floor(mainCamera.value.target.y / activeData.chunkPixelHeight));

        for (int dx = -1; dx <= 1; ++dx) {
          for (int dy = -1; dy <= 1; ++dy) {
            int chunkX = centerChunkX + dx;
            int chunkY = centerChunkY + dy;

            ChunkKey key{chunkX, chunkY};
            auto keyIt = activeData.staticTiles.find(key);
            if (keyIt == activeData.staticTiles.end()) {
              continue;
            }

            const auto &tiles = keyIt->second;

            for (const auto &tile : tiles) {
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

  world.system<const Rendering::Position, const Rendering::RenderComponent>()
      .with<const Rendering::SortableTag>()
      .kind<Rendering::Phases::Draw>()
      .run([](flecs::iter &it) {
        std::vector<RenderableSortData> sortData;

        auto world = it.world();
        const auto &activeData = world.get<ActiveMapData>();

        if (!activeData.textureBank || activeData.staticTiles.empty() || activeData.chunkPixelWidth <= 0 || activeData.chunkPixelHeight <= 0) {
          return;
        }

        const auto &mainCamera = world.get<GameCamera::MainCamera>();

        int centerChunkX = static_cast<int>(std::floor(mainCamera.value.target.x / activeData.chunkPixelWidth));
        int centerChunkY = static_cast<int>(std::floor(mainCamera.value.target.y / activeData.chunkPixelHeight));

        while (it.next()) {
          auto position = it.field<const Rendering::Position>(0);
          auto renderComponent = it.field<const Rendering::RenderComponent>(1);

          for (auto i : it) {
            sortData.push_back(RenderableSortData{&position[i], &renderComponent[i]});
          }
        }

        int count = it.count();
        for (int dx = -1; dx <= 1; ++dx) {
          for (int dy = -1; dy <= 1; ++dy) {
            int chunkX = centerChunkX + dx;
            int chunkY = centerChunkY + dy;

            ChunkKey key{chunkX, chunkY};
            auto keyIt = activeData.sortableTiles.find(key);
            if (keyIt == activeData.sortableTiles.end()) {
              continue;
            }
            count += static_cast<int>(keyIt->second.size());
          }
        }

        sortData.reserve(count);

        for (int dx = -1; dx <= 1; ++dx) {
          for (int dy = -1; dy <= 1; ++dy) {
            int chunkX = centerChunkX + dx;
            int chunkY = centerChunkY + dy;

            ChunkKey key{chunkX, chunkY};
            auto keyIt = activeData.sortableTiles.find(key);
            if (keyIt == activeData.sortableTiles.end()) {
              continue;
            }

            const auto &renderComponents = keyIt->second;

            for (const auto &renderComponent : renderComponents) {
              if (!renderComponent.object || !renderComponent.visible) {
                continue;
              }

              Rendering::Position position;
              position.value.x = static_cast<float>(chunkX * activeData.chunkPixelWidth);
              position.value.y = static_cast<float>(chunkY * activeData.chunkPixelHeight);

              sortData.push_back(RenderableSortData{&position, const_cast<Rendering::RenderComponent *>(&renderComponent)});
            }
          }
        }

        std::sort(sortData.begin(), sortData.end(), [](const RenderableSortData &a, const RenderableSortData &b) {
          return a.renderComponent->sortY < b.renderComponent->sortY;
        });

        for (const auto &data : sortData) {
          if (!data.renderComponent->object || !data.renderComponent->visible) {
            continue;
          }

          data.renderComponent->object->Draw(*data.position);
        }
      });

  world.observer<const MapPath>("Load Map Observer")
      .event(flecs::OnSet)
      .each([](flecs::entity entity, const MapPath &mapPath) {
        auto world = entity.world();
        LoadMapFromPath(world, mapPath);
      });
}

void SetMapPath(flecs::world &world, const std::string &path) {
  auto mapEntity = world.entity("Map");
  mapEntity.set<MapPath>(MapPath{path});
}

} // namespace MapManage

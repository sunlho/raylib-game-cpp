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
namespace {

bool disableDebugDraw = true;

struct ChunkKey {
  int x;
  int y;

  bool operator==(const ChunkKey &other) const {
    return x == other.x && y == other.y;
  }
};

struct ChunkKeyHash {
  std::size_t operator()(const ChunkKey &key) const {
    return std::hash<int>()(key.x) ^ (std::hash<int>()(key.y) << 1);
  }
};

struct ActiveMapData {
  std::shared_ptr<Tilemap::TilemapTextureBank> textureBank;
  std::unordered_map<ChunkKey, std::vector<Tilemap::ChunkTile>, ChunkKeyHash> staticTiles;
  std::unordered_map<ChunkKey, std::vector<Tilemap::ChunkTile>, ChunkKeyHash> sortableTiles;
  int tileWidth = 0;
  int tileHeight = 0;
};

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

  if (activeData.textureBank && !activeData.textureBank->tilesets.empty()) {
    activeData.tileWidth = activeData.textureBank->tilesets[0].tileWidth;
    activeData.tileHeight = activeData.textureBank->tilesets[0].tileHeight;
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
      const auto tileset = Tilemap::FindTilesetByGid(*activeData.textureBank, chunkTile.tileGid);
      const auto tileObject = tileset->getTile(chunkTile.tileGid);
      if (tileObject && !tileObject->collisions.empty()) {
        Tilemap::CreateCollisionEntity(world, Physics::Id, tileObject->collisions, chunkTile.destRect, chunk.layerIndex, groupIt->second);
      }

      ChunkKey key{chunk.chunkX, chunk.chunkY};
      if (chunkTile.needsYSort) {
        activeData.sortableTiles[key].push_back(chunkTile);
      } else {
        activeData.staticTiles[key].push_back(chunkTile);
      }
    }
  }
}

} // namespace

module::module(flecs::world &world) {
  Reflection::Register<Tilemap::MapBounds>(world);
  Reflection::Register<Tilemap::CollisionData>(world);
  Reflection::Register<MapPath>(world);
  Reflection::Register<ActiveMapData>(world);
  Reflection::Register<MapCacheState>(world);
  Reflection::Register<MapState>(world);

  world.component<Tilemap::MapBounds>()
      .add(flecs::Singleton);
  world.set<Tilemap::MapBounds>({});

  world.component<MapState>()
      .add(flecs::Singleton);
  world.set<MapState>({});

  world.component<ActiveMapData>()
      .add(flecs::Singleton);
  world.set<ActiveMapData>({});

  world.component<MapCacheState>()
      .add(flecs::Singleton);
  world.set<MapCacheState>({});

  world.system("Draw Static Chunks")
      .kind<Rendering::Phases::Background>()
      .run([](flecs::iter &it) {
        auto world = it.world();
        const auto &activeData = world.get<ActiveMapData>();

        if (!activeData.textureBank || activeData.staticTiles.empty() || activeData.tileWidth <= 0 || activeData.tileHeight <= 0) {
          return;
        }

        const auto &mainCamera = world.singleton<GameCamera::MainCamera>();
        const auto &camState = mainCamera.get<GameCamera::CameraState>();
        const int chunkPixelW = Tilemap::CHUNK_SIZE * activeData.tileWidth;
        const int chunkPixelH = Tilemap::CHUNK_SIZE * activeData.tileHeight;

        int centerChunkX = static_cast<int>(std::floor(camState.value.target.x / chunkPixelW));
        int centerChunkY = static_cast<int>(std::floor(camState.value.target.y / chunkPixelH));

        const int tilesetCount = static_cast<int>(activeData.textureBank->tilesets.size());

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
              if (tile.textureIndex < 0 || tile.textureIndex >= tilesetCount) {
                continue;
              }

              const auto &tileset = activeData.textureBank->tilesets[static_cast<std::size_t>(tile.textureIndex)];
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

  if (IsWindowReady()) {
    LoadMapFromPath(world, MapPath{path});
  }
}

} // namespace MapManage

#include <format>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Map.h"
#include "MapInternal.h"

#include "modules/Physics.h"
#include "modules/Stairs/Stairs.h"
#include "modules/Tilemap/Tilemap.h"

namespace MapManager {
namespace {

void DestroyCurrentMap(MapState &mapState) {
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

void SpawnStairs(flecs::world &world, const Tilemap::LoadedMap &loadedMap, flecs::entity mapRoot) {
  for (std::size_t stairIndex = 0; stairIndex < loadedMap.stairs.size(); ++stairIndex) {
    const std::string stairName = std::format("MapStair_{}", stairIndex);
    auto stairEntity = world.entity(stairName.c_str());

    if (mapRoot.is_valid()) {
      stairEntity.add(flecs::ChildOf, mapRoot);
    }

    stairEntity.set<Stairs::StairData>(loadedMap.stairs[stairIndex]);
  }
}

flecs::entity EnsureLayerGroup(flecs::world &world, std::unordered_map<int, flecs::entity> &layerGroups, int layerIndex, flecs::entity mapRoot) {
  auto [groupIt, inserted] = layerGroups.try_emplace(layerIndex);
  if (inserted) {
    const std::string layerName = "MapLayer_" + std::to_string(layerIndex);
    auto layerEntity = world.entity(layerName.c_str());

    if (mapRoot.is_valid()) {
      layerEntity.add(flecs::ChildOf, mapRoot);
    }

    groupIt->second = layerEntity;
  }
  return groupIt->second;
}

void IngestTile(ActiveMapData &activeData, flecs::world &world, const Tilemap::Chunk &chunk, const Tilemap::ChunkTile &chunkTile, flecs::entity layerGroup) {
  const auto tileObject = activeData.textureBank->getTile(chunkTile.tileGid);
  if (tileObject && !tileObject->collisions.empty()) {
    Tilemap::CreateCollisionEntity(world, Physics::Id, tileObject->collisions, chunkTile.destRect, chunk.layerIndex, layerGroup);
  }

  ChunkKey key{chunk.chunkX, chunk.chunkY};
  if (chunkTile.needsYSort) {
    auto renderable = std::make_shared<TileRenderable>(activeData.textureBank, chunkTile);
    Rendering::RenderComponent renderComponent;
    renderComponent.object = std::move(renderable);
    renderComponent.floor = chunkTile.floor;
    renderComponent.sortY = static_cast<int>(chunkTile.destRect.y + chunkTile.destRect.height);
    renderComponent.visible = true;

    activeData.sortableTiles[key].push_back(std::move(renderComponent));
  } else {
    activeData.staticTiles[key].push_back(chunkTile);
  }
}

void BuildChunkEntities(flecs::world &world, const Tilemap::LoadedMap &loadedMap, ActiveMapData &activeData, flecs::entity mapRoot) {
  std::unordered_map<int, flecs::entity> layerGroups;
  layerGroups.reserve(8);

  for (const auto &chunk : loadedMap.chunks) {
    if (chunk.isCollision) {
      Tilemap::CreateCollisionEntity(world, Physics::Id, chunk.collisions, chunk.destRect, chunk.layerIndex, mapRoot);
      continue;
    }

    flecs::entity layerGroup = EnsureLayerGroup(world, layerGroups, chunk.layerIndex, mapRoot);

    for (const auto &chunkTile : chunk.tiles) {
      IngestTile(activeData, world, chunk, chunkTile, layerGroup);
    }
  }
}

void LoadMapFromPath(flecs::world world, const MapPath &mapPath) {
  auto &mapState = world.get_mut<MapState>();
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

  SpawnStairs(world, *loadedMap, mapState.mapRoot);

  auto &mapBounds = world.get_mut<Tilemap::MapBounds>();
  mapBounds = loadedMap->bounds;
  world.modified<Tilemap::MapBounds>();

  auto &activeData = world.get_mut<ActiveMapData>();
  activeData.textureBank = loadedMap->textureBank;
  activeData.tileWidth = loadedMap->tileWidth;
  activeData.tileHeight = loadedMap->tileHeight;
  activeData.chunkPixelWidth = loadedMap->chunkPixelWidth;
  activeData.chunkPixelHeight = loadedMap->chunkPixelHeight;

  BuildChunkEntities(world, *loadedMap, activeData, mapState.mapRoot);
}

} // namespace

void RegisterMapLoader(flecs::world &world) {
  world.observer<const MapPath>("Load Map Observer")
      .event(flecs::OnSet)
      .each([](flecs::entity entity, const MapPath &mapPath) {
        auto world = entity.world();
        LoadMapFromPath(world, mapPath);
      });
}

} // namespace MapManager

#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Map.h"
#include "MapInternal.h"

#include "modules/Stairs/Stairs.h"
#include "modules/Tilemap/Tilemap.h"

namespace MapManager::Internal {
namespace {

using Clock = std::chrono::steady_clock;

double ElapsedMilliseconds(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

struct TexturePreloadStats {
  std::size_t requested = 0;
  std::size_t ready = 0;
  std::size_t failed = 0;
};

void DestroyCurrentMap(MapState &mapState) {
  if (mapState.mapRoot.is_valid()) {
    mapState.mapRoot.destruct();
    mapState.mapRoot = {};
  }
}

void ClearMapData(flecs::world &world) {
  auto &mapBounds = world.get_mut<MapBounds>();
  mapBounds.dimension = Vector2{0.0f, 0.0f};
  world.modified<MapBounds>();

  auto &activeData = world.get_mut<ActiveMapData>();
  activeData.textureBank.reset();
  activeData.staticTiles.clear();
  activeData.sortableTiles.clear();
  activeData.tileWidth = 0;
  activeData.tileHeight = 0;
  activeData.chunkPixelWidth = 0;
  activeData.chunkPixelHeight = 0;
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
    Tilemap::CreateCollisionEntity(world, tileObject->collisions, chunkTile.destRect, chunk.layerIndex, layerGroup);
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
      Tilemap::CreateCollisionEntity(world, chunk.collisions, chunk.destRect, chunk.layerIndex, mapRoot);
      continue;
    }

    flecs::entity layerGroup = EnsureLayerGroup(world, layerGroups, chunk.layerIndex, mapRoot);

    for (const auto &chunkTile : chunk.tiles) {
      IngestTile(activeData, world, chunk, chunkTile, layerGroup);
    }
  }
}

TexturePreloadStats PreloadMapTextures(Tilemap::LoadedMap &loadedMap) {
  TexturePreloadStats stats;
  if (!loadedMap.textureBank) {
    return stats;
  }

  std::unordered_set<std::string> texturePaths;
  for (const auto &chunk : loadedMap.chunks) {
    for (const auto &tile : chunk.tiles) {
      const auto *tileObject = loadedMap.textureBank->getTile(tile.tileGid);
      if (tileObject && !tileObject->texturePath.empty()) {
        texturePaths.insert(tileObject->texturePath);
      }
    }
  }

  stats.requested = texturePaths.size();
  for (const auto &path : texturePaths) {
    const Texture2D texture = loadedMap.textureBank->getOrLoadTexture(path);
    if (texture.id == 0) {
      ++stats.failed;
    } else {
      ++stats.ready;
    }
  }

  return stats;
}

std::size_t CountTiles(const Tilemap::LoadedMap &loadedMap) {
  std::size_t count = 0;
  for (const auto &chunk : loadedMap.chunks) {
    count += chunk.tiles.size();
  }
  return count;
}

void LoadMapFromPath(flecs::world world, const MapPath &mapPath) {
  auto &mapState = world.get_mut<MapState>();
  if (mapState.currentPath == mapPath.value && mapState.mapRoot.is_valid()) {
    return;
  }

  auto &cacheState = world.get_mut<MapCacheState>();
  const std::size_t hitCountBefore = cacheState.hitCount;
  const auto sourceStart = Clock::now();
  auto *loadedMap = GetOrLoadMap(cacheState, mapPath.value);
  const auto sourceEnd = Clock::now();
  const double sourceMilliseconds = ElapsedMilliseconds(sourceStart, sourceEnd);
  if (!loadedMap) {
    TraceLog(LOG_WARNING, "Map switch failed for '%s' during source/cache acquisition after %.3f ms", mapPath.value.c_str(), sourceMilliseconds);
    return;
  }
  const bool cacheHit = cacheState.hitCount > hitCountBefore;

  const auto preloadStart = Clock::now();
  const TexturePreloadStats preloadStats = PreloadMapTextures(*loadedMap);
  const auto preloadEnd = Clock::now();

  const auto materializationStart = Clock::now();
  DestroyCurrentMap(mapState);
  ClearMapData(world);

  mapState.mapRoot = world.entity("MapRoot");
  mapState.currentPath = mapPath.value;

  SpawnStairs(world, *loadedMap, mapState.mapRoot);

  auto &mapBounds = world.get_mut<MapBounds>();
  mapBounds.dimension = loadedMap->dimensions;
  world.modified<MapBounds>();

  auto &activeData = world.get_mut<ActiveMapData>();
  activeData.textureBank = loadedMap->textureBank;
  activeData.tileWidth = loadedMap->tileWidth;
  activeData.tileHeight = loadedMap->tileHeight;
  activeData.chunkPixelWidth = loadedMap->chunkPixelWidth;
  activeData.chunkPixelHeight = loadedMap->chunkPixelHeight;

  BuildChunkEntities(world, *loadedMap, activeData, mapState.mapRoot);
  const auto materializationEnd = Clock::now();

  const double preloadMilliseconds = ElapsedMilliseconds(preloadStart, preloadEnd);
  const double materializationMilliseconds = ElapsedMilliseconds(materializationStart, materializationEnd);
  const auto chunkCount = static_cast<unsigned long long>(loadedMap->chunks.size());
  const auto tileCount = static_cast<unsigned long long>(CountTiles(*loadedMap));
  const auto stairCount = static_cast<unsigned long long>(loadedMap->stairs.size());
  TraceLog(
      preloadStats.failed == 0 ? LOG_INFO : LOG_WARNING,
      "Map switch '%s': source/cache=%.3f ms (%s), texture preload=%.3f ms (%llu requested, %llu ready, %llu failed), world materialization=%.3f ms (%llu chunks, %llu tiles, %llu stairs)",
      mapPath.value.c_str(),
      sourceMilliseconds,
      cacheHit ? "cache hit" : "source load",
      preloadMilliseconds,
      static_cast<unsigned long long>(preloadStats.requested),
      static_cast<unsigned long long>(preloadStats.ready),
      static_cast<unsigned long long>(preloadStats.failed),
      materializationMilliseconds,
      chunkCount,
      tileCount,
      stairCount);
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

} // namespace MapManager::Internal

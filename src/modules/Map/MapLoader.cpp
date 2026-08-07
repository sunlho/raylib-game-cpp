#include <array>
#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Map.h"
#include "MapInternal.h"

#include "modules/Camera.h"
#include "modules/Physics.h"
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
  auto &worldBounds = world.get_mut<Core::WorldBounds>();
  worldBounds.dimension = Vector2{0.0f, 0.0f};
  world.modified<Core::WorldBounds>();

  auto &activeData = world.get_mut<ActiveMapData>();
  activeData.textureBank.reset();
  activeData.staticTiles.clear();
  activeData.sortableTiles.clear();
  activeData.spawnPoints.clear();
  activeData.tileWidth = 0;
  activeData.tileHeight = 0;
  activeData.chunkPixelWidth = 0;
  activeData.chunkPixelHeight = 0;
}

void SpawnStairs(flecs::world &world, const Tilemap::LoadedMap &loadedMap, flecs::entity mapRoot) {
  for (std::size_t stairIndex = 0; stairIndex < loadedMap.stairs.size(); ++stairIndex) {
    const std::string stairName = std::format("MapStair_{}", stairIndex);

    auto stairEntity = mapRoot.is_valid() ? world.entity(flecs::Parent{mapRoot}, stairName.c_str()) : world.entity(stairName.c_str());

    stairEntity.set<Stairs::StairData>(loadedMap.stairs[stairIndex]);
  }
}

void SpawnWorldBoundary(flecs::world &world, Vector2 dimensions, flecs::entity mapRoot) {
  if (dimensions.x <= 0.0f || dimensions.y <= 0.0f) {
    return;
  }

  constexpr float Thickness = 64.0f;
  const std::array<Rectangle, 4> walls = {
      Rectangle{0.0f, -Thickness, dimensions.x, Thickness},
      Rectangle{0.0f, dimensions.y, dimensions.x, Thickness},
      Rectangle{-Thickness, 0.0f, Thickness, dimensions.y},
      Rectangle{dimensions.x, 0.0f, Thickness, dimensions.y}};

  for (std::size_t index = 0; index < walls.size(); ++index) {
    const std::string name = std::format("MapBoundary_{}", index);
    auto entity = world.entity(flecs::Parent{mapRoot}, name.c_str());
    Physics::CreateStaticCollision(entity, Physics::StaticCollisionShape::Box, walls[index]);
  }
}

flecs::entity EnsureLayerGroup(flecs::world &world, std::unordered_map<int, flecs::entity> &layerGroups, int layerIndex, flecs::entity mapRoot) {
  auto [groupIt, inserted] = layerGroups.try_emplace(layerIndex);
  if (inserted) {
    const std::string layerName = "MapLayer_" + std::to_string(layerIndex);

    auto layerEntity = mapRoot.is_valid() ? world.entity(flecs::Parent{mapRoot}, layerName.c_str()) : world.entity(layerName.c_str());

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

TexturePreloadStats PreloadMapTexturesImpl(Tilemap::LoadedMap &loadedMap, std::string &failedPath) {
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
      if (failedPath.empty()) {
        failedPath = path;
      }
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

} // namespace

bool PreloadMapTextures(Tilemap::LoadedMap &loadedMap, std::string &failedPath) {
  failedPath.clear();
  return PreloadMapTexturesImpl(loadedMap, failedPath).failed == 0;
}

void CommitLoadedMap(flecs::world &world, const Tilemap::LoadedMap &loadedMap, const std::string &path) {
  auto &mapState = world.get_mut<MapState>();
  const auto materializationStart = Clock::now();
  DestroyCurrentMap(mapState);
  ClearMapData(world);

  mapState.mapRoot = world.entity("MapRoot");
  mapState.currentPath = path;

  SpawnStairs(world, loadedMap, mapState.mapRoot);

  auto &worldBounds = world.get_mut<Core::WorldBounds>();
  worldBounds.dimension = loadedMap.dimensions;
  world.modified<Core::WorldBounds>();

  SpawnWorldBoundary(world, loadedMap.dimensions, mapState.mapRoot);

  auto &activeData = world.get_mut<ActiveMapData>();
  activeData.textureBank = loadedMap.textureBank;
  activeData.spawnPoints = loadedMap.spawnPoints;
  activeData.tileWidth = loadedMap.tileWidth;
  activeData.tileHeight = loadedMap.tileHeight;
  activeData.chunkPixelWidth = loadedMap.chunkPixelWidth;
  activeData.chunkPixelHeight = loadedMap.chunkPixelHeight;

  BuildChunkEntities(world, loadedMap, activeData, mapState.mapRoot);
  const auto materializationEnd = Clock::now();

  const double materializationMilliseconds = ElapsedMilliseconds(materializationStart, materializationEnd);
  const auto chunkCount = static_cast<unsigned long long>(loadedMap.chunks.size());
  const auto tileCount = static_cast<unsigned long long>(CountTiles(loadedMap));
  const auto stairCount = static_cast<unsigned long long>(loadedMap.stairs.size());
  const auto spawnPointCount = static_cast<unsigned long long>(loadedMap.spawnPoints.size());
  TraceLog(
      LOG_INFO,
      "Map commit '%s': world materialization=%.3f ms (%llu chunks, %llu tiles, %llu stairs, %llu spawn points)",
      path.c_str(),
      materializationMilliseconds,
      chunkCount,
      tileCount,
      stairCount,
      spawnPointCount);
}

} // namespace MapManager::Internal

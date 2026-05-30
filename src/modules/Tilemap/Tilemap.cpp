#include "Tilemap.h"
#include "TilemapInternal.h"

#include "box2d/box2d.h"

#include "modules/Assets.h"
#include "modules/Reflection.h"

namespace {

void ResetTilemapState(flecs::world world) {
  auto &chunkIndex = world.get_mut<Tilemap::ChunkIndex>();
  chunkIndex.chunkEntityMap.clear();
  chunkIndex.objects.clear();
  chunkIndex.allChunkEntities.clear();
  world.modified<Tilemap::ChunkIndex>();

  auto &mapBounds = world.get_mut<Tilemap::MapBounds>();
  mapBounds.dimension = Vector2{0.0f, 0.0f};
  world.modified<Tilemap::MapBounds>();
}

void DestroyCurrentMapRoot(flecs::world world) {
  auto &tilemapState = world.get_mut<Tilemap::TilemapState>();

  if (tilemapState.mapRoot.is_valid()) {
    tilemapState.mapRoot.destruct();
    tilemapState.mapRoot = {};
  }
}

void LoadTilemapFromPath(flecs::world world, const Tilemap::TilemapPath &map) {
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

  auto textureBank = TilemapInternal::LoadTilesetTextures(tilemap, map.value);

  DestroyCurrentMapRoot(world);
  ResetTilemapState(world);

  auto &tilemapState = world.get_mut<Tilemap::TilemapState>();
  tilemapState.mapRoot = world.entity("tilemap_root");

  const auto mapTileCount = tilemap.getTileCount();
  const auto mapTileSize = tilemap.getTileSize();
  auto &mapBounds = world.get_mut<Tilemap::MapBounds>();
  mapBounds.dimension = Vector2{
      static_cast<float>(mapTileCount.x) * static_cast<float>(mapTileSize.x),
      static_cast<float>(mapTileCount.y) * static_cast<float>(mapTileSize.y)};
  world.modified<Tilemap::MapBounds>();

  auto &chunkIndex = world.get_mut<Tilemap::ChunkIndex>();

  int layerIndex = 0;
  for (const auto &layer : tilemap.getLayers()) {
    const auto layerName = layer ? layer->getName() : std::string();
    const auto layerGroupName = layerName.empty() ? (std::string("TilemapLayer_") + std::to_string(layerIndex)) : (layerName + "_" + std::to_string(layerIndex));
    auto layerGroup = world.entity(layerGroupName.c_str());

    if (tilemapState.mapRoot.is_valid()) {
      layerGroup.add(flecs::ChildOf, tilemapState.mapRoot);
    }

    if (!layer || layer->getType() != tmx::Layer::Type::Tile) {
      if (layer && layer->getType() == tmx::Layer::Type::Object) {
        const auto &objectGroup = layer->getLayerAs<tmx::ObjectGroup>();
        TilemapInternal::BuildObjectChunks(world, tilemap, objectGroup, layerIndex, layerGroup, textureBank, chunkIndex);
      }

      layerIndex++;
      continue;
    }

    const auto &tileLayer = layer->getLayerAs<tmx::TileLayer>();
    TilemapInternal::BuildLayerChunks(world, tilemap, tileLayer, layerIndex, layerGroup, textureBank, chunkIndex);
    layerIndex++;
  }

  world.modified<Tilemap::ChunkIndex>();
  TraceLog(LOG_INFO, "Tilemap loaded and chunked from: %s", mapPath.string().c_str());
}

} // namespace

void Tilemap::Import(flecs::world &world) {
  Reflection::Register<ChunkTile>(world);
  Reflection::Register<ChunkAnimFrame>(world);
  Reflection::Register<ChunkAnimTile>(world);
  Reflection::Register<Chunk>(world);
  Reflection::Register<ChunkDrawable>(world);
  Reflection::Register<ChunkObject>(world);
  Reflection::Register<TilemapPath>(world);
  Reflection::Register<MapBounds>(world);

  world.component<b2WorldId>()
      .add(flecs::Singleton);
  world.component<b2BodyId>();

  b2WorldDef physicsWorldDef = b2DefaultWorldDef();
  physicsWorldDef.gravity = b2Vec2{0.0f, 0.0f};
  world.set<b2WorldId>(b2CreateWorld(&physicsWorldDef));

  world.component<ChunkIndex>()
      .add(flecs::Singleton);
  world.set<ChunkIndex>({});
  world.component<MapBounds>()
      .add(flecs::Singleton);
  world.set<MapBounds>({});

  world.component<TilemapState>()
      .add(flecs::Singleton);
  world.set<TilemapState>({});

  TilemapInternal::RegisterCollisionObservers(world);

  world.system<const TilemapPath>("Load Tilemap")
      .kind(flecs::OnStart)
      .each([](flecs::entity entity, const TilemapPath &map) {
        auto world = entity.world();
        LoadTilemapFromPath(world, map);
      });
}

void Tilemap::SetTilemapPath(flecs::world &world, const std::string &path) {
  auto tilemapEntity = world.entity("tilemap");
  TilemapPath tilemapPath{path};
  tilemapEntity.set<TilemapPath>(tilemapPath);

  if (IsWindowReady()) {
    LoadTilemapFromPath(world, tilemapPath);
  }
}

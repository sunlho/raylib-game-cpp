#include "MapManage.h"

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "modules/Reflection.h"
#include "modules/Rendering.h"
#include "modules/Tilemap/Tilemap.h"
#include "modules/Tilemap/TilemapInternal.h"

namespace {

class ChunkRenderable final : public Rendering::Renderable {
public:
  ChunkRenderable(std::vector<Tilemap::ChunkTile> tiles, std::shared_ptr<TilemapInternal::TilemapTextureBank> textureBank)
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
  std::shared_ptr<TilemapInternal::TilemapTextureBank> textureBank_;
};

struct LoadedMapState {
  std::shared_ptr<TilemapInternal::TilemapTextureBank> textureBank;
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

  auto &loadedState = world.get_mut<LoadedMapState>();
  loadedState.textureBank.reset();
}

void CreateChunkEntity(flecs::world &world, const Tilemap::Chunk &chunk, const std::shared_ptr<TilemapInternal::TilemapTextureBank> &textureBank, flecs::entity layerGroup) {
  const Rectangle chunkRect = chunk.destRect;
  const float chunkWidth = chunkRect.width;
  const float chunkHeight = chunkRect.height;

  auto renderable = std::make_shared<ChunkRenderable>(chunk.tiles, textureBank);

  Rendering::RenderComponent renderComponent;
  renderComponent.object = renderable;
  renderComponent.visible = true;

  Rendering::Position position;
  position.value = Vector2{
      chunkRect.x + (chunkWidth * 0.5f),
      chunkRect.y + (chunkHeight * 0.5f)};

  auto chunkEntity = world.entity();
  chunkEntity
      .set<Tilemap::Chunk>(chunk)
      .set<Rendering::Position>(position)
      .set<Rendering::RenderComponent>(renderComponent);

  if (layerGroup.is_valid()) {
    chunkEntity.add(flecs::ChildOf, layerGroup);
  }
}

void LoadMapFromPath(flecs::world world, const MapManage::MapPath &mapPath) {
  Tilemap::LoadedMap loadedMap;
  if (!Tilemap::LoadFromPath(mapPath.value, loadedMap)) {
    return;
  }

  auto &mapState = world.get_mut<MapManage::MapState>();
  DestroyCurrentMap(mapState);
  ClearMapData(world);

  mapState.mapRoot = world.entity("map_root");

  auto &mapBounds = world.get_mut<Tilemap::MapBounds>();
  mapBounds = loadedMap.bounds;
  world.modified<Tilemap::MapBounds>();

  auto &loadedState = world.get_mut<LoadedMapState>();
  loadedState.textureBank = loadedMap.textureBank;

  std::unordered_map<int, flecs::entity> layerGroups;

  for (const auto &chunk : loadedMap.chunks) {
    auto [groupIt, inserted] = layerGroups.try_emplace(chunk.layerIndex);
    if (inserted) {
      const std::string layerName = "MapLayer_" + std::to_string(chunk.layerIndex);
      groupIt->second = world.entity(layerName.c_str());

      if (mapState.mapRoot.is_valid()) {
        groupIt->second.add(flecs::ChildOf, mapState.mapRoot);
      }
    }

    CreateChunkEntity(world, chunk, loadedState.textureBank, groupIt->second);
  }
}

} // namespace

void MapManage::Import(flecs::world &world) {
  Reflection::Register<Tilemap::MapBounds>(world);
  Reflection::Register<MapPath>(world);
  Reflection::Register<MapState>(world);

  world.component<Tilemap::MapBounds>()
      .add(flecs::Singleton);
  world.set<Tilemap::MapBounds>({});

  world.component<MapState>()
      .add(flecs::Singleton);
  world.set<MapState>({});

  world.component<LoadedMapState>()
      .add(flecs::Singleton);
  world.set<LoadedMapState>({});

  world.system<const MapPath>("Load Map")
      .kind(flecs::OnStart)
      .each([](flecs::entity entity, const MapPath &mapPath) {
        auto world = entity.world();
        LoadMapFromPath(world, mapPath);
      });
}

void MapManage::SetMapPath(flecs::world &world, const std::string &path) {
  auto mapEntity = world.entity("map");
  mapEntity.set<MapPath>(MapPath{path});

  if (IsWindowReady()) {
    LoadMapFromPath(world, MapPath{path});
  }
}

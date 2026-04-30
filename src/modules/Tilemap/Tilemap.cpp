#include "Tilemap.h"
#include "TilemapInternal.h"

#include "modules/Assets.h"
#include "modules/Reflection.h"

void Tilemap::Import(flecs::world &world) {
  Reflection::Register<ChunkTile>(world);
  Reflection::Register<ChunkAnimFrame>(world);
  Reflection::Register<ChunkAnimTile>(world);
  Reflection::Register<Chunk>(world);
  Reflection::Register<ChunkDrawable>(world);
  Reflection::Register<ChunkObject>(world);
  world.component<ChunkIndex>()
      .add(flecs::Singleton);
  world.set<ChunkIndex>({});

  Reflection::Register<TilemapPath>(world);

  world.system<const TilemapPath>("Load Tilemap")
      .kind(flecs::OnStart)
      .each([](flecs::iter &it, size_t i, const TilemapPath &map) {
        auto world = it.world();
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

        auto textureBank = TilemapInternal::LoadTilesetTextures(tilemap, map.value);

        int layerIndex = 0;
        for (const auto &layer : tilemap.getLayers()) {
          const auto layerName = layer ? layer->getName() : std::string();
          const auto layerGroupName = layerName.empty() ? (std::string("TilemapLayer_") + std::to_string(layerIndex)) : (layerName + "_" + std::to_string(layerIndex));
          auto layerGroup = world.entity(layerGroupName.c_str());

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

        world.modified<ChunkIndex>();
        TraceLog(LOG_INFO, "Tilemap loaded and chunked from: %s", mapPath.string().c_str());
      });
}

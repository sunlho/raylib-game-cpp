#include "Tilemap.h"

#include "TilemapInternal.h"

#include "modules/Assets.h"

namespace Tilemap {

bool LoadFromPath(const std::string &path, LoadedMap &loadedMap) {
  loadedMap = {};

  const auto mapPath = Assets::Path(path);

  if (!Assets::Exists(path)) {
    TraceLog(LOG_WARNING, "Tilemap file not found: %s", mapPath.string().c_str());
    return false;
  }

  tmx::Map tilemap;
  if (!tilemap.load(mapPath.string())) {
    TraceLog(LOG_WARNING, "Failed to load tilemap: %s", mapPath.string().c_str());
    return false;
  }

  loadedMap.textureBank = TilemapInternal::LoadTilesetTextures(tilemap, path);

  const auto mapTileCount = tilemap.getTileCount();
  const auto mapTileSize = tilemap.getTileSize();
  loadedMap.tileWidth = mapTileSize.x;
  loadedMap.tileHeight = mapTileSize.y;
  loadedMap.chunkPixelHeight = Tilemap::CHUNK_SIZE * mapTileSize.y;
  loadedMap.chunkPixelWidth = Tilemap::CHUNK_SIZE * mapTileSize.x;
  loadedMap.dimensions = Vector2{
      static_cast<float>(mapTileCount.x) * static_cast<float>(mapTileSize.x),
      static_cast<float>(mapTileCount.y) * static_cast<float>(mapTileSize.y)};

  int layerIndex = 0;
  for (const auto &layer : tilemap.getLayers()) {
    if (!layer) {
      layerIndex++;
      continue;
    }

    if (layer->getType() == tmx::Layer::Type::Tile) {
      const auto &tileLayer = layer->getLayerAs<tmx::TileLayer>();
      TilemapInternal::BuildLayerChunks(tilemap, tileLayer, layerIndex, loadedMap);
    } else if (layer->getType() == tmx::Layer::Type::Object) {
      const auto &objectGroup = layer->getLayerAs<tmx::ObjectGroup>();
      const auto classType = objectGroup.getClass();
      if (classType == "Collisions") {
        TilemapInternal::BuildObjectCollisions(tilemap, objectGroup, layerIndex, loadedMap);
      } else {
        TilemapInternal::BuildObjectChunks(tilemap, objectGroup, layerIndex, loadedMap);
      }
    }

    layerIndex++;
  }

  TraceLog(LOG_INFO, "Tilemap loaded from: %s", mapPath.string().c_str());
  return true;
}

} // namespace Tilemap

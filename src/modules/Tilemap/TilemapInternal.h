#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "raylib.h"
#include "tmxlite/Map.hpp"
#include "tmxlite/ObjectGroup.hpp"
#include "tmxlite/TileLayer.hpp"

#include "Tilemap.h"

namespace Tilemap {

namespace TilemapInternal {

constexpr std::uint32_t TMX_FLIP_BITS_REMOVAL = 0x1FFFFFFFU;

int CeilDiv(int value, int divisor);
Rectangle ComputeSourceRect(const Tilemap::TilemapTileset &tileset, std::uint32_t gid);

std::shared_ptr<Tilemap::TilemapTextureBank> LoadTilesetTextures(const tmx::Map &tilemap, const std::string &mapRelativePath);

void BuildLayerChunks(const tmx::Map &tilemap, const tmx::TileLayer &layer, int layerIndex, Tilemap::LoadedMap &loadedMap);

void BuildObjectChunks(const tmx::Map &tilemap, const tmx::ObjectGroup &objectGroup, int layerIndex, Tilemap::LoadedMap &loadedMap);

} // namespace TilemapInternal

} // namespace Tilemap

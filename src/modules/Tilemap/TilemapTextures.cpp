#include "TilemapInternal.h"

#include <filesystem>

#include "modules/Assets.h"

namespace TilemapInternal {

TilemapTextureBank::~TilemapTextureBank() {
  for (auto &tileset : tilesets) {
    if (tileset.texture.id != 0) {
      UnloadTexture(tileset.texture);
      tileset.texture = Texture2D{};
    }
  }
}

int FindTilesetIndexByGid(const TilemapTextureBank &textureBank, std::uint32_t gid) {
  for (std::size_t i = 0; i < textureBank.tilesets.size(); ++i) {
    const auto &tileset = textureBank.tilesets[i];
    if (gid >= tileset.firstGid && gid <= tileset.lastGid) {
      return static_cast<int>(i);
    }
  }

  return -1;
}

Rectangle ComputeSourceRect(const TilemapTilesetTexture &tileset, std::uint32_t gid) {
  const std::uint32_t localId = gid - tileset.firstGid;

  if (tileset.columnCount <= 0) {
    return Rectangle{0, 0, 0, 0};
  }

  const int column = static_cast<int>(localId % static_cast<std::uint32_t>(tileset.columnCount));
  const int row = static_cast<int>(localId / static_cast<std::uint32_t>(tileset.columnCount));

  const int stepX = tileset.tileWidth + tileset.spacing;
  const int stepY = tileset.tileHeight + tileset.spacing;

  return Rectangle{
      static_cast<float>(tileset.margin + column * stepX),
      static_cast<float>(tileset.margin + row * stepY),
      static_cast<float>(tileset.tileWidth),
      static_cast<float>(tileset.tileHeight)};
}

std::shared_ptr<TilemapTextureBank> LoadTilesetTextures(const tmx::Map &tilemap, const std::string &mapRelativePath) {
  auto textureBank = std::make_shared<TilemapTextureBank>();
  const auto &tilesets = tilemap.getTilesets();
  textureBank->tilesets.reserve(tilesets.size());

  for (std::size_t i = 0; i < tilesets.size(); ++i) {
    const auto &tileset = tilesets[i];
    const auto imagePath = tileset.getImagePath();

    if (imagePath.empty()) {
      TraceLog(LOG_WARNING, "Tileset[%d] has empty image path, skip", static_cast<int>(i));
      continue;
    }

    const auto mapDir = std::filesystem::path(mapRelativePath).parent_path();
    const auto textureRelative = (mapDir / imagePath).lexically_normal().generic_string();
    const auto texturePath = Assets::Path(textureRelative);

    if (!Assets::Exists(textureRelative)) {
      TraceLog(LOG_WARNING, "Tileset image not found: %s", texturePath.string().c_str());
      continue;
    }

    TilemapTilesetTexture loaded;
    loaded.texture = LoadTexture(texturePath.string().c_str());
    loaded.firstGid = tileset.getFirstGID();
    loaded.lastGid = tileset.getFirstGID() + tileset.getTileCount() - 1;
    loaded.tileWidth = static_cast<int>(tileset.getTileSize().x);
    loaded.tileHeight = static_cast<int>(tileset.getTileSize().y);
    loaded.columnCount = static_cast<int>(tileset.getColumnCount());
    loaded.spacing = static_cast<int>(tileset.getSpacing());
    loaded.margin = static_cast<int>(tileset.getMargin());

    textureBank->tilesets.push_back(loaded);
  }

  return textureBank;
}

} // namespace TilemapInternal

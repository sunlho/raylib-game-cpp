#include "TilemapInternal.h"

#include <cmath>
#include <filesystem>

#include "Tilemap.h"
#include "modules/Assets.h"

namespace Tilemap {
namespace Internal {

Rectangle ComputeSourceRect(const TilemapTileObject &tile, std::uint32_t gid) {
  return tile.srcRect;
}

std::shared_ptr<TilemapTextureBank> LoadTilesetTextures(const tmx::Map &tilemap, const std::string &mapRelativePath) {
  auto textureBank = std::make_shared<TilemapTextureBank>();
  const auto mapDir = std::filesystem::path(mapRelativePath).parent_path();

  const auto &tilesets = tilemap.getTilesets();
  for (const auto &tileset : tilesets) {
    const auto &tiles = tileset.getTiles();

    for (const auto &tile : tiles) {
      const std::uint32_t tileId = tile.ID + tileset.getFirstGID();
      TilemapTileObject tileObject;

      const auto tileImagePath = tile.imagePath;
      if (!tileImagePath.empty()) {
        const auto tileTextureRelative = (mapDir / tileImagePath).lexically_normal().generic_string();
        tileObject.texturePath = tileTextureRelative;
      } else {
        const auto tilesetImagePath = tileset.getImagePath();
        if (!tilesetImagePath.empty()) {
          const auto textureRelative = (mapDir / tilesetImagePath).lexically_normal().generic_string();
          tileObject.texturePath = textureRelative;
        }
      }
      const auto tileSizeX = tileset.getTileSize().x;
      const auto tileSizeY = tileset.getTileSize().y;
      tileObject.tileWidth = static_cast<int>(tileSizeX ? tileSizeX : tile.imageSize.x);
      tileObject.tileHeight = static_cast<int>(tileSizeY ? tileSizeY : tile.imageSize.y);

      const auto &animationFrames = tile.animation.frames;
      tileObject.animation.frames.reserve(animationFrames.size());
      for (const auto &frame : animationFrames) {
        if (frame.duration == 0) {
          TraceLog(LOG_WARNING, "Ignored zero-duration animation frame for tile GID %u", tileId);
          continue;
        }

        const float durationSeconds = static_cast<float>(frame.duration) / 1000.0f;
        tileObject.animation.frames.push_back(TileAnimationFrame{
            frame.tileID,
            durationSeconds});
        tileObject.animation.durationSeconds += durationSeconds;
      }

      const std::uint32_t localId = tile.ID;
      const int columnCount = static_cast<int>(tileset.getColumnCount());
      if (columnCount > 0) {
        const int column = static_cast<int>(localId % static_cast<std::uint32_t>(columnCount));
        const int row = static_cast<int>(localId / static_cast<std::uint32_t>(columnCount));
        const int stepX = tileObject.tileWidth + static_cast<int>(tileset.getSpacing());
        const int stepY = tileObject.tileHeight + static_cast<int>(tileset.getSpacing());

        tileObject.srcRect = Rectangle{
            static_cast<float>(static_cast<int>(tileset.getMargin()) + column * stepX),
            static_cast<float>(static_cast<int>(tileset.getMargin()) + row * stepY),
            static_cast<float>(tileObject.tileWidth),
            static_cast<float>(tileObject.tileHeight)};
      } else {
        tileObject.srcRect = Rectangle{
            0.0f,
            0.0f,
            static_cast<float>(tileObject.tileWidth),
            static_cast<float>(tileObject.tileHeight)};
      }

      const auto &objectGroup = tile.objectGroup;
      const auto &objects = objectGroup.getObjects();
      tileObject.collisions.reserve(objects.size());

      for (const auto &object : objects) {
        Tilemap::CollisionData collision;
        collision.shape = static_cast<Tilemap::CollisionShape>(object.getShape());
        auto const aabb = object.getAABB();
        collision.AABB = Rectangle{aabb.left, aabb.top, aabb.width, aabb.height};
        collision.position = Vector2{object.getPosition().x, object.getPosition().y};
        collision.rotation = object.getRotation();

        const auto &points = object.getPoints();
        collision.points.reserve(points.size());
        for (const auto &point : points) {
          collision.points.emplace_back(point.x, point.y);
        }

        tileObject.collisions.push_back(std::move(collision));
      }
      tileObject.properties = tile.properties;

      textureBank->tiles[tileId] = std::move(tileObject);
    }
  }

  return textureBank;
}

} // namespace Internal

Texture2D TilemapTextureBank::getOrLoadTexture(const std::string &path) {
  if (path.empty()) {
    return Texture2D{};
  }

  auto it = textureCache.find(path);
  if (it != textureCache.end()) {
    return it->second;
  }

  const auto texturePath = Assets::Path(path);
  if (!Assets::Exists(path)) {
    TraceLog(LOG_WARNING, "Texture not found: %s", texturePath.string().c_str());
    return Texture2D{};
  }

  Texture2D texture = LoadTexture(texturePath.string().c_str());
  textureCache[path] = texture;
  return texture;
}

const TilemapTileObject *TilemapTextureBank::getTileForRendering(std::uint32_t gid) const {
  const auto *tile = getTile(gid);
  if (!tile || tile->animation.frames.empty() || tile->animation.currentFrame >= tile->animation.frames.size()) {
    return tile;
  }

  const auto *frameTile = getTile(tile->animation.frames[tile->animation.currentFrame].tileGid);
  return frameTile ? frameTile : tile;
}

void TilemapTextureBank::updateAnimations(float deltaSeconds) {
  if (!(deltaSeconds > 0.0f)) {
    return;
  }

  for (auto &[gid, tile] : tiles) {
    (void)gid;
    auto &animation = tile.animation;
    if (animation.frames.empty() || !(animation.durationSeconds > 0.0f)) {
      continue;
    }

    animation.elapsedSeconds = std::fmod(animation.elapsedSeconds + deltaSeconds, animation.durationSeconds);
    float frameTime = animation.elapsedSeconds;
    animation.currentFrame = 0;
    for (std::size_t frameIndex = 0; frameIndex < animation.frames.size(); ++frameIndex) {
      const float frameDuration = animation.frames[frameIndex].durationSeconds;
      if (frameTime < frameDuration) {
        animation.currentFrame = frameIndex;
        break;
      }
      frameTime -= frameDuration;
    }
  }
}

void TilemapTextureBank::resetAnimations() {
  for (auto &[gid, tile] : tiles) {
    (void)gid;
    tile.animation.currentFrame = 0;
    tile.animation.elapsedSeconds = 0.0f;
  }
}

TilemapTextureBank::~TilemapTextureBank() {
  for (auto &[path, texture] : textureCache) {
    if (texture.id != 0) {
      UnloadTexture(texture);
    }
  }
  textureCache.clear();
}

} // namespace Tilemap

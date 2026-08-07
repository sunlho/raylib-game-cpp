#include <memory>
#include <string>
#include <utility>

#include "webp/decode.h"
#include "webp/demux.h"

#include "CharacterInternal.h"

#include "modules/Assets.h"

namespace Character::Internal {
namespace {

class RaylibPresentationBackend final : public PresentationBackend {
public:
  bool LoadAnimation(std::string_view path, LoadedAnimation &animation, std::string &error) override {
    const auto bytes = Assets::ReadBinary(path);
    if (!bytes || bytes->empty()) {
      error = "Sprite asset is unavailable: " + std::string(path);
      return false;
    }

    WebPData data = {};
    data.bytes = bytes->data();
    data.size = bytes->size();

    WebPAnimDecoderOptions options;
    if (!WebPAnimDecoderOptionsInit(&options)) {
      error = "Failed to initialize WebP decoder: " + std::string(path);
      return false;
    }
    options.color_mode = MODE_RGBA;

    WebPAnimDecoder *decoder = WebPAnimDecoderNew(&data, &options);
    if (decoder == nullptr) {
      error = "Failed to create WebP decoder: " + std::string(path);
      return false;
    }

    WebPAnimInfo info;
    if (!WebPAnimDecoderGetInfo(decoder, &info) ||
        info.frame_count <= 0 || info.canvas_width <= 0 || info.canvas_height <= 0) {
      WebPAnimDecoderDelete(decoder);
      error = "Invalid WebP animation: " + std::string(path);
      return false;
    }

    LoadedAnimation candidate;
    candidate.width = info.canvas_width;
    candidate.height = info.canvas_height;
    candidate.frames.reserve(static_cast<std::size_t>(info.frame_count));

    uint8_t *rgba = nullptr;
    int timestamp = 0;
    while (WebPAnimDecoderHasMoreFrames(decoder)) {
      if (!WebPAnimDecoderGetNext(decoder, &rgba, &timestamp)) {
        break;
      }

      Image image = {};
      image.data = rgba;
      image.width = candidate.width;
      image.height = candidate.height;
      image.mipmaps = 1;
      image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

      const Texture2D texture = LoadTextureFromImage(image);
      if (texture.id == 0) {
        WebPAnimDecoderDelete(decoder);
        UnloadAnimation(candidate);
        error = "Failed to upload sprite texture: " + std::string(path);
        return false;
      }
      candidate.frames.push_back(texture);
    }

    WebPAnimDecoderDelete(decoder);
    if (candidate.frames.empty()) {
      error = "WebP animation has no decodable frames: " + std::string(path);
      return false;
    }

    animation = std::move(candidate);
    return true;
  }

  void UnloadAnimation(LoadedAnimation &animation) noexcept override {
    for (const auto &texture : animation.frames) {
      if (texture.id != 0) {
        UnloadTexture(texture);
      }
    }
    animation.frames.clear();
    animation.width = 0;
    animation.height = 0;
  }

  void DrawFrame(
      const LoadedAnimation &animation,
      int frame,
      Rectangle destination,
      Vector2 origin) const override {
    if (frame < 0 || frame >= static_cast<int>(animation.frames.size())) {
      return;
    }

    DrawTexturePro(
        animation.frames[static_cast<std::size_t>(frame)],
        Rectangle{0.0f, 0.0f, static_cast<float>(animation.width), static_cast<float>(animation.height)},
        destination,
        origin,
        0.0f,
        WHITE);
  }
};

} // namespace

std::shared_ptr<PresentationBackend> CreateRaylibPresentationBackend() {
  return std::make_shared<RaylibPresentationBackend>();
}

} // namespace Character::Internal

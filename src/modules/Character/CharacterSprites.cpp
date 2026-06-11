
#include <algorithm>
#include <cstring>
#include <utility>

#include "raylib.h"
#include "raymath.h"
#include "webp/decode.h"
#include "webp/demux.h"

#include "CharacterInternal.h"
#include "modules/Assets.h"
#include "modules/Rendering.h"

namespace Character {
namespace {

bool LoadWebPAnimation(std::string_view path, SpriteAnimation &animation, int &outFrames) {
  const auto bytes = Assets::ReadBinary(path);
  if (!bytes || bytes->empty()) {
    return false;
  }

  WebPData data = {};
  data.bytes = bytes->data();
  data.size = bytes->size();

  WebPAnimDecoderOptions options;
  if (!WebPAnimDecoderOptionsInit(&options)) {
    return false;
  }
  options.color_mode = MODE_RGBA;

  WebPAnimDecoder *decoder = WebPAnimDecoderNew(&data, &options);
  if (decoder == nullptr) {
    return false;
  }

  WebPAnimInfo info;
  if (!WebPAnimDecoderGetInfo(decoder, &info)) {
    WebPAnimDecoderDelete(decoder);
    return false;
  }

  if (info.frame_count <= 0 || info.canvas_width <= 0 || info.canvas_height <= 0) {
    WebPAnimDecoderDelete(decoder);
    return false;
  }

  const int bytesPerFrame = info.canvas_width * info.canvas_height * 4;
  animation.width = info.canvas_width;
  animation.height = info.canvas_height;
  animation.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  animation.bytesPerFrame = bytesPerFrame;
  animation.lastFrame = -1;
  animation.pixels.resize(static_cast<std::size_t>(bytesPerFrame) * info.frame_count);

  int frameIndex = 0;
  uint8_t *rgba = nullptr;
  int timestamp = 0;
  while (WebPAnimDecoderHasMoreFrames(decoder) && frameIndex < info.frame_count) {
    if (!WebPAnimDecoderGetNext(decoder, &rgba, &timestamp)) {
      break;
    }

    std::memcpy(animation.pixels.data() + static_cast<std::size_t>(bytesPerFrame) * frameIndex,
                rgba,
                static_cast<std::size_t>(bytesPerFrame));
    frameIndex++;
  }

  WebPAnimDecoderDelete(decoder);

  if (frameIndex <= 0) {
    return false;
  }

  outFrames = frameIndex;
  return true;
}

} // namespace

void RegisterCharacterSprites(flecs::world &world) {
  world.observer<SpriteSet, AnimationController>("Load Character Sprites Observer")
      .event(flecs::OnSet)
      .each([](SpriteSet &spriteSet, AnimationController &controller) {
        if (spriteSet.loaded) {
          return;
        }

        controller.clips.clear();
        controller.currentClip = -1;
        controller.currentFrame = 0;
        controller.elapsed = 0.0f;

        for (auto &entry : spriteSet.entries) {
          if (entry.path.empty()) {
            continue;
          }

          if (!Assets::Exists(entry.path)) {
            TraceLog(LOG_WARNING, "Sprite not found: %s", entry.path.c_str());
            continue;
          }

          int frames = 0;
          if (!LoadWebPAnimation(entry.path, entry.animation, frames)) {
            TraceLog(LOG_WARNING, "Failed to decode WebP: %s", entry.path.c_str());
            continue;
          }

          Image image = {};
          image.data = entry.animation.pixels.data();
          image.width = entry.animation.width;
          image.height = entry.animation.height;
          image.mipmaps = 1;
          image.format = entry.animation.format;

          entry.animation.texture = LoadTextureFromImage(image);
          entry.animation.frameCount = frames;

          AnimationClip clip;
          clip.name = entry.name;
          clip.frameCount = frames;
          clip.frameDuration = entry.frameDuration;
          clip.loop = entry.loop;
          controller.AddAnimation(std::move(clip));
        }

        spriteSet.loaded = true;
      });

  world.system<CharacterInfo, const SpriteSet, AnimationController>("Select Character Animation")
      .kind<Character::Phases::Update>()
      .each([](const CharacterInfo &info, const SpriteSet &spriteSet, AnimationController &controller) {
        if (!spriteSet.loaded) {
          return;
        }

        const auto desired = CharacterInternal::BuildAnimationKey(info.state, info.direction);
        if (spriteSet.FindEntry(desired)) {
          controller.PlayAnimation(desired);
          return;
        }

        if (info.state != CharacterState::Idle) {
          const auto fallback = CharacterInternal::BuildAnimationKey(CharacterState::Idle, info.direction);
          if (spriteSet.FindEntry(fallback)) {
            controller.PlayAnimation(fallback);
          }
        }
      });

  world.system<const Rendering::Position, SpriteSet, const AnimationController>("Draw Character Sprites")
      .kind<Rendering::Phases::Draw>()
      .each([](const Rendering::Position &position, SpriteSet &spriteSet, const AnimationController &controller) {
        if (!spriteSet.loaded) {
          return;
        }

        const auto *clip = controller.GetCurrentAnimation();
        if (!clip) {
          return;
        }

        auto *entry = spriteSet.FindEntry(clip->name);
        if (!entry) {
          return;
        }

        auto &animation = entry->animation;
        if (animation.texture.id == 0 || animation.frameCount <= 0) {
          return;
        }

        int frame = controller.currentFrame;
        if (frame < 0) {
          frame = 0;
        }
        if (frame >= animation.frameCount) {
          frame = animation.frameCount - 1;
        }

        if (animation.bytesPerFrame > 0 &&
            frame != animation.lastFrame &&
            static_cast<std::size_t>(animation.bytesPerFrame) * (static_cast<std::size_t>(frame) + 1) <= animation.pixels.size()) {
          UpdateTexture(animation.texture, animation.pixels.data() + static_cast<std::size_t>(animation.bytesPerFrame) * frame);

          animation.lastFrame = frame;
        }

        Rectangle src = {
            0.0f,
            0.0f,
            static_cast<float>(animation.width),
            static_cast<float>(animation.height)};
        Rectangle dest = {
            position.value.x,
            position.value.y,
            static_cast<float>(animation.width) * spriteSet.scale,
            static_cast<float>(animation.height) * spriteSet.scale};
        dest.x = roundf(dest.x);
        dest.y = roundf(dest.y);
        Vector2 origin = spriteSet.useCenterOrigin ? Vector2{roundf(dest.width * 0.5f), roundf(dest.height * 0.5f)} : spriteSet.origin;

        DrawTexturePro(animation.texture, src, dest, origin, 0.0f, WHITE);
      });

  world.system<SpriteSet>("Unload Character Sprites")
      .kind(flecs::OnRemove)
      .each([](SpriteSet &spriteSet) {
        for (auto &entry : spriteSet.entries) {
          if (entry.animation.texture.id != 0) {
            UnloadTexture(entry.animation.texture);
            entry.animation.texture = Texture2D{};
          }
          entry.animation.pixels.clear();
          entry.animation.lastFrame = -1;
        }

        spriteSet.loaded = false;
      });
}
} // namespace Character

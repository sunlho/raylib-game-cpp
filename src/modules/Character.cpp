#include "Character.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Assets.h"
#include "Movement.h"
#include "Reflection.h"
#include "Rendering.h"
#include "raylib.h"
#include "raymath.h"
#include "webp/decode.h"
#include "webp/demux.h"

namespace Character {
namespace {
const char *DirectionSuffix(CharacterDirection direction) {
  switch (direction) {
  case CharacterDirection::Up:
    return "N";
  case CharacterDirection::Down:
    return "S";
  case CharacterDirection::Left:
    return "W";
  case CharacterDirection::Right:
    return "E";
  }

  return "S";
}

const char *StatePrefix(CharacterState state) {
  switch (state) {
  case CharacterState::Idle:
    return "idle";
  case CharacterState::Moving:
    return "walk";
  case CharacterState::Attacking:
    return "interact";
  case CharacterState::Hurt:
    return "idle";
  case CharacterState::Dead:
    return "idle";
  }

  return "idle";
}

std::string BuildAnimationKey(CharacterState state, CharacterDirection direction) {
  std::string key;
  key.reserve(16);
  key.append(StatePrefix(state));
  key.push_back('-');
  key.append(DirectionSuffix(direction));
  return key;
}

AnimationClip *FindClip(AnimationController &controller, std::string_view name) {
  const auto it = std::find_if(controller.clips.begin(), controller.clips.end(), [name](const AnimationClip &clip) {
    return clip.name == name;
  });
  if (it == controller.clips.end()) {
    return nullptr;
  }

  return &(*it);
}

float RandomDelaySeconds(float minDelay, float maxDelay) {
  const float clampedMin = std::max(0.0f, minDelay);
  const float clampedMax = std::max(clampedMin, maxDelay);
  const int minMs = static_cast<int>(clampedMin * 1000.0f);
  const int maxMs = static_cast<int>(clampedMax * 1000.0f);
  if (minMs == maxMs) {
    return static_cast<float>(minMs) / 1000.0f;
  }

  const int delayMs = GetRandomValue(minMs, maxMs);
  return static_cast<float>(delayMs) / 1000.0f;
}

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

void AnimationController::AddAnimation(AnimationClip clip) {
  if (clip.frameCount < 1) {
    clip.frameCount = 1;
  }
  if (clip.frameDuration <= 0.0f) {
    clip.frameDuration = 0.1f;
  }

  clips.push_back(std::move(clip));
}

bool AnimationController::PlayAnimation(std::string_view name, bool restart) {
  const auto it = std::find_if(clips.begin(), clips.end(), [name](const AnimationClip &clip) {
    return clip.name == name;
  });
  if (it == clips.end()) {
    return false;
  }

  const int index = static_cast<int>(std::distance(clips.begin(), it));
  if (currentClip != index || restart) {
    currentClip = index;
    currentFrame = 0;
    elapsed = 0.0f;
  }

  return true;
}

const AnimationClip *AnimationController::GetCurrentAnimation() const {
  if (currentClip < 0 || currentClip >= static_cast<int>(clips.size())) {
    return nullptr;
  }

  return &clips[static_cast<std::size_t>(currentClip)];
}

const SpriteEntry *SpriteSet::FindEntry(std::string_view name) const {
  const auto it = std::find_if(entries.begin(), entries.end(), [name](const SpriteEntry &entry) {
    return entry.name == name;
  });
  if (it == entries.end()) {
    return nullptr;
  }

  return &(*it);
}

SpriteEntry *SpriteSet::FindEntry(std::string_view name) {
  const auto it = std::find_if(entries.begin(), entries.end(), [name](const SpriteEntry &entry) {
    return entry.name == name;
  });
  if (it == entries.end()) {
    return nullptr;
  }

  return &(*it);
}

void Import(flecs::world &world) {
  world.component<CharacterState>();
  world.component<CharacterDirection>();

  Reflection::Register<CharacterInfo>(world);
  Reflection::Register<CharacterStats>(world);
  Reflection::Register<AnimationClip>(world);
  Reflection::Register<AnimationController>(world);
  Reflection::Register<IdleBehavior>(world);

  auto updatePhase = world.entity<Character::Phases::Update>();
  updatePhase
      .add(flecs::Phase)
      .depends_on(world.entity<Movement::Phases::Update>());

  world.entity<Movement::Phases::CameraFollow>()
      .depends_on(updatePhase);

  world.system<CharacterStats, CharacterInfo>("Clamp Character Health")
      .kind<Character::Phases::Update>()
      .each([](CharacterStats &stats, CharacterInfo &info) {
        if (stats.maxHealth < 1.0f) {
          stats.maxHealth = 1.0f;
        }

        if (stats.health > stats.maxHealth) {
          stats.health = stats.maxHealth;
        }

        if (stats.health <= 0.0f && info.state != CharacterState::Dead) {
          stats.health = 0.0f;
          info.state = CharacterState::Dead;
        }
      });

  world.system<CharacterInfo, const Movement::Velocity>("Update Character Direction")
      .kind<Character::Phases::Update>()
      .each([](CharacterInfo &info, const Movement::Velocity &velocity) {
        if (info.state == CharacterState::Dead) {
          return;
        }

        const float lengthSq = Vector2LengthSqr(velocity.value);
        if (lengthSq > 0.0001f) {
          if (std::fabs(velocity.value.x) >= std::fabs(velocity.value.y)) {
            info.direction = velocity.value.x >= 0.0f ? CharacterDirection::Right : CharacterDirection::Left;
          } else {
            info.direction = velocity.value.y >= 0.0f ? CharacterDirection::Down : CharacterDirection::Up;
          }

          if (info.state == CharacterState::Idle) {
            info.state = CharacterState::Moving;
          }
        } else if (info.state == CharacterState::Moving) {
          info.state = CharacterState::Idle;
        }
      });

  world.system<CharacterInfo, AnimationController, IdleBehavior>("Handle Idle Delay")
      .kind<Character::Phases::Update>()
      .each([](flecs::iter &it, size_t, CharacterInfo &info, AnimationController &controller, IdleBehavior &idle) {
        if (info.state == CharacterState::Moving) {
          idle.waiting = false;
          idle.playing = false;
          idle.wasMoving = true;
          return;
        }

        if (info.state != CharacterState::Idle) {
          idle.waiting = false;
          idle.playing = false;
          idle.wasMoving = false;
          return;
        }

        const auto idleKey = BuildAnimationKey(CharacterState::Idle, info.direction);

        if (!idle.initialized || idle.wasMoving) {
          idle.timer = RandomDelaySeconds(idle.minDelay, idle.maxDelay);
          idle.waiting = true;
          idle.playing = false;
          idle.wasMoving = false;
          idle.initialized = true;
          controller.PlayAnimation(idleKey, true);
          controller.currentFrame = 0;
          controller.elapsed = 0.0f;
        }

        if (idle.waiting) {
          idle.timer -= it.delta_time();
          if (idle.timer <= 0.0f) {
            idle.waiting = false;
            idle.playing = true;
            controller.PlayAnimation(idleKey, true);
            controller.currentFrame = 0;
            controller.elapsed = 0.0f;
            if (auto *clip = FindClip(controller, idleKey)) {
              clip->loop = false;
            }
          }
          return;
        }

        if (idle.playing) {
          const auto *clip = controller.GetCurrentAnimation();
          if (clip && clip->name == idleKey &&
              controller.currentFrame >= std::max(1, clip->frameCount) - 1) {
            idle.playing = false;
            idle.waiting = true;
            idle.timer = RandomDelaySeconds(idle.minDelay, idle.maxDelay);
            controller.currentFrame = 0;
            controller.elapsed = 0.0f;
          }
        }
      });

  world.system<SpriteSet, AnimationController>("Load Character Sprites")
      .kind<Character::Phases::Update>()
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

        const auto desired = BuildAnimationKey(info.state, info.direction);
        if (spriteSet.FindEntry(desired)) {
          controller.PlayAnimation(desired);
          return;
        }

        if (info.state != CharacterState::Idle) {
          const auto fallback = BuildAnimationKey(CharacterState::Idle, info.direction);
          if (spriteSet.FindEntry(fallback)) {
            controller.PlayAnimation(fallback);
          }
        }
      });

  world.system<AnimationController>("Advance Character Animations")
      .kind<Character::Phases::Update>()
      .each([](flecs::iter &it, size_t i, AnimationController &controller) {
        auto entity = it.entity(i);
        if (entity.has<IdleBehavior>()) {
          const auto &idle = entity.get<IdleBehavior>();
          if (idle.waiting) {
            return;
          }
        }

        if (controller.currentClip < 0 ||
            controller.currentClip >= static_cast<int>(controller.clips.size())) {
          return;
        }

        auto &clip = controller.clips[static_cast<std::size_t>(controller.currentClip)];
        const int frameCount = std::max(1, clip.frameCount);
        const float frameDuration = clip.frameDuration > 0.0f ? clip.frameDuration : 0.1f;

        controller.elapsed += it.delta_time();
        const int advanceFrames = static_cast<int>(controller.elapsed / frameDuration);
        if (advanceFrames <= 0) {
          return;
        }

        controller.elapsed -= static_cast<float>(advanceFrames) * frameDuration;
        if (clip.loop) {
          controller.currentFrame = (controller.currentFrame + advanceFrames) % frameCount;
        } else {
          controller.currentFrame = std::min(frameCount - 1, controller.currentFrame + advanceFrames);
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

#include "CharacterInternal.h"

#include <algorithm>
#include <cmath>

#include "modules/Movement.h"
#include "modules/Reflection.h"
#include "raylib.h"
#include "raymath.h"

namespace Character {
void RegisterCharacterAnimation(flecs::world &world) {
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
}
} // namespace Character

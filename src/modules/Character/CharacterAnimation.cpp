#include <cmath>

#include "raylib.h"
#include "raymath.h"

#include "Character.h"
#include "CharacterInternal.h"

#include "modules/Movement.h"
#include "modules/Reflection.h"

namespace Character::Internal {

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

  world.system<CharacterInfo, const Movement::RequestedVelocity>("Update Character Direction")
      .kind<Character::Phases::Update>()
      .each([](CharacterInfo &info, const Movement::RequestedVelocity &velocity) {
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
}

} // namespace Character::Internal

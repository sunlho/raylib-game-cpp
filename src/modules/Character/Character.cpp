#include "CharacterInternal.h"

#include "modules/Movement.h"
#include "modules/Reflection.h"
#include "modules/Rendering.h"

namespace Character {
void Import(flecs::world &world) {
  world.scope("Character");

  world.component<CharacterState>();
  world.component<CharacterDirection>();

  Reflection::Register<CharacterInfo>(world);
  Reflection::Register<CharacterStats>(world);
  Reflection::Register<AnimationClip>(world);
  Reflection::Register<AnimationController>(world);
  Reflection::Register<IdleBehavior>(world);

  RegisterCharacterAnimation(world);
  RegisterCharacterSprites(world);
}
} // namespace Character

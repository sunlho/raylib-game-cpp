#include "CharacterInternal.h"

#include "modules/Movement.h"
#include "modules/Reflection.h"
#include "modules/Rendering.h"

namespace Character {

module::module(flecs::world &world) {
  Reflection::Register<CharacterState>(world);
  Reflection::Register<CharacterDirection>(world);
  Reflection::Register<CharacterInfo>(world);
  Reflection::Register<CharacterStats>(world);
  Reflection::Register<AnimationClip>(world);
  Reflection::Register<AnimationController>(world);
  Reflection::Register<IdleBehavior>(world);
  Reflection::Register<SpriteAnimation>(world);
  Reflection::Register<SpriteEntry>(world);
  Reflection::Register<SpriteSet>(world);

  RegisterCharacterAnimation(world);
  RegisterCharacterSprites(world);
  RegisterCharacterPhysics(world);
}

} // namespace Character

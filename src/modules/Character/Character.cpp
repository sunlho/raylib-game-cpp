#include "CharacterInternal.h"

#include "modules/Reflection.h"

namespace Character {

module::module(flecs::world &world) {
  Reflection::Register<CharacterState>(world);
  Reflection::Register<CharacterDirection>(world);
  Reflection::Register<CharacterInfo>(world);
  Reflection::Register<CharacterStats>(world);

  Internal::RegisterCharacterAnimation(world);
  Internal::RegisterCharacterPhysics(world);
  Internal::EnsureCharacterPresentation(world);
}

} // namespace Character

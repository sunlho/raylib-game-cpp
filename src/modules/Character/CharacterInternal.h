#pragma once

#include <string>
#include <string_view>

#include "Character.h"

namespace Character {
namespace CharacterInternal {

const char *DirectionSuffix(CharacterDirection direction);
const char *StatePrefix(CharacterState state);
std::string BuildAnimationKey(CharacterState state, CharacterDirection direction);
AnimationClip *FindClip(AnimationController &controller, std::string_view name);
float RandomDelaySeconds(float minDelay, float maxDelay);

} // namespace CharacterInternal

void RegisterCharacterAnimation(flecs::world &world);
void RegisterCharacterSprites(flecs::world &world);
void RegisterCharacterPhysics(flecs::world &world);

} // namespace Character

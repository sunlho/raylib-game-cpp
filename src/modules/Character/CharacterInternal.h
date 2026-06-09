#pragma once

#include <string>
#include <string_view>

#include "Character.h"

namespace Character {

std::string BuildAnimationKey(CharacterState state, CharacterDirection direction);
AnimationClip *FindClip(AnimationController &controller, std::string_view name);
float RandomDelaySeconds(float minDelay, float maxDelay);
bool LoadWebPAnimation(std::string_view path, SpriteAnimation &animation, int &outFrames);

void RegisterCharacterAnimation(flecs::world &world);
void RegisterCharacterSprites(flecs::world &world);
void RegisterCharacterPhysics(flecs::world &world);

} // namespace Character

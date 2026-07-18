#pragma once

#include <string>
#include <string_view>

#include "Character.h"

#include "modules/Rendering.h"

namespace Character::Internal {

const char *DirectionSuffix(CharacterDirection direction);
const char *StatePrefix(CharacterState state);
std::string BuildAnimationKey(CharacterState state, CharacterDirection direction);
AnimationClip *FindClip(AnimationController &controller, std::string_view name);
float RandomDelaySeconds(float minDelay, float maxDelay);

class CharacterRenderable final : public Rendering::Renderable {
public:
  CharacterRenderable(flecs::entity entity);
  void Draw(const Rendering::Position &position) const override;

private:
  flecs::entity entity_;
};

void RegisterCharacterAnimation(flecs::world &world);
void RegisterCharacterSprites(flecs::world &world);
void RegisterCharacterPhysics(flecs::world &world);
void RegisterCharacterRendering(flecs::world &world);

} // namespace Character::Internal

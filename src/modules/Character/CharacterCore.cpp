#include <algorithm>

#include "raylib.h"

#include "CharacterInternal.h"

namespace Character::Internal {

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

std::string BuildCueKey(Presentation::PresentationCue cue, CharacterDirection direction) {
  const char *prefix = nullptr;
  switch (cue) {
  case Presentation::PresentationCue::Interact:
    prefix = "interact";
    break;
  case Presentation::PresentationCue::Hurt:
    prefix = "hurt";
    break;
  case Presentation::PresentationCue::Reset:
    return {};
  }

  std::string key{prefix};
  key.push_back('-');
  key.append(DirectionSuffix(direction));
  return key;
}

float RandomDelaySeconds(float minDelay, float maxDelay) {
  const float clampedMin = std::max(0.0f, minDelay);
  const float clampedMax = std::max(clampedMin, maxDelay);
  const int minMs = static_cast<int>(clampedMin * 1000.0f);
  const int maxMs = static_cast<int>(clampedMax * 1000.0f);
  if (minMs == maxMs) {
    return static_cast<float>(minMs) / 1000.0f;
  }

  return static_cast<float>(GetRandomValue(minMs, maxMs)) / 1000.0f;
}

} // namespace Character::Internal

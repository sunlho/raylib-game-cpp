#include <algorithm>
#include <cmath>
#include <utility>

#include "CharacterInternal.h"

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
} // namespace

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

} // namespace Character

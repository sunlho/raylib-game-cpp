#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "flecs.h"
#include "raylib.h"

namespace Character {

struct Phases {
  struct Update {};
};

enum class CharacterState : int {
  Idle = 0,
  Moving = 1,
  Attacking = 2,
  Hurt = 3,
  Dead = 4,
};

enum class CharacterDirection : int {
  Down = 0,
  Up = 1,
  Left = 2,
  Right = 3,
};

struct CharacterInfo {
  std::string name;
  CharacterState state = CharacterState::Idle;
  CharacterDirection direction = CharacterDirection::Down;
};

struct CharacterStats {
  float health = 100.0f;
  float maxHealth = 100.0f;
  float attack = 10.0f;
  float defense = 0.0f;
};

struct AnimationClip {
  std::string name;
  int frameCount = 1;
  float frameDuration = 0.1f;
  bool loop = true;
};

struct AnimationController {
  std::vector<AnimationClip> clips;
  int currentClip = -1;
  int currentFrame = 0;
  float elapsed = 0.0f;

  void AddAnimation(AnimationClip clip);
  bool PlayAnimation(std::string_view name, bool restart = false);
  const AnimationClip *GetCurrentAnimation() const;
};

struct IdleBehavior {
  float minDelay = 0.5f;
  float maxDelay = 1.2f;
  float timer = 0.0f;
  bool waiting = true;
  bool playing = false;
  bool wasMoving = false;
  bool initialized = false;
};

struct SpriteAnimation {
  Texture2D texture = {0};
  int frameCount = 0;
  int width = 0;
  int height = 0;
  int format = 0;
  int bytesPerFrame = 0;
  int lastFrame = -1;
  std::vector<unsigned char> pixels;
};

struct SpriteEntry {
  std::string name;
  std::string path;
  float frameDuration = 0.12f;
  bool loop = true;
  SpriteAnimation animation;
};

struct SpriteSet {
  std::vector<SpriteEntry> entries;
  float scale = 1.0f;
  Vector2 origin = {0.0f, 0.0f};
  bool useCenterOrigin = true;
  bool loaded = false;

  const SpriteEntry *FindEntry(std::string_view name) const;
  SpriteEntry *FindEntry(std::string_view name);
};

struct PlayerTag {};
struct NPCTag {};

Vector2 GetSpriteHalfExtents(const SpriteSet &spriteSet, const AnimationController &controller);

void TestChangeCharacterPhysicsShapeCenter(flecs::world &world);

struct module {
  module(flecs::world &world);
};

} // namespace Character

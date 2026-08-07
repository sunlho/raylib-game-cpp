#pragma once

#include <string>
#include <vector>

#include "flecs.h"

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

struct PlayerTag {};
struct NPCTag {};

namespace Presentation {

struct AppearanceId {
  std::string value;
};

struct AnimationIntent {
  std::string name;
  std::string path;
  float frameDuration = 0.12f;
  bool loop = true;
};

struct AppearanceGeometry {
  float scale = 1.0f;
  float originX = 0.0f;
  float originY = 0.0f;
  bool useCenterOrigin = true;
};

struct AppearanceIntent {
  std::vector<AnimationIntent> animations;
  std::string defaultAnimation;
  AppearanceGeometry geometry;
  float idleDelayMin = 0.5f;
  float idleDelayMax = 1.2f;
};

enum class PresentationCue {
  Reset,
  Interact,
  Hurt,
};

enum class ErrorCode {
  None,
  Closed,
  InvalidAppearanceId,
  DuplicateAppearance,
  InvalidIntent,
  AssetLoadFailed,
  UnknownAppearance,
  InvalidCharacter,
  MissingCharacterData,
  RenderableConflict,
};

struct Result {
  ErrorCode code = ErrorCode::None;
  std::string message;

  explicit operator bool() const { return code == ErrorCode::None; }
};

Result Register(flecs::world &world, AppearanceId id, AppearanceIntent intent);
Result Assign(flecs::entity character, const AppearanceId &id);
void Cue(flecs::entity character, PresentationCue cue);
void Shutdown(flecs::world &world);

} // namespace Presentation

struct module {
  module(flecs::world &world);
};

} // namespace Character

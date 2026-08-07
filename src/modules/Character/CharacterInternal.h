#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "raylib.h"

#include "Character.h"

#include "modules/Core/Transform.h"
#include "modules/Rendering.h"

namespace Character::Internal {

const char *DirectionSuffix(CharacterDirection direction);
const char *StatePrefix(CharacterState state);
std::string BuildAnimationKey(CharacterState state, CharacterDirection direction);
std::string BuildCueKey(Presentation::PresentationCue cue, CharacterDirection direction);
float RandomDelaySeconds(float minDelay, float maxDelay);

struct LoadedAnimation {
  std::vector<Texture2D> frames;
  int width = 0;
  int height = 0;
};

class PresentationBackend {
public:
  virtual ~PresentationBackend() = default;
  virtual bool LoadAnimation(std::string_view path, LoadedAnimation &animation, std::string &error) = 0;
  virtual void UnloadAnimation(LoadedAnimation &animation) noexcept = 0;
  virtual void DrawFrame(
      const LoadedAnimation &animation,
      int frame,
      Rectangle destination,
      Vector2 origin) const = 0;
};

struct AnimationResource {
  Presentation::AnimationIntent intent;
  LoadedAnimation loaded;
};

struct AppearanceResource {
  Presentation::AppearanceIntent intent;
  std::vector<AnimationResource> animations;
  std::unordered_map<std::string, std::size_t> animationByName;
  std::size_t defaultAnimation = 0;
};

struct PlaybackState {
  std::string appearanceId;
  std::size_t animation = 0;
  int frame = 0;
  float elapsed = 0.0f;
  CharacterState lastState = CharacterState::Idle;
  CharacterDirection lastDirection = CharacterDirection::Down;
  bool cueActive = false;
  bool idleInitialized = false;
  bool idleWaiting = true;
  bool idlePlaying = false;
  float idleTimer = 0.0f;
};

struct CatalogState {
  std::shared_ptr<PresentationBackend> backend;
  std::unordered_map<std::string, AppearanceResource> appearances;
  bool closed = false;

  ~CatalogState();
};

struct PresentationCatalog {
  std::shared_ptr<CatalogState> state;
};

struct PresentationOwned {};

class CharacterRenderable final : public Rendering::Renderable {
public:
  CharacterRenderable(flecs::entity entity, std::weak_ptr<CatalogState> catalog);
  void Draw(const Core::Position &position) const override;

private:
  flecs::entity entity_;
  std::weak_ptr<CatalogState> catalog_;
};

std::shared_ptr<PresentationBackend> CreateRaylibPresentationBackend();
void EnsureCharacterPresentation(flecs::world &world);
void RegisterCharacterAnimation(flecs::world &world);
void RegisterCharacterPhysics(flecs::world &world);

const AppearanceResource *FindAppearance(const CatalogState &catalog, std::string_view id);
const AnimationResource *CurrentAnimation(const CatalogState &catalog, const PlaybackState &playback);

// Private test seam. Must be installed before the first appearance is registered.
bool InstallPresentationBackendForTesting(
    flecs::world &world,
    std::shared_ptr<PresentationBackend> backend);

} // namespace Character::Internal

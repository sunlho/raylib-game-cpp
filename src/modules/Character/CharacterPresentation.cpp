#include "CharacterInternal.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace Character {
namespace Internal {
namespace {

Presentation::Result Success() { return {}; }

Presentation::Result Failure(Presentation::ErrorCode code, std::string message) {
  return Presentation::Result{code, std::move(message)};
}

void ReleaseAppearance(CatalogState &catalog, AppearanceResource &appearance) noexcept {
  if (!catalog.backend) {
    return;
  }
  for (auto &animation : appearance.animations) {
    catalog.backend->UnloadAnimation(animation.loaded);
  }
}

void ReleaseAll(CatalogState &catalog) noexcept {
  for (auto &[id, appearance] : catalog.appearances) {
    (void)id;
    ReleaseAppearance(catalog, appearance);
  }
  catalog.appearances.clear();
}

const AnimationResource *AnimationAt(const AppearanceResource &appearance, std::size_t index) {
  if (index >= appearance.animations.size()) {
    return nullptr;
  }
  return &appearance.animations[index];
}

std::size_t ResolveBaseAnimation(
    const AppearanceResource &appearance,
    CharacterState state,
    CharacterDirection direction) {
  const auto exact = appearance.animationByName.find(BuildAnimationKey(state, direction));
  if (exact != appearance.animationByName.end()) {
    return exact->second;
  }

  const auto idle = appearance.animationByName.find(BuildAnimationKey(CharacterState::Idle, direction));
  return idle == appearance.animationByName.end() ? appearance.defaultAnimation : idle->second;
}

void StartAnimation(PlaybackState &playback, std::size_t animation) {
  playback.animation = animation;
  playback.frame = 0;
  playback.elapsed = 0.0f;
}

void SyncSortY(
    const AppearanceResource &appearance,
    const PlaybackState &playback,
    const Core::Position &position,
    Rendering::RenderComponent &renderComponent) {
  if (const auto *animation = AnimationAt(appearance, playback.animation)) {
    const float halfHeight = static_cast<float>(animation->loaded.height) * appearance.intent.geometry.scale * 0.5f;
    renderComponent.sortY = static_cast<int>(position.value.y + halfHeight);
  }
}

void ResetToBase(
    PlaybackState &playback,
    const AppearanceResource &appearance,
    const CharacterInfo &info,
    bool restart) {
  const std::size_t desired = ResolveBaseAnimation(appearance, info.state, info.direction);
  if (restart || playback.animation != desired) {
    StartAnimation(playback, desired);
  }
  playback.cueActive = false;
  playback.idleInitialized = false;
  playback.idleWaiting = true;
  playback.idlePlaying = false;
  playback.idleTimer = 0.0f;
  playback.lastState = info.state;
  playback.lastDirection = info.direction;
}

bool AdvanceAnimation(
    PlaybackState &playback,
    const AnimationResource &animation,
    float deltaTime,
    bool loop) {
  const int frameCount = static_cast<int>(animation.loaded.frames.size());
  if (frameCount <= 0) {
    return true;
  }

  const float duration = animation.intent.frameDuration;
  playback.elapsed += std::max(0.0f, deltaTime);
  const int advanceFrames = static_cast<int>(playback.elapsed / duration);
  if (advanceFrames <= 0) {
    return false;
  }

  playback.elapsed -= static_cast<float>(advanceFrames) * duration;
  if (loop) {
    playback.frame = (playback.frame + advanceFrames) % frameCount;
    return false;
  }

  const int nextFrame = playback.frame + advanceFrames;
  playback.frame = std::min(frameCount - 1, nextFrame);
  if (nextFrame >= frameCount) {
    playback.elapsed = 0.0f;
    return true;
  }
  return false;
}

void UpdateIdle(
    PlaybackState &playback,
    const AppearanceResource &appearance,
    const AnimationResource &animation,
    float deltaTime,
    bool stateChanged) {
  if (!playback.idleInitialized || stateChanged) {
    playback.idleInitialized = true;
    playback.idleWaiting = true;
    playback.idlePlaying = false;
    playback.idleTimer = RandomDelaySeconds(appearance.intent.idleDelayMin, appearance.intent.idleDelayMax);
    playback.frame = 0;
    playback.elapsed = 0.0f;
  }

  if (playback.idleWaiting) {
    playback.idleTimer -= std::max(0.0f, deltaTime);
    if (playback.idleTimer <= 0.0f) {
      playback.idleWaiting = false;
      playback.idlePlaying = true;
      playback.frame = 0;
      playback.elapsed = 0.0f;
    }
    return;
  }

  if (playback.idlePlaying && AdvanceAnimation(playback, animation, deltaTime, false)) {
    playback.idlePlaying = false;
    playback.idleWaiting = true;
    playback.idleTimer = RandomDelaySeconds(appearance.intent.idleDelayMin, appearance.intent.idleDelayMax);
    playback.frame = 0;
    playback.elapsed = 0.0f;
  }
}

void UpdatePresentation(
    CatalogState &catalog,
    const CharacterInfo &info,
    const Core::Position &position,
    PlaybackState &playback,
    Rendering::RenderComponent &renderComponent,
    float deltaTime) {
  const auto *appearance = FindAppearance(catalog, playback.appearanceId);
  if (!appearance) {
    return;
  }

  const bool stateChanged = info.state != playback.lastState || info.direction != playback.lastDirection;
  if (info.state == CharacterState::Dead && playback.cueActive) {
    ResetToBase(playback, *appearance, info, true);
  } else if (playback.cueActive) {
    const auto *animation = AnimationAt(*appearance, playback.animation);
    if (!animation || AdvanceAnimation(playback, *animation, deltaTime, false)) {
      ResetToBase(playback, *appearance, info, true);
    }
  } else {
    const std::size_t desired = ResolveBaseAnimation(*appearance, info.state, info.direction);
    if (playback.animation != desired) {
      StartAnimation(playback, desired);
    }

    const auto *animation = AnimationAt(*appearance, playback.animation);
    if (animation) {
      if (info.state == CharacterState::Idle) {
        UpdateIdle(playback, *appearance, *animation, deltaTime, stateChanged);
      } else {
        playback.idleInitialized = false;
        playback.idlePlaying = false;
        playback.idleWaiting = true;
        AdvanceAnimation(playback, *animation, deltaTime, animation->intent.loop);
      }
    }
  }

  playback.lastState = info.state;
  playback.lastDirection = info.direction;

  SyncSortY(*appearance, playback, position, renderComponent);
}

Presentation::Result ValidateIntent(
    const Presentation::AppearanceId &id,
    const Presentation::AppearanceIntent &intent) {
  if (id.value.empty()) {
    return Failure(Presentation::ErrorCode::InvalidAppearanceId, "AppearanceId must not be empty");
  }
  if (intent.animations.empty() || intent.defaultAnimation.empty()) {
    return Failure(Presentation::ErrorCode::InvalidIntent, "Appearance intent requires animations and a default animation");
  }
  if (intent.geometry.scale <= 0.0f || intent.idleDelayMin < 0.0f || intent.idleDelayMax < intent.idleDelayMin) {
    return Failure(Presentation::ErrorCode::InvalidIntent, "Appearance geometry or idle timing is invalid");
  }

  std::unordered_set<std::string> names;
  bool hasDefault = false;
  for (const auto &animation : intent.animations) {
    if (animation.name.empty() || animation.path.empty() || animation.frameDuration <= 0.0f ||
        !names.insert(animation.name).second) {
      return Failure(Presentation::ErrorCode::InvalidIntent, "Animation names and paths must be non-empty, unique, and have a positive duration");
    }
    hasDefault = hasDefault || animation.name == intent.defaultAnimation;
  }
  if (!hasDefault) {
    return Failure(Presentation::ErrorCode::InvalidIntent, "Default animation is not defined");
  }
  return Success();
}

} // namespace

CatalogState::~CatalogState() { ReleaseAll(*this); }

const AppearanceResource *FindAppearance(const CatalogState &catalog, std::string_view id) {
  const auto found = catalog.appearances.find(std::string(id));
  return found == catalog.appearances.end() ? nullptr : &found->second;
}

const AnimationResource *CurrentAnimation(const CatalogState &catalog, const PlaybackState &playback) {
  const auto *appearance = FindAppearance(catalog, playback.appearanceId);
  return appearance ? AnimationAt(*appearance, playback.animation) : nullptr;
}

void EnsureCharacterPresentation(flecs::world &world) {
  if (world.has<PresentationCatalog>()) {
    return;
  }

  auto state = std::make_shared<CatalogState>();
  state->backend = CreateRaylibPresentationBackend();
  world.component<PlaybackState>();
  world.component<PresentationOwned>();
  world.component<PresentationCatalog>().add(flecs::Singleton);
  world.set<PresentationCatalog>({state});

  world.system<const CharacterInfo, const Core::Position, PlaybackState, Rendering::RenderComponent>("Update Character Presentation")
      .kind<Character::Phases::Update>()
      .each([state](flecs::iter &it, std::size_t, const CharacterInfo &info, const Core::Position &position, PlaybackState &playback, Rendering::RenderComponent &renderComponent) {
        if (!state->closed) {
          UpdatePresentation(*state, info, position, playback, renderComponent, it.delta_time());
        }
      });
}

bool InstallPresentationBackendForTesting(
    flecs::world &world,
    std::shared_ptr<PresentationBackend> backend) {
  EnsureCharacterPresentation(world);
  auto state = world.get<PresentationCatalog>().state;
  if (!backend || state->closed || !state->appearances.empty()) {
    return false;
  }
  state->backend = std::move(backend);
  return true;
}

} // namespace Internal

namespace Presentation {

Result Register(flecs::world &world, AppearanceId id, AppearanceIntent intent) {
  Internal::EnsureCharacterPresentation(world);
  auto state = world.get<Internal::PresentationCatalog>().state;
  if (state->closed) {
    return Internal::Failure(ErrorCode::Closed, "Character presentation is shut down");
  }
  if (state->appearances.contains(id.value)) {
    return Internal::Failure(ErrorCode::DuplicateAppearance, "AppearanceId is already registered: " + id.value);
  }
  if (auto validation = Internal::ValidateIntent(id, intent); !validation) {
    return validation;
  }

  Internal::AppearanceResource candidate;
  candidate.intent = std::move(intent);
  candidate.animations.reserve(candidate.intent.animations.size());
  for (const auto &animationIntent : candidate.intent.animations) {
    Internal::AnimationResource animation;
    animation.intent = animationIntent;
    std::string error;
    if (!state->backend->LoadAnimation(animation.intent.path, animation.loaded, error)) {
      Internal::ReleaseAppearance(*state, candidate);
      return Internal::Failure(ErrorCode::AssetLoadFailed, std::move(error));
    }

    const std::size_t index = candidate.animations.size();
    candidate.animationByName.emplace(animation.intent.name, index);
    candidate.animations.push_back(std::move(animation));
  }
  candidate.defaultAnimation = candidate.animationByName.at(candidate.intent.defaultAnimation);
  state->appearances.emplace(std::move(id.value), std::move(candidate));
  return Internal::Success();
}

Result Assign(flecs::entity character, const AppearanceId &id) {
  if (!character.is_valid() || !character.is_alive()) {
    return Internal::Failure(ErrorCode::InvalidCharacter, "Character entity is not alive");
  }

  flecs::world world = character.world();
  Internal::EnsureCharacterPresentation(world);
  auto state = world.get<Internal::PresentationCatalog>().state;
  if (state->closed) {
    return Internal::Failure(ErrorCode::Closed, "Character presentation is shut down");
  }

  const auto *appearance = Internal::FindAppearance(*state, id.value);
  if (!appearance) {
    return Internal::Failure(ErrorCode::UnknownAppearance, "Unknown AppearanceId: " + id.value);
  }
  const auto *info = character.try_get<CharacterInfo>();
  const auto *position = character.try_get<Core::Position>();
  if (!info || !position) {
    return Internal::Failure(ErrorCode::MissingCharacterData, "CharacterInfo and Position are required before Assign");
  }
  if (character.has<Rendering::RenderComponent>() && !character.has<Internal::PresentationOwned>()) {
    return Internal::Failure(ErrorCode::RenderableConflict, "Character already has a renderable not owned by Character::Presentation");
  }

  Internal::PlaybackState playback;
  playback.appearanceId = id.value;
  Internal::ResetToBase(playback, *appearance, *info, true);

  Rendering::RenderComponent renderComponent;
  if (const auto *existing = character.try_get<Rendering::RenderComponent>()) {
    renderComponent.floor = existing->floor;
    renderComponent.visible = existing->visible;
  }
  renderComponent.object = std::make_shared<Internal::CharacterRenderable>(character, state);
  Internal::SyncSortY(*appearance, playback, *position, renderComponent);

  character.set<Internal::PlaybackState>(std::move(playback));
  character.set<Rendering::RenderComponent>(std::move(renderComponent));
  character.add<Rendering::SortableTag>();
  character.add<Internal::PresentationOwned>();
  return Internal::Success();
}

void Cue(flecs::entity character, PresentationCue cue) {
  if (!character.is_valid() || !character.is_alive()) {
    return;
  }

  auto *playback = character.try_get_mut<Internal::PlaybackState>();
  const auto *info = character.try_get<CharacterInfo>();
  if (!playback || !info) {
    return;
  }

  auto state = character.world().get<Internal::PresentationCatalog>().state;
  if (!state || state->closed) {
    return;
  }
  const auto *appearance = Internal::FindAppearance(*state, playback->appearanceId);
  if (!appearance) {
    return;
  }

  if (cue == PresentationCue::Reset) {
    Internal::ResetToBase(*playback, *appearance, *info, true);
    if (const auto *position = character.try_get<Core::Position>()) {
      if (auto *render = character.try_get_mut<Rendering::RenderComponent>()) {
        Internal::SyncSortY(*appearance, *playback, *position, *render);
      }
    }
    return;
  }
  if (info->state == CharacterState::Dead) {
    return;
  }

  const std::string key = Internal::BuildCueKey(cue, info->direction);
  const auto found = appearance->animationByName.find(key);
  if (found == appearance->animationByName.end()) {
    return;
  }
  Internal::StartAnimation(*playback, found->second);
  playback->cueActive = true;
  playback->idleInitialized = false;
  if (const auto *position = character.try_get<Core::Position>()) {
    if (auto *render = character.try_get_mut<Rendering::RenderComponent>()) {
      Internal::SyncSortY(*appearance, *playback, *position, *render);
    }
  }
}

void Shutdown(flecs::world &world) {
  if (!world.has<Internal::PresentationCatalog>()) {
    return;
  }

  auto state = world.get<Internal::PresentationCatalog>().state;
  if (!state || state->closed) {
    return;
  }

  std::vector<ecs_entity_t> entities;
  world.each([&entities](flecs::entity entity, const Internal::PlaybackState &) {
    entities.push_back(entity.id());
  });
  for (const ecs_entity_t id : entities) {
    if (!ecs_is_alive(world.c_ptr(), id)) {
      continue;
    }
    flecs::entity entity(world.c_ptr(), id);
    entity.remove<Internal::PlaybackState>();
    if (entity.has<Internal::PresentationOwned>()) {
      entity.remove<Rendering::RenderComponent>();
      entity.remove<Rendering::SortableTag>();
      entity.remove<Internal::PresentationOwned>();
    }
  }

  Internal::ReleaseAll(*state);
  state->closed = true;
}

} // namespace Presentation
} // namespace Character

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "modules/Character/CharacterInternal.h"

namespace {

class FakePresentationBackend final : public Character::Internal::PresentationBackend {
public:
  bool LoadAnimation(
      std::string_view path,
      Character::Internal::LoadedAnimation &animation,
      std::string &error) override {
    ++loadCalls;
    if (path == failingPath) {
      error = "intentional fake upload failure";
      return false;
    }

    animation.width = 16;
    animation.height = 20;
    pathFirstFrame[std::string(path)] = nextTextureId;
    for (int frame = 0; frame < 3; ++frame) {
      Texture2D texture = {};
      texture.id = nextTextureId++;
      texture.width = animation.width;
      texture.height = animation.height;
      animation.frames.push_back(texture);
    }
    return true;
  }

  void UnloadAnimation(Character::Internal::LoadedAnimation &animation) noexcept override {
    ++unloadCalls;
    animation.frames.clear();
  }

  void DrawFrame(
      const Character::Internal::LoadedAnimation &animation,
      int frame,
      Rectangle,
      Vector2) const override {
    drawnTextureIds.push_back(animation.frames.at(static_cast<std::size_t>(frame)).id);
  }

  int loadCalls = 0;
  int unloadCalls = 0;
  std::string failingPath;
  std::unordered_map<std::string, unsigned int> pathFirstFrame;
  mutable std::vector<unsigned int> drawnTextureIds;

private:
  unsigned int nextTextureId = 1;
};

int failures = 0;

void Check(bool condition, std::string_view message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

Character::Presentation::AppearanceIntent MakeIntent() {
  Character::Presentation::AppearanceIntent intent;
  intent.animations = {
      {"idle-S", "idle-s", 0.1f},
      {"idle-N", "idle-n", 0.1f},
      {"walk-S", "walk-s", 0.1f},
      {"interact-S", "interact-s", 0.1f, false},
      {"hurt-S", "hurt-s", 0.1f, false},
  };
  intent.defaultAnimation = "idle-S";
  intent.geometry.scale = 2.0f;
  intent.idleDelayMin = 100.0f;
  intent.idleDelayMax = 100.0f;
  return intent;
}

flecs::entity MakeCharacter(
    flecs::world &world,
    std::string_view name,
    Character::CharacterState state,
    Character::CharacterDirection direction) {
  return world.entity(std::string(name).c_str())
      .set<Character::CharacterInfo>({std::string(name), state, direction})
      .set<Core::Position>({Vector2{10.0f, 100.0f}});
}

void Draw(flecs::entity entity) {
  const auto *render = entity.try_get<Rendering::RenderComponent>();
  const auto *position = entity.try_get<Core::Position>();
  Check(render && render->object && position, "assigned character has a drawable render component");
  if (render && render->object && position) {
    render->object->Draw(*position);
  }
}

void RunPresentation(flecs::world &world, float deltaTime) {
  const auto system = world.lookup("Update Character Presentation");
  Check(system.is_alive(), "presentation update system exists");
  if (system.is_alive()) {
    ecs_run(world.c_ptr(), system.id(), deltaTime, nullptr);
  }
}

void TestAtomicRegistration() {
  flecs::world world;
  auto backend = std::make_shared<FakePresentationBackend>();
  Check(Character::Internal::InstallPresentationBackendForTesting(world, backend), "fake backend installs");

  auto intent = MakeIntent();
  backend->failingPath = "idle-n";
  const auto failed = Character::Presentation::Register(world, {"player"}, intent);
  Check(!failed && failed.code == Character::Presentation::ErrorCode::AssetLoadFailed, "upload failure is reported");
  Check(backend->unloadCalls == 1, "successful uploads are rolled back after a later failure");

  backend->failingPath.clear();
  const auto retried = Character::Presentation::Register(world, {"player"}, intent);
  Check(static_cast<bool>(retried), "failed registration leaves the id available for retry");

  Character::Presentation::Shutdown(world);
  Check(backend->unloadCalls == 1 + static_cast<int>(intent.animations.size()), "shutdown releases every committed animation once");
}

void TestSharedResourcesPlaybackFallbackAndShutdown() {
  flecs::world world;
  auto backend = std::make_shared<FakePresentationBackend>();
  Check(Character::Internal::InstallPresentationBackendForTesting(world, backend), "fake backend installs for lifecycle test");

  const auto intent = MakeIntent();
  Check(static_cast<bool>(Character::Presentation::Register(world, {"player"}, intent)), "appearance registers");
  const int loadsAfterRegistration = backend->loadCalls;

  auto moving = MakeCharacter(world, "moving", Character::CharacterState::Moving, Character::CharacterDirection::Down);
  auto idle = MakeCharacter(world, "idle", Character::CharacterState::Idle, Character::CharacterDirection::Down);
  auto sparse = MakeCharacter(world, "sparse", Character::CharacterState::Moving, Character::CharacterDirection::Up);
  Check(static_cast<bool>(Character::Presentation::Assign(moving, {"player"})), "moving character is assigned");
  Check(static_cast<bool>(Character::Presentation::Assign(idle, {"player"})), "idle character is assigned");
  Check(static_cast<bool>(Character::Presentation::Assign(sparse, {"player"})), "sparse character is assigned");
  Check(backend->loadCalls == loadsAfterRegistration, "assigning multiple characters performs no GPU loads");

  Draw(moving);
  Draw(idle);
  Draw(sparse);
  Check(backend->drawnTextureIds[0] == backend->pathFirstFrame["walk-s"], "exact state and direction animation wins");
  Check(backend->drawnTextureIds[1] == backend->pathFirstFrame["idle-s"], "idle character uses directional idle");
  Check(backend->drawnTextureIds[2] == backend->pathFirstFrame["idle-n"], "missing state animation falls back to directional idle");

  const auto *initialRender = moving.try_get<Rendering::RenderComponent>();
  const auto originalObject = initialRender ? initialRender->object : nullptr;
  const auto unknown = Character::Presentation::Assign(moving, {"missing"});
  Check(!unknown && unknown.code == Character::Presentation::ErrorCode::UnknownAppearance, "unknown appearance is rejected");
  Check(moving.try_get<Rendering::RenderComponent>()->object == originalObject, "failed assign leaves the previous presentation intact");

  Character::Presentation::Cue(moving, Character::Presentation::PresentationCue::Interact);
  Character::Presentation::Cue(moving, Character::Presentation::PresentationCue::Hurt);
  RunPresentation(world, 0.11f);
  backend->drawnTextureIds.clear();
  Draw(moving);
  Draw(idle);
  Check(backend->drawnTextureIds[0] == backend->pathFirstFrame["hurt-s"] + 1, "latest cue replaces the previous cue and advances independently");
  Check(backend->drawnTextureIds[1] == backend->pathFirstFrame["idle-s"], "another instance keeps independent playback state");
  Check(moving.try_get<Rendering::RenderComponent>()->sortY == 120, "sort Y includes the scaled sprite half-height");

  moving.get_mut<Core::Position>().value.y = 200.0f;
  Character::Presentation::Cue(moving, Character::Presentation::PresentationCue::Reset);
  Check(moving.try_get<Rendering::RenderComponent>()->sortY == 220, "reset synchronizes sort Y immediately after relocation");

  Character::Presentation::Cue(moving, Character::Presentation::PresentationCue::Interact);
  moving.get_mut<Character::CharacterInfo>().state = Character::CharacterState::Dead;
  RunPresentation(world, 0.01f);
  backend->drawnTextureIds.clear();
  Draw(moving);
  Check(backend->drawnTextureIds[0] == backend->pathFirstFrame["idle-s"], "dead state cancels an active cue and uses fallback");

  Character::Presentation::Shutdown(world);
  Check(!moving.has<Rendering::RenderComponent>() && !idle.has<Rendering::RenderComponent>(), "shutdown detaches owned renderables");
  Check(backend->unloadCalls == static_cast<int>(intent.animations.size()), "shutdown releases shared resources once, not once per entity");
  Character::Presentation::Shutdown(world);
  Check(backend->unloadCalls == static_cast<int>(intent.animations.size()), "shutdown is idempotent");

  const auto closed = Character::Presentation::Register(world, {"later"}, intent);
  Check(!closed && closed.code == Character::Presentation::ErrorCode::Closed, "registration stays closed after shutdown");
}

} // namespace

int main() {
  TestAtomicRegistration();
  TestSharedResourcesPlaybackFallbackAndShutdown();
  if (failures == 0) {
    std::cout << "Character presentation tests passed\n";
  }
  return failures == 0 ? 0 : 1;
}

#include <iostream>
#include <utility>

#include "flecs.h"
#include "raylib.h"

#include "modules/Camera.h"
#include "modules/Character/Character.h"
#include "modules/Console/Console.h"
#include "modules/Console/Register.h"
#include "modules/Map/Map.h"
#include "modules/Movement.h"
#include "modules/Physics.h"
#include "modules/Reflection.h"
#include "modules/Rendering.h"
#include "modules/Runtime/Runtime.h"
#include "modules/Stairs/Stairs.h"

// constexpr int SCREEN_WIDTH = 2560;
// constexpr int SCREEN_HEIGHT = 1440;
// constexpr int SCREEN_WIDTH = 1920;
// constexpr int SCREEN_HEIGHT = 1080;
// constexpr int SCREEN_WIDTH = 1600;
// constexpr int SCREEN_HEIGHT = 900;
constexpr int SCREEN_WIDTH = 1280;
constexpr int SCREEN_HEIGHT = 720;
constexpr int BASE_WIDTH = 640;
constexpr int BASE_HEIGHT = 360;

static bool isDebugDrawEnabled = false;

static ecs_entity_t CreatePlayer(flecs::world &world) {
  Character::SpriteSet playerSprites;
  playerSprites.entries = {
      {"idle-N", "player/A_idle-N.webp"},
      {"idle-S", "player/A_idle-S.webp"},
      {"idle-E", "player/A_idle-E.webp"},
      {"idle-W", "player/A_idle-W.webp"},
      {"walk-N", "player/A_walk-N.webp", 0.09f},
      {"walk-S", "player/A_walk-S.webp", 0.09f},
      {"walk-E", "player/A_walk-E.webp", 0.09f},
      {"walk-W", "player/A_walk-W.webp", 0.09f},
      {"interact-N", "player/A_interact-N.webp", 0.08f, false},
      {"interact-S", "player/A_interact-S.webp", 0.08f, false},
      {"interact-E", "player/A_interact-E.webp", 0.08f, false},
      {"interact-W", "player/A_interact-W.webp", 0.08f, false},
  };
  playerSprites.scale = 1.0f;

  const Vector2 playerStart = {700.0f, 700.0f};
  auto player = world.entity("Player");
  player.add<Character::PlayerTag>()
      .add<Movement::PlayerControlled>()
      .add<Movement::CameraFollowTag>()
      .set<Character::CharacterInfo>({"Player", Character::CharacterState::Idle, Character::CharacterDirection::Down})
      .set<Character::CharacterStats>({100.0f, 100.0f, 10.0f, 2.0f})
      .set<Character::AnimationController>({})
      .set<Character::IdleBehavior>({})
      .set<Character::SpriteSet>(std::move(playerSprites))
      .set<Rendering::Position>({playerStart})
      .set<Stairs::FloorState>({2.5f, 2.5f})
      .set<Movement::Velocity>({Vector2{0.0f, 0.0f}})
      .set<Movement::MoveSpeed>({100.0f})
      .set<Movement::RunSettings>({1.6f, 0.2f})
      .set<Movement::RunState>({});

  auto &mainCamera = world.get_mut<GameCamera::MainCamera>();
  const auto &renderTargetSize = world.get<Rendering::RenderTargetSize>();
  mainCamera.value.offset = Vector2{renderTargetSize.dimension.x * 0.5f, renderTargetSize.dimension.y * 0.5f};
  mainCamera.value.target = playerStart;
  return player.id();
}

int main() {

  InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "raylib game cpp");
  SetExitKey(KEY_NULL);
  SetTargetFPS(60);

  flecs::world world;
  world.set<flecs::Rest>({});
  world.import<flecs::stats>();
  ecs_entity_t dequeue_rest = ecs_lookup(world, "flecs.rest.DequeueRest");

  world.import<Rendering::module>();
  world.import<GameConsole::module>();
  world.import<GameCamera::module>();
  world.import<Physics::module>();
  world.import<Movement::module>();
  world.import<Character::module>();
  world.import<Stairs::module>();
  world.import<MapManager::module>();

  GameConsole::RegisterCommands(world, {&isDebugDrawEnabled});
  Runtime::Coordinator runtime(world, dequeue_rest);

  auto &renderTargetSize = world.get_mut<Rendering::RenderTargetSize>();
  renderTargetSize.dimension = Vector2{static_cast<float>(BASE_WIDTH), static_cast<float>(BASE_HEIGHT)};

  ecs_progress(world, 0);
  Rendering::RunLoadingSequence(
      world,
      {
          {
              0.7f,
              "Loading map...",
              [](flecs::world &loadingWorld) {
                MapManager::SetMapPath(loadingWorld, "Map.tmx");
              },
          },
          {
              1.0f,
              "Loading character...",
              [&runtime](flecs::world &loadingWorld) {
                const ecs_entity_t player = CreatePlayer(loadingWorld);
                runtime.PrepareLoadedWorld(player);
              },
          },
      },
      "Preparing resources...");

  while (!WindowShouldClose()) {
    const Runtime::FrameReport frame = runtime.AdvanceFrame({
        .frameTime = GetFrameTime(),
        .exitPressed = IsKeyPressed(KEY_ESCAPE),
        .pauseSimulation = false,
        .debugDraw = isDebugDrawEnabled,
    });
    if (frame.exitRequested) {
      break;
    }
  }

  world.quit();
  GameConsole::Shutdown();
  CloseWindow();

  return 0;
}

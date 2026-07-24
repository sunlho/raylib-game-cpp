#include <algorithm>
#include <iostream>
#include <utility>

#include "flecs.h"
#include "raylib.h"

#include "modules/Camera.h"
#include "modules/Character/Character.h"
#include "modules/Console/Console.h"
#include "modules/Console/Register.h"
#include "modules/Debug/DebugDraw.h"
#include "modules/Debug/FrameStepper.h"
#include "modules/Debug/Screenshot.h"
#include "modules/Map/Map.h"
#include "modules/Movement.h"
#include "modules/Physics.h"
#include "modules/Reflection.h"
#include "modules/Rendering.h"
#include "modules/Simulation.h"
#include "modules/Stairs/Stairs.h"
#include "modules/Utils.h"

// constexpr int SCREEN_WIDTH = 2560;
// constexpr int SCREEN_HEIGHT = 1440;
// constexpr int SCREEN_WIDTH = 1920;
// constexpr int SCREEN_HEIGHT = 1080;
// constexpr int SCREEN_WIDTH = 1600;
// constexpr int SCREEN_HEIGHT = 900;
// constexpr int SCREEN_WIDTH = 1280;
// constexpr int SCREEN_HEIGHT = 720;
constexpr int SCREEN_WIDTH = 640;
constexpr int SCREEN_HEIGHT = 360;
constexpr float CAMERA_ZOOM = 2.0f;

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
      .set<Rendering::PreviousPosition>({playerStart})
      .set<Rendering::RenderPosition>({playerStart, playerStart})
      .set<Stairs::FloorState>({2.5f, 2.5f})
      .set<Movement::Velocity>({Vector2{0.0f, 0.0f}})
      .set<Movement::MoveSpeed>({100.0f})
      .set<Movement::RunSettings>({1.6f, 0.2f})
      .set<Movement::RunState>({});

  auto &mainCamera = world.get_mut<GameCamera::MainCamera>();
  const auto &renderTargetSize = world.get<Rendering::RenderTargetSize>();
  mainCamera.value.offset = Vector2{renderTargetSize.dimension.x * 0.5f, renderTargetSize.dimension.y * 0.5f};
  GameCamera::SnapTo(world, playerStart);
  return player.id();
}

static void UpdateLoadingRevealCenter(flecs::world &world) {
  const auto player = world.lookup("Player");
  if (!player.is_valid() || !player.has<Rendering::Position>()) {
    return;
  }

  const auto &position = player.get<Rendering::Position>();
  const auto &mainCamera = world.get<GameCamera::MainCamera>();
  Rendering::SetLoadingRevealCenter(world, GetWorldToScreen2D(position.value, mainCamera.value));
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

  const auto background = buildPipeline<Rendering::Phases::Background>(world);
  const auto worldDraw = buildPipeline<Rendering::Phases::World>(world);
  const auto sortedWorldDraw = buildPipeline<Rendering::Phases::SortedWorld>(world);

  const auto moveUpdate = buildPipeline<Movement::Phases::Update>(world);
  const auto characterUpdate = buildPipeline<Character::Phases::Update>(world);

  const auto prePhysics = buildPipeline<Simulation::PrePhysics>(world);
  const auto physicsStep = buildPipeline<Simulation::PhysicsStep>(world);
  const auto postPhysics = buildPipeline<Simulation::PostPhysics>(world);
  const auto fixedUpdate = buildPipeline<Simulation::FixedUpdate>(world);

  auto &renderTargetSize = world.get_mut<Rendering::RenderTargetSize>();
  renderTargetSize.dimension = world.get<Rendering::RenderSettings>().sceneTargetSize;
  world.get_mut<GameCamera::MainCamera>().value.zoom = CAMERA_ZOOM;

  float fixedTimeStep = 1.0f / 60.0f;
  float accumulator = 0.0f;
  Debug::FrameStepper frameStepper;
  Debug::ScreenshotCapture screenshotCapture;
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
              [characterUpdate, &accumulator](flecs::world &loadingWorld) {
                CreatePlayer(loadingWorld);
                ecs_run_pipeline(loadingWorld, characterUpdate, 0.0f);
                accumulator = 0.0f;
              },
          },
      },
      "Preparing resources...");

  while (!WindowShouldClose()) {
    const bool consoleWasOpen = GameConsole::IsOpen(world);
    if (!Rendering::IsLoadingScreenVisible(world)) {
      GameConsole::Update(world);
    }
    const bool consoleIsOpen = GameConsole::IsOpen(world);

    if (!consoleWasOpen && IsKeyPressed(KEY_ESCAPE)) {
      break;
    }

    const auto frameTime = GetFrameTime();
    screenshotCapture.Update(frameTime);
    Rendering::UpdateLoadingScreen(world, frameTime);

    const bool loadingScreenVisible = Rendering::IsLoadingScreenVisible(world);
    frameStepper.UpdateControls(!loadingScreenVisible && !consoleIsOpen);
    if (frameStepper.DidRequestScreenshotStep()) {
      screenshotCapture.RequestCapture();
    }
    if (frameStepper.DidPauseStateChange()) {
      // Discard partial/catch-up time at pause boundaries so one requested
      // frame always maps to exactly one fixed simulation step.
      accumulator = 0.0f;
    }

    ecs_frame_begin(world, frameTime);

    if (!loadingScreenVisible && frameStepper.ShouldAdvanceSimulation()) {
      const float simulationFrameTime = frameStepper.IsStepRequested() ? fixedTimeStep : frameTime;
      ecs_run_pipeline(world, moveUpdate, simulationFrameTime);
    }

    if (!loadingScreenVisible && frameStepper.ShouldAdvanceSimulation()) {
      const float simulationFrameTime = frameStepper.IsStepRequested() ? fixedTimeStep : frameTime;
      accumulator += simulationFrameTime;
      while (accumulator >= fixedTimeStep) {
        Rendering::CapturePreviousPositions(world);
        ecs_run_pipeline(world, prePhysics, fixedTimeStep);
        ecs_run_pipeline(world, physicsStep, fixedTimeStep);
        ecs_run_pipeline(world, postPhysics, fixedTimeStep);
        ecs_run_pipeline(world, fixedUpdate, fixedTimeStep);
        ecs_run_pipeline(world, characterUpdate, fixedTimeStep);
        frameStepper.RecordFixedStep();
        accumulator -= fixedTimeStep;
      }
    }

    const float renderAlpha = frameStepper.IsStepRequested() ? 1.0f : std::clamp(accumulator / fixedTimeStep, 0.0f, 1.0f);
    Rendering::InterpolateRenderPositions(world, renderAlpha);
    if (!loadingScreenVisible) Movement::UpdateCamera(world, frameTime);
    const auto &renderCamera = world.get<GameCamera::MainCamera>();
    Rendering::QuantizeRenderPositions(world, renderCamera.renderTarget, renderCamera.pixelsPerWorldUnit);

    Rendering::BeginFrame(world);
    GameCamera::Begin2D(world);
    ecs_run_pipeline(world, background, frameTime);
    ecs_run_pipeline(world, worldDraw, frameTime);
    ecs_run_pipeline(world, sortedWorldDraw, frameTime);

    if (isDebugDrawEnabled) {
      Physics::DebugDraw(world);
      DebugDraw::ProcessDrawQueue();
    }

    GameCamera::End2D(world);
    UpdateLoadingRevealCenter(world);
    Rendering::PresentFrame(world);
    frameStepper.DrawOverlay();
    GameConsole::Draw(world);
    screenshotCapture.CapturePending();
    screenshotCapture.DrawNotification();
    Rendering::EndFrame();

    ecs_run(world, dequeue_rest, frameTime, NULL);

    ecs_frame_end(world);
  }

  world.quit();
  GameConsole::Shutdown();
  CloseWindow();

  return 0;
}

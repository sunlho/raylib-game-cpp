#include <algorithm>
#include <iostream>
#include <utility>

#include "flecs.h"
#include "raylib.h"

#include "modules/Camera.h"
#include "modules/Character/Character.h"
#include "modules/Console/Console.h"
#include "modules/Console/Register.h"
#include "modules/Core/Core.h"
#include "modules/Debug/DebugDraw.h"
#include "modules/Debug/FrameStepper.h"
#include "modules/Debug/Screenshot.h"
#include "modules/Map/Map.h"
#include "modules/Movement.h"
#include "modules/Physics.h"
#include "modules/Reflection.h"
#include "modules/Rendering.h"
#include "modules/Stairs/Stairs.h"
#include "modules/Utils.h"
#include "modules/Window.h"

constexpr int STARTUP_PRESET = GameWindow::kDefaultPreset;

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
      .set<Core::Position>({playerStart})
      .set<Core::PreviousPosition>({playerStart})
      .set<Core::RenderPosition>({playerStart, playerStart})
      .set<Stairs::FloorState>({2.5f, 2.5f})
      .set<Movement::Velocity>({Vector2{0.0f, 0.0f}})
      .set<Movement::MoveSpeed>({100.0f})
      .set<Movement::RunSettings>({1.6f, 0.2f})
      .set<Movement::RunState>({});

  GameCamera::SnapCameraTo(world, playerStart);
  return player.id();
}

static void UpdateLoadingRevealCenter(flecs::world &world) {
  const auto player = world.lookup("Player");
  if (!player.is_valid() || !player.has<Core::Position>()) {
    return;
  }

  // Use quantised render position for the reveal centre when available so the
  // circle follows the pixel-aligned player position.
  const auto *rp = player.try_get<Core::RenderPosition>();
  const auto &pos = player.get<Core::Position>();
  const Vector2 worldPos = rp ? rp->quantized : pos.value;
  const auto &mainCamera = world.get<GameCamera::MainCamera>();
  Rendering::SetLoadingRevealCenter(world, GetWorldToScreen2D(worldPos, mainCamera.value));
}

static void RunGame() {
  flecs::world world;
  world.set<flecs::Rest>({});
  world.import<flecs::stats>();
  ecs_entity_t dequeue_rest = ecs_lookup(world, "flecs.rest.DequeueRest");

  world.import<Core::module>();
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
  const auto postPhysicsEvents = buildPipeline<Simulation::PostPhysicsEvents>(world);
  const auto fixedUpdate = buildPipeline<Simulation::FixedUpdate>(world);
  const auto fixedUpdateLate = buildPipeline<Simulation::FixedUpdateLate>(world);

  Rendering::SetSceneResolution(world, Rendering::SceneResolution::Native);

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

    if (!consoleIsOpen && IsKeyDown(KEY_F11)) {
      GameWindow::ToggleFullscreen();
    }

    const auto frameTime = GetFrameTime();
    screenshotCapture.Update(frameTime);
    Rendering::UpdateLoadingScreen(world, frameTime);

    MapManager::ProcessPendingMapLoad(world);

    const bool loadingScreenVisible = Rendering::IsLoadingScreenVisible(world);
    frameStepper.UpdateControls(!loadingScreenVisible && !consoleIsOpen);
    if (frameStepper.DidRequestScreenshotStep()) {
      screenshotCapture.RequestCapture();
    }
    if (frameStepper.DidPauseStateChange()) {
      accumulator = 0.0f;
    }

    ecs_frame_begin(world, frameTime);

    if (!loadingScreenVisible && frameStepper.ShouldAdvanceSimulation()) {
      const float simulationFrameTime = frameStepper.IsStepRequested() ? fixedTimeStep : frameTime;
      ecs_run_pipeline(world, moveUpdate, simulationFrameTime);
    }

    if (!loadingScreenVisible && frameStepper.ShouldAdvanceSimulation()) {
      const float simulationFrameTime = frameStepper.IsStepRequested() ? fixedTimeStep : frameTime;
      accumulator += std::min(simulationFrameTime, 0.25f);
      while (accumulator >= fixedTimeStep) {
        // Snapshot positions BEFORE physics so the interpolation range is
        // [previous tick end, current tick end], not [current, next].
        world.each([](const Core::Position &pos, Core::PreviousPosition &prev) {
          prev.value = pos.value;
        });

        ecs_run_pipeline(world, prePhysics, fixedTimeStep);
        ecs_run_pipeline(world, physicsStep, fixedTimeStep);
        ecs_run_pipeline(world, postPhysics, fixedTimeStep);
        ecs_run_pipeline(world, postPhysicsEvents, fixedTimeStep);
        ecs_run_pipeline(world, fixedUpdate, fixedTimeStep);
        ecs_run_pipeline(world, fixedUpdateLate, fixedTimeStep);
        ecs_run_pipeline(world, characterUpdate, fixedTimeStep);
        // Camera follow is updated per render frame below, not here.
        frameStepper.RecordFixedStep();
        accumulator -= fixedTimeStep;
      }
    }

    // Re-derive everything that depends on the window size (scene buffer
    // density in Native mode, and the output scale). Must run before the camera
    // update so it quantises against the new grid.
    Rendering::UpdateOutputGeometry(world);

    // --- Per-render-frame render preparation ---
    // 1. Interpolate simulation positions. While paused there is no partial
    //    tick to blend, so show the last completed tick (alpha = 1) instead of
    //    snapping back to PreviousPosition.
    const float renderAlpha = frameStepper.IsPaused() ? 1.0f : std::clamp(accumulator / fixedTimeStep, 0.0f, 1.0f);
    if (!loadingScreenVisible) {
      Rendering::InterpolatePositions(world, renderAlpha);

      // 2. Update the smooth camera using the interpolated player position.
      //    Must happen before QuantizeRenderPositions so camera.renderTarget is ready.
      //    The camera is part of the simulation's visible state, so it only
      //    advances when the simulation does; otherwise its exponential damping
      //    would keep closing on the player while the world is frozen. A single
      //    step advances it by exactly one fixed tick, not by real frame time.
      if (frameStepper.ShouldAdvanceSimulation()) {
        const float cameraDeltaTime = frameStepper.IsStepRequested() ? fixedTimeStep : frameTime;
        const auto playerEntity = world.lookup("Player");
        if (playerEntity.is_valid()) {
          const auto *rp = playerEntity.try_get<Core::RenderPosition>();
          if (rp) {
            GameCamera::UpdateRenderCamera(world, rp->interpolated, cameraDeltaTime);
          }
        }
      }

      // 3. Quantise all dynamic entities relative to the final camera position.
      Rendering::QuantizeRenderPositions(world);
    }

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
}

int main() {

  InitWindow(GameWindow::kPresets[STARTUP_PRESET].width, GameWindow::kPresets[STARTUP_PRESET].height, "raylib game cpp");

  GameWindow::ApplyPreset(STARTUP_PRESET);
  SetExitKey(KEY_NULL);
  SetTargetFPS(60);

  RunGame();

  GameConsole::Shutdown();
  Rendering::Shutdown();
  CloseWindow();

  return 0;
}

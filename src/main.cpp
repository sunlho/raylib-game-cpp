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
      // {"walk-N", "player/A_walk-N-debug.webp", 0.09f},
      // {"walk-S", "player/A_walk-S-debug.webp", 0.09f},
      // {"walk-E", "player/A_walk-E-debug.webp", 0.09f},
      // {"walk-W", "player/A_walk-W-debug.webp", 0.09f},
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

  // Snap the camera to the player spawn so it doesn't fly in from {0,0}.
  GameCamera::SnapCameraTo(world, playerStart);
  return player.id();
}

static void UpdateLoadingRevealCenter(flecs::world &world) {
  const auto player = world.lookup("Player");
  if (!player.is_valid() || !player.has<Rendering::Position>()) {
    return;
  }

  // Use quantised render position for the reveal centre when available so the
  // circle follows the pixel-aligned player position.
  const auto *rp = player.try_get<Rendering::RenderPosition>();
  const auto &pos = player.get<Rendering::Position>();
  const Vector2 worldPos = rp ? rp->quantized : pos.value;
  const auto &mainCamera = world.get<GameCamera::MainCamera>();
  Rendering::SetLoadingRevealCenter(world, GetWorldToScreen2D(worldPos, mainCamera.value));
}

int main() {

  // The scene buffer is a fixed size, so the window is free to be any size:
  // only the final blit rectangle depends on it (see Rendering::PresentFrame).
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "raylib game cpp");
  SetWindowMinSize(BASE_WIDTH, BASE_HEIGHT);
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
  // cameraFollow pipeline kept for ECS phase completeness; no systems run in it.
  [[maybe_unused]] const auto cameraFollow = buildPipeline<Movement::Phases::CameraFollow>(world);

  const auto characterUpdate = buildPipeline<Character::Phases::Update>(world);

  const auto prePhysics = buildPipeline<Simulation::PrePhysics>(world);
  const auto physicsStep = buildPipeline<Simulation::PhysicsStep>(world);
  const auto postPhysics = buildPipeline<Simulation::PostPhysics>(world);
  const auto fixedUpdate = buildPipeline<Simulation::FixedUpdate>(world);

  // The visible world is fixed at 640x360 world units regardless of window size.
  auto &logicalView = world.get_mut<Rendering::LogicalViewSize>();
  logicalView.value = Vector2{static_cast<float>(BASE_WIDTH), static_cast<float>(BASE_HEIGHT)};

  // Native = the scene buffer follows the window at an integer zoom, which is
  // what Eastward's default "clear" mode does. It is the only setting where the
  // camera AND every character quantise to half an output pixel rather than
  // half an art pixel, so neither the map nor the characters visibly step.
  // 'sceneres full|half' pins a fixed buffer instead.
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

    // Alt+Enter, not F11: F11 is already the screenshot key (Debug::ScreenshotCapture).
    const bool altHeld = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    if (!consoleIsOpen && altHeld && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))) {
      ToggleBorderlessWindowed();
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
      // Cap the accumulated time to avoid a death spiral after a long stall
      // (e.g. window drag or debugger pause).
      accumulator += std::min(simulationFrameTime, 0.25f);
      while (accumulator >= fixedTimeStep) {
        // Snapshot positions BEFORE physics so the interpolation range is
        // [previous tick end, current tick end], not [current, next].
        world.each([](const Rendering::Position &pos, Rendering::PreviousPosition &prev) {
          prev.value = pos.value;
        });

        ecs_run_pipeline(world, prePhysics, fixedTimeStep);
        ecs_run_pipeline(world, physicsStep, fixedTimeStep);
        ecs_run_pipeline(world, postPhysics, fixedTimeStep);
        ecs_run_pipeline(world, fixedUpdate, fixedTimeStep);
        ecs_run_pipeline(world, characterUpdate, fixedTimeStep);
        // cameraFollow removed: camera is now updated per render frame below.
        frameStepper.RecordFixedStep();
        accumulator -= fixedTimeStep;
      }
    }

    // Re-derive the scene buffer density if the window changed (Native only).
    // Must run before the camera update so it quantises against the new grid.
    Rendering::UpdateAutoSceneResolution(world);

    // --- Per-render-frame render preparation ---
    // 1. Interpolate simulation positions. While paused there is no partial
    //    tick to blend, so show the last completed tick (alpha = 1) instead of
    //    snapping back to PreviousPosition.
    const float renderAlpha = frameStepper.IsPaused()
                                  ? 1.0f
                                  : std::clamp(accumulator / fixedTimeStep, 0.0f, 1.0f);
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
          const auto *rp = playerEntity.try_get<Rendering::RenderPosition>();
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
  GameConsole::Shutdown();
  Rendering::Shutdown();
  CloseWindow();

  return 0;
}

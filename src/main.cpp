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
#include "modules/Input.h"
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
      .add<GameCamera::FollowTarget>()
      .set<Character::CharacterInfo>({"Player", Character::CharacterState::Idle, Character::CharacterDirection::Down})
      .set<Character::CharacterStats>({100.0f, 100.0f, 10.0f, 2.0f})
      .set<Character::AnimationController>({})
      .set<Character::IdleBehavior>({})
      .set<Character::SpriteSet>(std::move(playerSprites))
      .set<Core::Position>({playerStart})
      .set<Core::PreviousPosition>({playerStart})
      .set<Core::RenderPosition>({playerStart, playerStart})
      .set<Stairs::FloorState>({2.5f, 2.5f});

  Movement::EnablePlayerMovement(player, {100.0f, 1.6f, 0.2f});

  GameCamera::SnapCameraTo(world, playerStart);
  return player.id();
}

static void UpdateLoadingRevealCenter(flecs::world &world, flecs::query<const Core::RenderPosition> &query) {
  Vector2 worldPosition = {};
  bool found = false;
  query.each([&](const Core::RenderPosition &renderPosition) {
    if (found) {
      return;
    }

    worldPosition = renderPosition.quantized;
    found = true;
  });

  if (found) {
    const auto &mainCamera = world.get<GameCamera::MainCamera>();
    Rendering::SetLoadingRevealCenter(world, GetWorldToScreen2D(worldPosition, mainCamera.value));
  }
}

struct MapStatusPresentation {
  MapManager::Status pending;
  bool dirty = false;

  void Capture(const MapManager::Status &status) {
    pending = status;
    dirty = true;
  }

  void Flush(flecs::world &world) {
    if (!dirty) {
      return;
    }
    dirty = false;

    if (pending.phase == MapManager::Phase::Running) {
      Rendering::SetLoadingProgress(world, pending.progress, pending.hint);
      return;
    }
    if (pending.phase == MapManager::Phase::Failed && pending.failure) {
      TraceLog(LOG_ERROR, "Map operation %llu failed: %s", static_cast<unsigned long long>(pending.id), pending.failure->message.c_str());
      Rendering::SetLoadingProgress(world, 1.0f, "Map failed: " + pending.failure->message);
      Rendering::BeginLoadingReveal(world);
      return;
    }
    if (pending.phase == MapManager::Phase::Succeeded) {
      Rendering::BeginLoadingReveal(world);
    }
  }
};

static void RunGame() {
  MapStatusPresentation mapStatusPresentation;
  flecs::world world;
  world.set<flecs::Rest>({});
  world.import<flecs::stats>();
  ecs_entity_t dequeue_rest = ecs_lookup(world, "flecs.rest.DequeueRest");

  world.import<Core::module>();
  world.import<Rendering::module>();
  world.import<GameConsole::module>();
  world.import<Input::module>();
  world.import<GameCamera::module>();
  world.import<Movement::module>();
  world.import<Physics::module>();
  world.import<Character::module>();
  world.import<Stairs::module>();
  world.import<MapManager::module>();

  auto followTargetQuery =
      world.query_builder<const Core::RenderPosition>()
          .with<GameCamera::FollowTarget>()
          .build();

  world.observer<const MapManager::Status>("Present Map Lifecycle")
      .event(flecs::OnSet)
      .each([&mapStatusPresentation](const MapManager::Status &status) {
        // OnSet runs synchronously inside modified<Status>(). Only copy the
        // event here; mutating other ECS singletons from this callback would
        // re-enter Flecs while it is publishing the event.
        mapStatusPresentation.Capture(status);
      });

  GameConsole::RegisterCommands(world, {&isDebugDrawEnabled});

  const auto background = buildPipeline<Rendering::Phases::Background>(world);
  const auto worldDraw = buildPipeline<Rendering::Phases::World>(world);
  const auto sortedWorldDraw = buildPipeline<Rendering::Phases::SortedWorld>(world);

  Rendering::SetSceneResolution(world, Rendering::SceneResolution::Native);

  float fixedTimeStep = 1.0f / 60.0f;
  float accumulator = 0.0f;
  Debug::FrameStepper frameStepper;
  Debug::ScreenshotCapture screenshotCapture;
  ecs_progress(world, 0);
  CreatePlayer(world);
  MapManager::Submit(world, MapManager::MapTransition{"Map.tmx"});
  mapStatusPresentation.Flush(world);

  while (!WindowShouldClose()) {
    const bool consoleWasOpen = GameConsole::IsOpen(world);
    if (!Rendering::IsLoadingScreenVisible(world)) {
      GameConsole::Update(world);
    }
    const bool consoleIsOpen = GameConsole::IsOpen(world);
    Input::Update(world);

    if (!consoleWasOpen && IsKeyPressed(KEY_ESCAPE)) {
      break;
    }

    if (!consoleIsOpen && IsKeyDown(KEY_F11)) {
      GameWindow::ToggleFullscreen();
    }

    const auto frameTime = GetFrameTime();
    screenshotCapture.Update(frameTime);
    MapManager::AdvanceLifecycle(world);
    mapStatusPresentation.Flush(world);
    Rendering::UpdateLoadingScreen(world, frameTime);

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
      accumulator += std::min(simulationFrameTime, 0.25f);
      while (accumulator >= fixedTimeStep) {
        Simulation::RunFixedTick(world, fixedTimeStep);
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
        GameCamera::UpdateRenderCamera(world, followTargetQuery, cameraDeltaTime);
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
    UpdateLoadingRevealCenter(world, followTargetQuery);
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

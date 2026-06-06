#include <iostream>
#include <memory>
#include <utility>

#include "box2d/box2d.h"
#include "flecs.h"
#include "raylib.h"

#include "modules/Camera.h"
#include "modules/Character/Character.h"
#include "modules/MapManage.h"
#include "modules/Movement.h"
#include "modules/Physics.h"
#include "modules/Reflection.h"
#include "modules/Rendering.h"
#include "modules/Simulation.h"

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

int main() {
  flecs::world world;
  world.set<flecs::Rest>({});
  world.import<flecs::stats>();

  Reflection::Register<Rectangle>(world);
  Reflection::Register<RenderTexture2D>(world);

  Rendering::Import(world);
  GameCamera::Import(world);
  Movement::Import(world);
  Character::Import(world);
  MapManage::Import(world);
  Physics::Import(world);

  const auto preDraw = world.pipeline().with(flecs::System).with<Rendering::Phases::PreDraw>().build();
  const auto background = world.pipeline().with(flecs::System).with<Rendering::Phases::Background>().build();
  const auto draw = world.pipeline().with(flecs::System).with<Rendering::Phases::Draw>().build();
  const auto postDraw = world.pipeline().with(flecs::System).with<Rendering::Phases::PostDraw>().build();

  const auto begin2D = world.pipeline().with(flecs::System).with<GameCamera::Phases::Begin2D>().build();
  const auto end2D = world.pipeline().with(flecs::System).with<GameCamera::Phases::End2D>().build();

  const auto moveUpdate = world.pipeline().with(flecs::System).with<Movement::Phases::Update>().build();
  const auto cameraFollow = world.pipeline().with(flecs::System).with<Movement::Phases::CameraFollow>().build();

  const auto characterUpdate = world.pipeline().with(flecs::System).with<Character::Phases::Update>().build();

  const auto fixedUpdate = world.pipeline().with(flecs::System).with<Simulation::FixedUpdate>().build();

  const auto mainWindow = world.component<Rendering::MainWindow>()
                              .add(flecs::Singleton)
                              .set<Rendering::WindowSize>({SCREEN_WIDTH, SCREEN_HEIGHT})
                              .set<Rendering::WindowTitle>({"raylib game cpp"})
                              .set<Rendering::WindowFPS>({60});
  auto windowFPS = mainWindow.get_mut<Rendering::WindowFPS>();

  world.component<Rendering::RenderTargetSize>()
      .add(flecs::Singleton)
      .set<Rendering::RenderTargetSize>({Vector2{static_cast<float>(BASE_WIDTH), static_cast<float>(BASE_HEIGHT)}});

  world.component<Rendering::RenderTargetState>()
      .add(flecs::Singleton)
      .set<Rendering::RenderTargetState>({});

  world.component<RenderTexture2D>()
      .add(flecs::Singleton)
      .set<RenderTexture2D>({});

  world.component<GameCamera::MainCamera>()
      .add(flecs::Singleton)
      .set<GameCamera::CameraState>({
          Camera2D{Vector2{0.0f, 0.0f}, Vector2{0.0f, 0.0f}, 0.0f, 1.0f},
          true,
          true,
      });

  MapManage::SetMapPath(world, "Map.tmx");

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

  world.entity("player")
      .add<Character::PlayerTag>()
      .add<Movement::PlayerControlled>()
      .add<Movement::CameraFollowTag>()
      .set<Character::CharacterInfo>({"Player", Character::CharacterState::Idle, Character::CharacterDirection::Down})
      .set<Character::CharacterStats>({100.0f, 100.0f, 10.0f, 2.0f})
      .set<Character::AnimationController>({})
      .set<Character::IdleBehavior>({})
      .set<Character::SpriteSet>(playerSprites)
      .set<Rendering::Position>({Vector2{500.0f, 500.0f}})
      .set<Movement::Velocity>({Vector2{0.0f, 0.0f}})
      .set<Movement::MoveSpeed>({85.0f});

  float timeStep = 1.0f / 60.0f;
  Physics::CreateBox2DWorld(world, timeStep);
  float accumulator = 0.0f;
  ecs_progress(world, 0);

  while (!WindowShouldClose()) {

    if (IsKeyPressed(KEY_PAGE_UP)) {
      SetTargetFPS(std::min(240, GetFPS() + 10));
    }
    if (IsKeyPressed(KEY_PAGE_DOWN)) {
      SetTargetFPS(std::max(60, GetFPS() - 10));
    }

    ecs_run_pipeline(world, moveUpdate, GetFrameTime());

    accumulator += GetFrameTime();
    while (accumulator >= timeStep) {
      ecs_run_pipeline(world, fixedUpdate, timeStep);
      ecs_run_pipeline(world, characterUpdate, GetFrameTime());
      accumulator -= timeStep;
    }
    ecs_run_pipeline(world, cameraFollow, GetFrameTime());

    ecs_run_pipeline(world, preDraw, GetFrameTime());
    ecs_run_pipeline(world, background, GetFrameTime());
    ecs_run_pipeline(world, begin2D, GetFrameTime());
    ecs_run_pipeline(world, draw, GetFrameTime());
    ecs_run_pipeline(world, end2D, GetFrameTime());
    ecs_run_pipeline(world, postDraw, GetFrameTime());
  }

  world.quit();

  return 0;
}

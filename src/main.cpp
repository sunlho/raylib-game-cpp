#include <memory>

#include "flecs.h"
#include "raylib.h"

#include "modules/Camera.h"
#include "modules/Movement.h"
#include "modules/Reflection.h"
#include "modules/Rendering.h"
#include "modules/Tilemap/Tilemap.h"

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720

int main() {
  flecs::world world;
  world.set<flecs::Rest>({});
  world.import<flecs::stats>();

  // world.component<Vector2>();
  Reflection::Register<Rectangle>(world);
  Reflection::Register<RenderTexture2D>(world);

  Rendering::Import(world);
  Movement::Import(world);
  GameCamera::Import(world);
  Tilemap::Import(world);

  world.component<Rendering::MainWindow>()
      .add(flecs::Singleton)
      .set<Rendering::WindowSize>({SCREEN_WIDTH, SCREEN_HEIGHT})
      .set<Rendering::WindowTitle>({"raylib game cpp"})
      .set<Rendering::WindowFPS>({60});

  world.component<GameCamera::MainCamera>()
      .add(flecs::Singleton)
      .set<GameCamera::CameraState>({
          Camera2D{
              Vector2{0.0f, 0.0f},
              Vector2{0.0f, 0.0f},
              0.0f,
              2.0f},
          true,
          true,
      });

  world.entity("tilemap")
      .set<Tilemap::TilemapPath>({"Map.tmx"});

  world.entity("player")
      .add<Movement::PlayerControlled>()
      .add<Movement::CameraFollowTag>()
      .set<Rendering::Position>({Vector2{128.0f, 128.0f}})
      .set<Movement::Velocity>({Vector2{0.0f, 0.0f}})
      .set<Movement::MoveSpeed>({160.0f})
      .set<Rendering::RenderComponent>({
          std::make_shared<Rendering::CircleRenderable>(LIME, 8.0f),
          true,
      });

  while (world.progress(GetFrameTime())) {
  }

  return 0;
}

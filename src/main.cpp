#include "flecs.h"
#include "raylib.h"

#include <memory>

#include "modules/Camera.h"
#include "modules/Rendering.h"
#include "modules/Tilemap/Tilemap.h"

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720

int main() {
  flecs::world world;
  world.set<flecs::Rest>({});

  GameCamera::Import(world);
  Rendering::Import(world);
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

  while (world.progress(GetFrameTime())) {
  }

  return 0;
}

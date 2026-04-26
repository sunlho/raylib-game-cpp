#include "flecs.h"
#include "raylib.h"

#include <memory>

#include "modules/Rendering.h"
#include "modules/Tilemap.h"

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720

int main() {
  flecs::world world;
  world.set<flecs::Rest>({});

  world.import<Rendering>();
  world.import<Tilemap>();

  world.component<Rendering::MainWindow>()
      .add(flecs::Singleton)
      .set<Rendering::WindowSize>({SCREEN_WIDTH, SCREEN_HEIGHT})
      .set<Rendering::WindowTitle>({"raylib game cpp"})
      .set<Rendering::WindowFPS>({60});

  world.entity("tilemap")
      .set<Tilemap::TilemapPath>({"Map.tmx"});

  while (world.progress(GetFrameTime())) {
  }

  return 0;
}

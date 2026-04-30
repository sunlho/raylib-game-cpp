#include <memory>

#include "flecs.h"
#include "raylib.h"

#include "modules/Camera.h"
#include "modules/Character.h"
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
  Character::Import(world);
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
      .set<Rendering::Position>({Vector2{128.0f, 128.0f}})
      .set<Movement::Velocity>({Vector2{0.0f, 0.0f}})
      .set<Movement::MoveSpeed>({120.0f});

  world.entity("npc")
      .add<Character::NPCTag>()
      .set<Character::CharacterInfo>({"NPC", Character::CharacterState::Idle, Character::CharacterDirection::Down})
      .set<Character::CharacterStats>({60.0f, 60.0f, 6.0f, 1.0f})
      .set<Rendering::Position>({Vector2{256.0f, 160.0f}});

  while (world.progress(GetFrameTime())) {
    if (WindowShouldClose()) {
      break;
    }
  }

  return 0;
}

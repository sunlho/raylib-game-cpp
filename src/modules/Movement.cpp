#include "Movement.h"

#include "Camera.h"
#include "Rendering.h"
#include "raymath.h"

void Movement::Import(flecs::world &world) {
  auto updatePhase = world.entity<Movement::Phases::Update>();
  auto followPhase = world.entity<Movement::Phases::CameraFollow>();

  updatePhase
      .add(flecs::Phase)
      .depends_on(world.entity<Rendering::Phases::PreDraw>());

  followPhase
      .add(flecs::Phase)
      .depends_on(updatePhase);

  world.entity<Rendering::Phases::Background>()
      .depends_on(followPhase);

  world.system<Velocity, const MoveSpeed>("Update Player Input")
      .kind<Movement::Phases::Update>()
      .with<PlayerControlled>()
      .each([](Velocity &velocity, const MoveSpeed &speed) {
        Vector2 direction = {
            static_cast<float>(IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) -
                static_cast<float>(IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)),
            static_cast<float>(IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) -
                static_cast<float>(IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))};

        if (Vector2LengthSqr(direction) > 0.0f) {
          direction = Vector2Normalize(direction);
        }

        velocity.value = Vector2Scale(direction, speed.value);
      });

  world.system<Rendering::Position, const Velocity>("Move Entities")
      .kind<Movement::Phases::Update>()
      .each([](flecs::iter &it, size_t i, Rendering::Position &position, const Velocity &velocity) {
        position.value = Vector2Add(position.value, Vector2Scale(velocity.value, it.delta_time()));
      });

  world.system<const Rendering::Position>("Follow Camera Target")
      .with<CameraFollowTag>()
      .kind<Movement::Phases::CameraFollow>()
      .each([&world](const Rendering::Position &position) {
        auto mainCamera = world.singleton<GameCamera::MainCamera>();
        auto &cameraState = mainCamera.get_mut<GameCamera::CameraState>();
        cameraState.value.target = position.value;
      });
}

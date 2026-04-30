#include "Camera.h"

#include "Reflection.h"
#include "Rendering.h"

void GameCamera::Import(flecs::world &world) {

  Reflection::Register<CameraState>(world);

  auto beginPhase = world.entity<GameCamera::Phases::Begin2D>();
  auto endPhase = world.entity<GameCamera::Phases::End2D>();

  beginPhase
      .add(flecs::Phase)
      .depends_on(world.entity<Rendering::Phases::Background>());
  world.entity<Rendering::Phases::Draw>()
      .depends_on(beginPhase);

  endPhase
      .add(flecs::Phase)
      .depends_on(world.entity<Rendering::Phases::Draw>());
  world.entity<Rendering::Phases::PostDraw>()
      .depends_on(endPhase);

  world.system("Auto Center Camera Offset")
      .kind<GameCamera::Phases::Begin2D>()
      .run([](flecs::iter &it) {
        const auto &world = it.world();
        auto cameraEntity = world.singleton<GameCamera::MainCamera>();
        auto &cameraState = cameraEntity.get_mut<GameCamera::CameraState>();
        if (!cameraState.autoCenterOffset) {
          return;
        }

        auto mainWindow = world.singleton<Rendering::MainWindow>();
        const auto &windowSize = mainWindow.get<Rendering::WindowSize>();

        cameraState.value.offset = Vector2{
            windowSize.dimension.x * 0.5f,
            windowSize.dimension.y * 0.5f};
      });

  world.system<const CameraState>("Begin Camera 2D")
      .kind<GameCamera::Phases::Begin2D>()
      .each([](const CameraState &cameraState) {
        if (!cameraState.enabled) {
          return;
        }

        BeginMode2D(cameraState.value);
      });

  world.system<const CameraState>("End Camera 2D")
      .kind<GameCamera::Phases::End2D>()
      .each([](const CameraState &cameraState) {
        if (!cameraState.enabled) {
          return;
        }

        EndMode2D();
      });
}

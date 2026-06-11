#include "Camera.h"

#include "Reflection.h"
#include "Rendering.h"
#include "Simulation.h"

namespace GameCamera {

module::module(flecs::world &world) {
  Reflection::Register<Camera2D>(world);
  Reflection::Register<CameraState>(world);

  world.system("Auto Center Camera Offset")
      .kind<Phases::Begin2D>()
      .run([](flecs::iter &it) {
        const auto &world = it.world();
        auto cameraEntity = world.singleton<MainCamera>();
        auto &cameraState = cameraEntity.get_mut<CameraState>();
        if (!cameraState.autoCenterOffset) {
          return;
        }

        const auto renderTargetSize = world.get<Rendering::RenderTargetSize>();

        cameraState.value.offset = Vector2{
            renderTargetSize.dimension.x * 0.5f,
            renderTargetSize.dimension.y * 0.5f};
      });

  world.system<const CameraState>("Begin Camera 2D")
      .kind<Phases::Begin2D>()
      .each([](const CameraState &cameraState) {
        if (!cameraState.enabled) {
          return;
        }

        BeginMode2D(cameraState.value);
      });

  world.system<const CameraState>("End Camera 2D")
      .kind<Phases::End2D>()
      .each([](const CameraState &cameraState) {
        if (!cameraState.enabled) {
          return;
        }

        EndMode2D();
      });
}

} // namespace GameCamera

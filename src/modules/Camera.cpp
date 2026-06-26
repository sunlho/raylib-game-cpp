#include "Camera.h"

#include "Reflection.h"
#include "Rendering.h"
#include "Simulation.h"

namespace GameCamera {

module::module(flecs::world &world) {
  Reflection::Register<Camera2D>(world);
  Reflection::Register<MainCamera>(world)
      .add(flecs::Singleton)
      .set<MainCamera>({Camera2D{Vector2{0.0f, 0.0f}, Vector2{0.0f, 0.0f}, 0.0f, 1.0f}, true, true});

  world.system("Auto Center Camera Offset")
      .kind<Phases::Begin2D>()
      .run([](flecs::iter &it) {
        const auto &world = it.world();
        auto &mainCamera = world.get_mut<MainCamera>();
        if (!mainCamera.autoCenterOffset) {
          return;
        }

        const auto renderTargetSize = world.get<Rendering::RenderTargetSize>();

        mainCamera.value.offset = Vector2{
            renderTargetSize.dimension.x * 0.5f,
            renderTargetSize.dimension.y * 0.5f};
      });

  world.system<const MainCamera>("Begin Camera 2D")
      .kind<Phases::Begin2D>()
      .each([](const MainCamera &mainCamera) {
        if (!mainCamera.enabled) {
          return;
        }

        BeginMode2D(mainCamera.value);
      });

  world.system<const MainCamera>("End Camera 2D")
      .kind<Phases::End2D>()
      .each([](const MainCamera &mainCamera) {
        if (!mainCamera.enabled) {
          return;
        }

        EndMode2D();
      });
}

} // namespace GameCamera

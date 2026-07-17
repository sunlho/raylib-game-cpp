#include "Camera.h"

#include "Reflection.h"
#include "Rendering.h"

namespace GameCamera {

void Begin2D(flecs::world &world) {
  auto &mainCamera = world.get_mut<MainCamera>();
  if (mainCamera.autoCenterOffset) {
    const auto &renderTargetSize = world.get<Rendering::RenderTargetSize>();
    mainCamera.value.offset = Vector2{
        renderTargetSize.dimension.x * 0.5f,
        renderTargetSize.dimension.y * 0.5f};
  }

  if (mainCamera.enabled) {
    BeginMode2D(mainCamera.value);
  }
}

void End2D(const flecs::world &world) {
  if (world.get<MainCamera>().enabled) {
    EndMode2D();
  }
}

module::module(flecs::world &world) {
  Reflection::Register<Camera2D>(world);
  Reflection::Register<MainCamera>(world)
      .add(flecs::Singleton)
      .set<MainCamera>({Camera2D{Vector2{0.0f, 0.0f}, Vector2{0.0f, 0.0f}, 0.0f, 1.0f}, true, true});
}

} // namespace GameCamera

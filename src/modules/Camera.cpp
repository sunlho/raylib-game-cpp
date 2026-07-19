#include "Camera.h"

#include <cmath>

#include "raymath.h"

#include "Reflection.h"
#include "Rendering.h"

namespace GameCamera {

void Begin2D(flecs::world &world) {
  auto &mainCamera = world.get_mut<MainCamera>();
  auto &renderTargetState = world.get_mut<Rendering::RenderTargetState>();
  if (mainCamera.autoCenterOffset) {
    const auto &renderTargetSize = world.get<Rendering::RenderTargetSize>();
    mainCamera.value.offset = Vector2{
        renderTargetSize.dimension.x * 0.5f,
        renderTargetSize.dimension.y * 0.5f};
  }

  if (mainCamera.enabled) {
    Camera2D renderCamera = mainCamera.value;
    if (renderTargetState.active) {
      const float padding = static_cast<float>(renderTargetState.padding);
      renderCamera.offset = Vector2Add(renderCamera.offset, Vector2{padding, padding});
    }

    renderTargetState.cameraSubpixelOffset = Vector2{0.0f, 0.0f};
    if (renderTargetState.active && mainCamera.snapTargetToPixel) {
      // Draw at whole world pixels, then restore the discarded fraction while compositing.
      Camera2D exactCamera = renderCamera;
      renderCamera.target = Vector2{
          roundf(renderCamera.target.x),
          roundf(renderCamera.target.y)};

      const Vector2 origin = Vector2{0.0f, 0.0f};
      renderTargetState.cameraSubpixelOffset = Vector2Subtract(
          GetWorldToScreen2D(origin, renderCamera),
          GetWorldToScreen2D(origin, exactCamera));
    }

    BeginMode2D(renderCamera);
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

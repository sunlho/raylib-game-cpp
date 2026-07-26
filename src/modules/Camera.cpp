#include "Camera.h"

#include <algorithm>
#include <cmath>

#include "Reflection.h"
#include "raymath.h"

namespace GameCamera {

namespace {

Vector2 ClampCameraToWorld(Vector2 target, Vector2 logicalViewSize, Vector2 worldSize) {
  const Vector2 halfView = Vector2Scale(logicalViewSize, 0.5f);

  auto clampAxis = [](float value, float halfExtent, float extent) -> float {
    if (extent <= 0.0f)
      return value;
    if (extent < halfExtent * 2.0f)
      return extent * 0.5f;
    return std::clamp(value, halfExtent, extent - halfExtent);
  };

  return {
      clampAxis(target.x, halfView.x, worldSize.x),
      clampAxis(target.y, halfView.y, worldSize.y),
  };
}

} // namespace

void Begin2D(flecs::world &world) {
  auto &mainCamera = world.get_mut<MainCamera>();
  if (mainCamera.autoCenterOffset) {
    // Offset is in scene-buffer pixels: the centre of the guarded buffer, so
    // renderTarget lands exactly on the middle of the visible scene area.
    const auto &renderTargetSize = world.get<Core::RenderTargetSize>();
    mainCamera.value.offset = Vector2{
        renderTargetSize.dimension.x * 0.5f + Core::kSceneGuardPixels,
        renderTargetSize.dimension.y * 0.5f + Core::kSceneGuardPixels};
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

void UpdateRenderCamera(flecs::world &world, Vector2 interpolatedPlayerPos, float dt) {
  auto &camera = world.get_mut<MainCamera>();
  const Vector2 viewSize = world.get<Core::LogicalViewSize>().value;
  const Vector2 worldSize = world.get<Core::WorldBounds>().dimension;

  // --- Smooth camera ---
  const Vector2 desired = ClampCameraToWorld(interpolatedPlayerPos, viewSize, worldSize);

  const float blend = 1.0f - std::exp(-camera.followSpeed * dt);
  camera.smoothTarget = Vector2Lerp(camera.smoothTarget, desired, blend);
  camera.smoothTarget = ClampCameraToWorld(camera.smoothTarget, viewSize, worldSize);

  // --- Quantise to the render grid, keeping the residual for the final blit ---
  if (camera.snapToRenderGrid) {
    const float ppwu = camera.pixelsPerWorldUnit;

    // How many output pixels one scene pixel becomes in the current window.
    const float outputScale = world.get<Core::OutputScale>().value;
    const float outputSteps = std::max(1.0f, std::round(outputScale));

    // fine: finest grid the output can actually show (one output pixel).
    // coarse: what the scene buffer can render (one scene pixel).
    const Vector2 fine = Core::SnapToGrid(camera.smoothTarget, ppwu * outputSteps);
    const Vector2 unclamped = Core::SnapToGrid(fine, ppwu);
    Vector2 coarse = ClampCameraToWorld(unclamped, viewSize, worldSize);
    // Second snap after clamp, in case the world boundary is not on a grid multiple.
    coarse = Core::SnapToGrid(coarse, ppwu);

    camera.renderTarget = coarse;

    // Translate the blit by the discarded remainder. Skipped while the camera is
    // held against a world edge: there the remainder is an artefact of clamping,
    // and shifting would pull guard pixels from outside the world into view. The
    // camera is not moving there either, so there is no jitter to compensate.
    const bool clampedToEdge = coarse.x != unclamped.x || coarse.y != unclamped.y;
    if (clampedToEdge) {
      camera.renderShift = {0.0f, 0.0f};
    } else {
      const Vector2 residual = Vector2Subtract(coarse, fine);
      const float guard = Core::kSceneGuardPixels * outputScale;
      camera.renderShift = {
          std::clamp(std::round(residual.x * ppwu * outputScale), -guard, guard),
          std::clamp(std::round(residual.y * ppwu * outputScale), -guard, guard)};
    }
  } else {
    camera.renderTarget = camera.smoothTarget;
    camera.renderShift = {0.0f, 0.0f};
  }

  camera.value.target = camera.renderTarget;
}

void SnapCameraTo(flecs::world &world, Vector2 focus) {
  auto &camera = world.get_mut<MainCamera>();
  const Vector2 viewSize = world.get<Core::LogicalViewSize>().value;
  const Vector2 worldSize = world.get<Core::WorldBounds>().dimension;

  const Vector2 clamped = ClampCameraToWorld(focus, viewSize, worldSize);

  camera.smoothTarget = clamped;
  camera.renderTarget = camera.snapToRenderGrid
                            ? Core::SnapToGrid(clamped, camera.pixelsPerWorldUnit)
                            : clamped;
  camera.renderShift = {0.0f, 0.0f};
  camera.value.target = camera.renderTarget;
}

module::module(flecs::world &world) {
  Reflection::Register<Camera2D>(world);
  Reflection::Register<MainCamera>(world)
      .add(flecs::Singleton)
      .set<MainCamera>({});
}

} // namespace GameCamera

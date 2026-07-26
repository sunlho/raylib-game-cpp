#include "Camera.h"

#include <algorithm>
#include <cmath>

#include "Map/Map.h"
#include "Reflection.h"
#include "Rendering.h"
#include "raymath.h"

namespace GameCamera {

namespace {

// Convert a 60 Hz reference lerp factor to a frame-rate-independent exponential
// damping blend: blend = 1 - exp(-lambda * dt).
float ReferenceFrameLerp(float referenceFactor, float dt, float referenceHz = 60.0f) {
  const float factor = std::clamp(referenceFactor, 0.0f, 0.999999f);
  const float lambda = -std::log(1.0f - factor) * referenceHz;
  return 1.0f - std::exp(-lambda * std::max(dt, 0.0f));
}

float DesiredLookAheadDistance(FollowMotion motion) {
  switch (motion) {
  case FollowMotion::Walk:
    return 7.0f;
  case FollowMotion::Attack:
    return 3.0f;
  case FollowMotion::Run:
    return 10.0f;
  case FollowMotion::Idle:
    return 0.0f;
  }
  return 0.0f;
}

void UpdateFocusProxy(FocusProxy &focus, Vector2 desiredDirection, FollowMotion motion, float dt) {
  if (Vector2LengthSqr(desiredDirection) > 0.0f) {
    desiredDirection = Vector2Normalize(desiredDirection);
    const float dirBlend = ReferenceFrameLerp(0.04f, dt);
    Vector2 blended = Vector2Lerp(focus.direction, desiredDirection, dirBlend);
    if (Vector2LengthSqr(blended) > 0.0f) {
      focus.direction = Vector2Normalize(blended);
    }
  }

  const float desiredDistance = DesiredLookAheadDistance(motion);
  const float distFactor = desiredDistance > focus.distance ? 0.10f : 0.02f;
  focus.distance = Lerp(focus.distance, desiredDistance, ReferenceFrameLerp(distFactor, dt));
  focus.offset = Vector2Scale(focus.direction, focus.distance);
}

Vector2 ClampCameraToMap(Vector2 target, Vector2 logicalViewSize, Vector2 mapSize) {
  const Vector2 halfView = Vector2Scale(logicalViewSize, 0.5f);

  auto clampAxis = [](float value, float halfExtent, float extent) -> float {
    if (extent <= 0.0f)
      return value;
    if (extent < halfExtent * 2.0f)
      return extent * 0.5f;
    return std::clamp(value, halfExtent, extent - halfExtent);
  };

  return {
      clampAxis(target.x, halfView.x, mapSize.x),
      clampAxis(target.y, halfView.y, mapSize.y),
  };
}

} // namespace

void Begin2D(flecs::world &world) {
  auto &mainCamera = world.get_mut<MainCamera>();
  if (mainCamera.autoCenterOffset) {
    // Offset is in scene-buffer pixels: the centre of the guarded buffer, so
    // renderTarget lands exactly on the middle of the visible scene area.
    const auto &renderTargetSize = world.get<Rendering::RenderTargetSize>();
    mainCamera.value.offset = Vector2{
        renderTargetSize.dimension.x * 0.5f + Rendering::kSceneGuardPixels,
        renderTargetSize.dimension.y * 0.5f + Rendering::kSceneGuardPixels};
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
  const auto &logicalView = world.get<Rendering::LogicalViewSize>();
  const auto &mapBounds = world.get<MapManager::MapBounds>();
  const Vector2 mapSize = mapBounds.dimension;
  const Vector2 viewSize = logicalView.value;

  // --- FocusProxy: read character state to determine look-ahead ---
  // Query the entity tagged CameraFollowTag for its CharacterInfo to determine
  // motion state. Skip look-ahead if no such entity is found.
  {
    // We only update the proxy direction if the player is moving; otherwise
    // let distance retract slowly (handled inside UpdateFocusProxy).
    // Velocity is not directly accessible here without pulling in more headers,
    // so we pass a zero direction when idle  distance retracts on its own.
    // (See Movement.cpp for the richer version that feeds direction.)
    // For now, just retract toward idle each frame; the caller may replace this
    // with a richer UpdateFocusProxy call after integrating CharacterInfo.
    UpdateFocusProxy(camera.focus, Vector2{0.0f, 0.0f}, FollowMotion::Idle, dt);
  }

  // --- Smooth camera ---
  const Vector2 desired = ClampCameraToMap(
      Vector2Add(interpolatedPlayerPos, camera.focus.offset),
      viewSize,
      mapSize);

  const float blend = 1.0f - std::exp(-camera.followSpeed * dt);
  camera.smoothTarget = Vector2Lerp(camera.smoothTarget, desired, blend);
  camera.smoothTarget = ClampCameraToMap(camera.smoothTarget, viewSize, mapSize);

  // --- Quantise to the render grid, keeping the residual for the final blit ---
  if (camera.snapToRenderGrid) {
    const float ppwu = camera.pixelsPerWorldUnit;

    // How many output pixels one scene pixel becomes in the current window.
    const auto &sceneSize = world.get<Rendering::RenderTargetSize>().dimension;
    const bool integerOnly =
        world.get<Rendering::OutputSettings>().mode == Rendering::OutputScaleMode::Integer;
    const Rectangle outputRect = Rendering::ComputeOutputRect(
        GetScreenWidth(), GetScreenHeight(), sceneSize, integerOnly);
    const float outputScale = (sceneSize.y > 0.0f && outputRect.height > 0.0f)
                                  ? outputRect.height / sceneSize.y
                                  : 1.0f;
    const float outputSteps = std::max(1.0f, std::round(outputScale));

    // fine: finest grid the output can actually show (one output pixel).
    // coarse: what the scene buffer can render (one scene pixel).
    const Vector2 fine = Rendering::SnapToGrid(camera.smoothTarget, ppwu * outputSteps);
    const Vector2 unclamped = Rendering::SnapToGrid(fine, ppwu);
    Vector2 coarse = ClampCameraToMap(unclamped, viewSize, mapSize);
    // Second snap after clamp, in case the map boundary is not on a grid multiple.
    coarse = Rendering::SnapToGrid(coarse, ppwu);

    camera.renderTarget = coarse;

    // Translate the blit by the discarded remainder. Skipped while the camera is
    // held against a map edge: there the remainder is an artefact of clamping,
    // and shifting would pull guard pixels from outside the map into view. The
    // camera is not moving there either, so there is no jitter to compensate.
    const bool clampedToEdge = coarse.x != unclamped.x || coarse.y != unclamped.y;
    if (clampedToEdge) {
      camera.renderShift = {0.0f, 0.0f};
    } else {
      const Vector2 residual = Vector2Subtract(coarse, fine);
      const float guard = Rendering::kSceneGuardPixels * outputScale;
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
  const auto &logicalView = world.get<Rendering::LogicalViewSize>();
  const auto &mapBounds = world.get<MapManager::MapBounds>();

  const Vector2 clamped = [&] {
    const Vector2 halfView = Vector2Scale(logicalView.value, 0.5f);
    auto clampAxis = [](float v, float half, float extent) -> float {
      if (extent <= 0.0f)
        return v;
      if (extent < half * 2.0f)
        return extent * 0.5f;
      return std::clamp(v, half, extent - half);
    };
    return Vector2{
        clampAxis(focus.x, halfView.x, mapBounds.dimension.x),
        clampAxis(focus.y, halfView.y, mapBounds.dimension.y)};
  }();

  camera.smoothTarget = clamped;
  camera.renderTarget = camera.snapToRenderGrid
                            ? Rendering::SnapToGrid(clamped, camera.pixelsPerWorldUnit)
                            : clamped;
  camera.renderShift = {0.0f, 0.0f};
  camera.value.target = camera.renderTarget;

  camera.focus = FocusProxy{};
  camera.focus.direction = {0.0f, 1.0f};
}

module::module(flecs::world &world) {
  Reflection::Register<Camera2D>(world);
  Reflection::Register<MainCamera>(world)
      .add(flecs::Singleton)
      .set<MainCamera>({});
}

} // namespace GameCamera

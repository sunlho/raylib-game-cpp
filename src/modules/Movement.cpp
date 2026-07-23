
#include <algorithm>
#include <cmath>
#include <iostream>

#include "raylib.h"
#include "raymath.h"

#include "Camera.h"
#include "Character/Character.h"
#include "Console/Console.h"
#include "Map/Map.h"
#include "Movement.h"
#include "Physics.h"
#include "Reflection.h"
#include "Rendering.h"
#include "Simulation.h"

namespace Movement {
namespace {

constexpr float MinimumCameraZoom = 1.0e-4f;

float CameraPixelScale(float zoom) {
  return std::max(std::fabs(zoom), MinimumCameraZoom);
}

float SnapToScreenPixel(float value, float pixelScale) {
  return roundf(value * pixelScale) / pixelScale;
}

float ClampAxisToBounds(float value, float halfExtent, float mapExtent) {
  if (mapExtent <= 0.0f) {
    return value;
  }

  const float minValue = halfExtent;
  const float maxValue = mapExtent - halfExtent;
  if (maxValue < minValue) {
    return mapExtent * 0.5f;
  }

  return std::clamp(value, minValue, maxValue);
}

Vector2 SmoothCameraTarget(Vector2 current, Vector2 desired, float followSpeed, float deltaTime) {
  if (followSpeed <= 0.0f || deltaTime <= 0.0f) {
    return desired;
  }

  const float blend = 1.0f - std::exp(-followSpeed * deltaTime);
  return Vector2Lerp(current, desired, std::clamp(blend, 0.0f, 1.0f));
}

} // namespace

module::module(flecs::world &world) {
  Reflection::Register<Velocity>(world);
  Reflection::Register<MoveSpeed>(world);
  Reflection::Register<RunSettings>(world);
  Reflection::Register<RunState>(world);

  world.system<Velocity, const MoveSpeed, const RunSettings, RunState>("Update Player Input")
      .kind<Movement::Phases::Update>()
      .with<PlayerControlled>()
      .each([](flecs::iter &it, size_t, Velocity &velocity, const MoveSpeed &speed, const RunSettings &runSettings, RunState &runState) {
        if (GameConsole::IsOpen(it.world())) {
          velocity.value = Vector2{0.0f, 0.0f};
          runState = {};
          return;
        }

        Vector2 direction = {
            static_cast<float>(IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) -
                static_cast<float>(IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)),
            static_cast<float>(IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) -
                static_cast<float>(IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))};

        const bool isMoving = Vector2LengthSqr(direction) > 0.0f;
        if (isMoving) {
          direction = Vector2Normalize(direction);
        }

        runState.active = isMoving && (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));

        if (runState.active) {
          const float accelerationTime = std::max(0.0f, runSettings.accelerationTime);
          runState.progress =
              accelerationTime > 0.0f
                  ? std::min(1.0f, runState.progress + it.delta_time() / accelerationTime)
                  : 1.0f;
        } else {
          runState.progress = 0.0f;
        }

        const float runMultiplier = std::max(1.0f, runSettings.speedMultiplier);
        const float accelerationCurve = std::sqrt(runState.progress);
        const float currentMultiplier = 1.0f + (runMultiplier - 1.0f) * accelerationCurve;
        velocity.value = Vector2Scale(direction, speed.value * currentMultiplier);
      });

  world.system<Rendering::Position, const Character::SpriteSet, const Character::AnimationController, Rendering::RenderComponent, const Physics::PhysicsBody>("Clamp Player To Map Bounds")
      .kind<Simulation::FixedUpdate>()
      .with<PlayerControlled>()
      .each([](flecs::iter &it, size_t, Rendering::Position &position, const Character::SpriteSet &spriteSet, const Character::AnimationController &controller, Rendering::RenderComponent &renderComponent, const Physics::PhysicsBody &physicsBody) {
        const auto &mapBounds = it.world().get<MapManager::MapBounds>();
        const Vector2 halfExtents = Character::GetSpriteHalfExtents(spriteSet, controller);

        const Vector2 clampedPosition = {
            ClampAxisToBounds(position.value.x, halfExtents.x, mapBounds.dimension.x),
            ClampAxisToBounds(position.value.y, halfExtents.y, mapBounds.dimension.y)};

        if (clampedPosition.x != position.value.x || clampedPosition.y != position.value.y) {
          Physics::Relocate(physicsBody, clampedPosition);
        }

        position.value = clampedPosition;

        renderComponent.sortY = position.value.y + halfExtents.y;
      });

  world.system<const Rendering::Position>("Follow Camera Target")
      .with<CameraFollowTag>()
      .kind<Movement::Phases::CameraFollow>()
      .each([](flecs::iter &it, size_t i, const Rendering::Position &position) {
        auto world = it.world();
        auto &mainCamera = world.get_mut<GameCamera::MainCamera>();
        const auto mapBounds = world.get<MapManager::MapBounds>();
        const auto renderTargetSize = world.get<Rendering::RenderTargetSize>();
        const bool snapTargetToPixel = mainCamera.snapTargetToPixel;
        const float pixelScale = CameraPixelScale(mainCamera.value.zoom);

        const Vector2 viewportHalf = Vector2{
            renderTargetSize.dimension.x * 0.5f / pixelScale,
            renderTargetSize.dimension.y * 0.5f / pixelScale};

        const Vector2 desiredTarget = Vector2Add(position.value, mainCamera.followOffset);
        Vector2 clampedDesiredTarget = desiredTarget;
        clampedDesiredTarget.x = ClampAxisToBounds(clampedDesiredTarget.x, viewportHalf.x, mapBounds.dimension.x);
        clampedDesiredTarget.y = ClampAxisToBounds(clampedDesiredTarget.y, viewportHalf.y, mapBounds.dimension.y);

        Vector2 target = SmoothCameraTarget(
            mainCamera.value.target,
            clampedDesiredTarget,
            mainCamera.followSpeed,
            it.delta_time());
        target.x = ClampAxisToBounds(target.x, viewportHalf.x, mapBounds.dimension.x);
        target.y = ClampAxisToBounds(target.y, viewportHalf.y, mapBounds.dimension.y);

        if (snapTargetToPixel) {
          target = Vector2{
              SnapToScreenPixel(target.x, pixelScale),
              SnapToScreenPixel(target.y, pixelScale)};
          target.x = SnapToScreenPixel(ClampAxisToBounds(target.x, viewportHalf.x, mapBounds.dimension.x), pixelScale);
          target.y = SnapToScreenPixel(ClampAxisToBounds(target.y, viewportHalf.y, mapBounds.dimension.y), pixelScale);
        }

        mainCamera.value.target = target;
        mainCamera.useFollowRenderPosition = snapTargetToPixel && mainCamera.enabled;
        const Vector2 playerFromCamera = Vector2Subtract(position.value, target);
        mainCamera.followRenderPosition = Vector2Add(
            target,
            Vector2{
                SnapToScreenPixel(playerFromCamera.x, pixelScale),
                SnapToScreenPixel(playerFromCamera.y, pixelScale)});
      });
}

} // namespace Movement

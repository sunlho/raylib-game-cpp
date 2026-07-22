
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

constexpr float DiagonalMovementEpsilon = 1.0e-4f;

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

bool IsEqualMagnitudeDiagonal(Vector2 delta) {
  const float absX = std::fabs(delta.x);
  const float absY = std::fabs(delta.y);
  const float magnitude = std::max(absX, absY);
  if (std::min(absX, absY) <= DiagonalMovementEpsilon) {
    return false;
  }

  return std::fabs(absX - absY) <=
         std::max(DiagonalMovementEpsilon, magnitude * 1.0e-3f);
}

float DirectionSign(float value) {
  return value < 0.0f ? -1.0f : 1.0f;
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

        runState.active = isMoving &&
                          (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));

        if (runState.active) {
          const float accelerationTime = std::max(0.0f, runSettings.accelerationTime);
          runState.progress = accelerationTime > 0.0f
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

        const Vector2 viewportHalf = Vector2{
            renderTargetSize.dimension.x * 0.5f,
            renderTargetSize.dimension.y * 0.5f};

        const Vector2 desiredTarget = Vector2Add(position.value, mainCamera.followOffset);
        Vector2 target = desiredTarget;
        target.x = ClampAxisToBounds(target.x, viewportHalf.x, mapBounds.dimension.x);
        target.y = ClampAxisToBounds(target.y, viewportHalf.y, mapBounds.dimension.y);
        const bool targetClampedX = target.x != desiredTarget.x;
        const bool targetClampedY = target.y != desiredTarget.y;

        if (snapTargetToPixel) {
          Vector2 snappedTarget = {roundf(target.x), roundf(target.y)};

          if (mainCamera.hasPreviousFollowTarget) {
            const Vector2 followDelta = Vector2Subtract(target, mainCamera.previousFollowTarget);
            if (IsEqualMagnitudeDiagonal(followDelta)) {
              const Vector2 signs = {
                  DirectionSign(followDelta.x),
                  DirectionSign(followDelta.y)};
              const Vector2 remaining = Vector2Subtract(target, mainCamera.value.target);
              const float sharedStep = roundf(
                  (remaining.x * signs.x + remaining.y * signs.y) * 0.5f);
              snappedTarget = Vector2Add(
                  mainCamera.value.target,
                  Vector2Scale(signs, sharedStep));
            }
          }

          target = snappedTarget;
          target.x = roundf(ClampAxisToBounds(target.x, viewportHalf.x, mapBounds.dimension.x));
          target.y = roundf(ClampAxisToBounds(target.y, viewportHalf.y, mapBounds.dimension.y));
        }

        mainCamera.previousFollowTarget = Vector2{
            ClampAxisToBounds(desiredTarget.x, viewportHalf.x, mapBounds.dimension.x),
            ClampAxisToBounds(desiredTarget.y, viewportHalf.y, mapBounds.dimension.y)};
        mainCamera.hasPreviousFollowTarget = true;
        mainCamera.value.target = target;
        mainCamera.useFollowRenderPosition = snapTargetToPixel && mainCamera.enabled;
        mainCamera.followRenderPosition = Vector2{
            targetClampedX
                ? roundf(position.value.x)
                : roundf(target.x - mainCamera.followOffset.x),
            targetClampedY
                ? roundf(position.value.y)
                : roundf(target.y - mainCamera.followOffset.y)};
      });
}

} // namespace Movement

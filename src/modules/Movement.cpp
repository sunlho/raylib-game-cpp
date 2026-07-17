
#include <algorithm>
#include <iostream>

#include "box2d/box2d.h"
#include "raylib.h"
#include "raymath.h"

#include "Camera.h"
#include "Character/Character.h"
#include "Console/Console.h"
#include "Movement.h"
#include "Map/Map.h"
#include "Physics.h"
#include "Reflection.h"
#include "Rendering.h"
#include "Simulation.h"

namespace Movement {
namespace {

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

} // namespace

module::module(flecs::world &world) {
  Reflection::Register<Velocity>(world);
  Reflection::Register<MoveSpeed>(world);

  world.system<Velocity, const MoveSpeed>("Update Player Input")
      .kind<Movement::Phases::Update>()
      .with<PlayerControlled>()
      .each([](flecs::iter &it, size_t, Velocity &velocity, const MoveSpeed &speed) {
        if (GameConsole::IsOpen(it.world())) {
          velocity.value = Vector2{0.0f, 0.0f};
          return;
        }

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

  world.system<const Velocity, const Physics::PhysicsBody>("Apply Entity Velocity")
      .kind<Simulation::PrePhysics>()
      .each([](const Velocity &velocity, const Physics::PhysicsBody &physicsBody) {
        if (!b2Body_IsValid(physicsBody.id)) {
          return;
        }

        b2Body_SetLinearVelocity(physicsBody.id, b2Vec2{velocity.value.x, velocity.value.y});
      });

  world.system<Rendering::Position, const Physics::PhysicsBody>("Sync Physics Positions")
      .kind<Simulation::PostPhysics>()
      .each([](Rendering::Position &position, const Physics::PhysicsBody &physicsBody) {
        if (!b2Body_IsValid(physicsBody.id)) {
          return;
        }

        b2Vec2 bodyPosition = b2Body_GetPosition(physicsBody.id);
        position.value = Vector2{bodyPosition.x, bodyPosition.y};
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

        if ((clampedPosition.x != position.value.x || clampedPosition.y != position.value.y) &&
            b2Body_IsValid(physicsBody.id)) {
          b2Body_SetTransform(
              physicsBody.id,
              b2Vec2{clampedPosition.x, clampedPosition.y},
              b2Body_GetRotation(physicsBody.id));
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

        Vector2 target = Vector2Add(position.value, mainCamera.followOffset);

        if (snapTargetToPixel) {
          target.x = roundf(target.x);
          target.y = roundf(target.y);
        }

        const float deltaTime = it.delta_time();
        const float followAmount = 1.0f - expf(-mainCamera.followSpeed * deltaTime);
        const float lerpAmount = followAmount > 1.0f ? 1.0f : followAmount;
        mainCamera.value.target = Vector2Lerp(mainCamera.value.target, target, lerpAmount);

        const Vector2 viewportHalf = Vector2{
            renderTargetSize.dimension.x * 0.5f,
            renderTargetSize.dimension.y * 0.5f};

        mainCamera.value.target.x = ClampAxisToBounds(mainCamera.value.target.x, viewportHalf.x, mapBounds.dimension.x);
        mainCamera.value.target.y = ClampAxisToBounds(mainCamera.value.target.y, viewportHalf.y, mapBounds.dimension.y);

        if (snapTargetToPixel) {
          mainCamera.value.target.x = roundf(mainCamera.value.target.x);
          mainCamera.value.target.y = roundf(mainCamera.value.target.y);
        }
      });
}

} // namespace Movement

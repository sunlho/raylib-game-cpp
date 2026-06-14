
#include <algorithm>
#include <iostream>

#include "box2d/box2d.h"
#include "raylib.h"
#include "raymath.h"

#include "Camera.h"
#include "Character/Character.h"
#include "Movement.h"
#include "Physics.h"
#include "Reflection.h"
#include "Rendering.h"
#include "Simulation.h"
#include "Tilemap/Tilemap.h"

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
      .each([](Velocity &velocity, const MoveSpeed &speed) {
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

  // TODO: The physics position is not clamped to the map bounds
  world.system<Rendering::Position, const Velocity, const Physics::PhysicsBody>("Move Entities")
      .kind<Simulation::FixedUpdate>()
      .each([](flecs::iter &it, size_t i, Rendering::Position &position, const Velocity &velocity, const Physics::PhysicsBody &physicsBody) {
        const float deltaTime = it.delta_time();
        b2Body_SetLinearVelocity(physicsBody.id, b2Vec2{velocity.value.x, velocity.value.y});
        b2Vec2 bodyPosition = b2Body_GetPosition(physicsBody.id);
        position.value = Vector2{bodyPosition.x, bodyPosition.y};
      });

  world.system<Rendering::Position, const Character::SpriteSet, const Character::AnimationController, Rendering::RenderComponent>("Clamp Player To Map Bounds")
      .kind<Simulation::FixedUpdate>()
      .with<PlayerControlled>()
      .each([](flecs::iter &it, size_t, Rendering::Position &position, const Character::SpriteSet &spriteSet, const Character::AnimationController &controller, Rendering::RenderComponent &renderComponent) {
        const auto &mapBounds = it.world().get<Tilemap::MapBounds>();
        const Vector2 halfExtents = Character::GetSpriteHalfExtents(spriteSet, controller);

        position.value.x = ClampAxisToBounds(position.value.x, halfExtents.x, mapBounds.dimension.x);
        position.value.y = ClampAxisToBounds(position.value.y, halfExtents.y, mapBounds.dimension.y);

        renderComponent.sortY = Rendering::GetSortYByLayer(renderComponent.layerIndex, position.value.y + halfExtents.y);
      });

  world.system<const Rendering::Position>("Follow Camera Target")
      .with<CameraFollowTag>()
      .kind<Movement::Phases::CameraFollow>()
      .each([](flecs::iter &it, size_t i, const Rendering::Position &position) {
        auto world = it.world();
        auto mainCamera = world.singleton<GameCamera::MainCamera>();
        auto &cameraState = mainCamera.get_mut<GameCamera::CameraState>();
        const auto mapBounds = world.get<Tilemap::MapBounds>();
        const auto renderTargetSize = world.get<Rendering::RenderTargetSize>();
        const bool snapTargetToPixel = cameraState.snapTargetToPixel;

        Vector2 target = Vector2Add(position.value, cameraState.followOffset);

        if (snapTargetToPixel) {
          target.x = roundf(target.x);
          target.y = roundf(target.y);
        }

        const float deltaTime = it.delta_time();
        const float followAmount = 1.0f - expf(-cameraState.followSpeed * deltaTime);
        const float lerpAmount = followAmount > 1.0f ? 1.0f : followAmount;
        cameraState.value.target = Vector2Lerp(cameraState.value.target, target, lerpAmount);

        const Vector2 viewportHalf = Vector2{
            renderTargetSize.dimension.x * 0.5f,
            renderTargetSize.dimension.y * 0.5f};

        cameraState.value.target.x = ClampAxisToBounds(cameraState.value.target.x, viewportHalf.x, mapBounds.dimension.x);
        cameraState.value.target.y = ClampAxisToBounds(cameraState.value.target.y, viewportHalf.y, mapBounds.dimension.y);

        if (snapTargetToPixel) {
          cameraState.value.target.x = roundf(cameraState.value.target.x);
          cameraState.value.target.y = roundf(cameraState.value.target.y);
        }
      });
}

} // namespace Movement

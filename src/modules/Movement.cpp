
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

}

void UpdateCamera(flecs::world &world, float deltaTime) {
  const auto player = world.lookup("Player");
  if (!player.is_valid() || !player.has<Rendering::RenderPosition>()) return;
  auto &camera = world.get_mut<GameCamera::MainCamera>();
  const auto &settings = world.get<Rendering::RenderSettings>();
  const auto &mapBounds = world.get<MapManager::MapBounds>();
  Vector2 direction = camera.focus.direction;
  float distance = 0.0f;
  if (const auto *info = player.try_get<Character::CharacterInfo>()) {
    switch (info->direction) {
    case Character::CharacterDirection::Up: direction = {0.0f, -1.0f}; break;
    case Character::CharacterDirection::Down: direction = {0.0f, 1.0f}; break;
    case Character::CharacterDirection::Left: direction = {-1.0f, 0.0f}; break;
    case Character::CharacterDirection::Right: direction = {1.0f, 0.0f}; break;
    }
    if (info->state == Character::CharacterState::Moving) distance = 7.0f;
    if (info->state == Character::CharacterState::Attacking) distance = 3.0f;
  }
  if (const auto *run = player.try_get<RunState>(); run && run->active) distance = 10.0f;
  const auto frameBlend = [deltaTime](float factor) {
    return 1.0f - std::exp(std::log(1.0f - factor) * 60.0f * std::max(deltaTime, 0.0f));
  };
  camera.focus.direction = Vector2Normalize(Vector2Lerp(camera.focus.direction, direction, frameBlend(0.04f)));
  camera.focus.distance += (distance - camera.focus.distance) * frameBlend(distance > camera.focus.distance ? 0.10f : 0.02f);
  camera.focus.offset = Vector2Scale(camera.focus.direction, camera.focus.distance);
  const Vector2 half = Vector2Scale(settings.logicalViewSize, 0.5f);
  Vector2 desired = Vector2Add(player.get<Rendering::RenderPosition>().interpolated, camera.focus.offset);
  desired.x = ClampAxisToBounds(desired.x, half.x, mapBounds.dimension.x);
  desired.y = ClampAxisToBounds(desired.y, half.y, mapBounds.dimension.y);
  camera.smoothTarget = SmoothCameraTarget(camera.smoothTarget, desired, camera.followSpeed, deltaTime);
  camera.smoothTarget.x = ClampAxisToBounds(camera.smoothTarget.x, half.x, mapBounds.dimension.x);
  camera.smoothTarget.y = ClampAxisToBounds(camera.smoothTarget.y, half.y, mapBounds.dimension.y);
  const float steps = camera.pixelsPerWorldUnit;
  camera.renderTarget = camera.snapToRenderGrid && steps > 0.0f
      ? Vector2{std::round(camera.smoothTarget.x * steps) / steps, std::round(camera.smoothTarget.y * steps) / steps}
      : camera.smoothTarget;
}

} // namespace Movement

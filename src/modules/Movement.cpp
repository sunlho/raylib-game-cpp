#include "Movement.h"

#include <algorithm>

#include "Camera.h"
#include "Character.h"
#include "Reflection.h"
#include "Rendering.h"
#include "Tilemap/Tilemap.h"
#include "raymath.h"

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

Vector2 GetSpriteHalfExtents(const Character::SpriteSet &spriteSet, const Character::AnimationController &controller) {
  if (!spriteSet.loaded) {
    return Vector2{0.0f, 0.0f};
  }

  const Character::AnimationClip *clip = controller.GetCurrentAnimation();
  const Character::SpriteEntry *entry = clip ? spriteSet.FindEntry(clip->name) : nullptr;
  if (!entry && !spriteSet.entries.empty()) {
    entry = &spriteSet.entries.front();
  }

  if (!entry || entry->animation.width <= 0 || entry->animation.height <= 0) {
    return Vector2{0.0f, 0.0f};
  }

  return Vector2{
      static_cast<float>(entry->animation.width) * spriteSet.scale * 0.5f,
      static_cast<float>(entry->animation.height) * spriteSet.scale * 0.5f};
}
} // namespace

void Movement::Import(flecs::world &world) {

  Reflection::Register<Velocity>(world);
  Reflection::Register<MoveSpeed>(world);

  auto updatePhase = world.entity<Movement::Phases::Update>();
  auto boundsClampPhase = world.entity<Movement::Phases::BoundsClamp>();
  auto followPhase = world.entity<Movement::Phases::CameraFollow>();

  updatePhase
      .add(flecs::Phase)
      .depends_on(world.entity<Rendering::Phases::PreDraw>());

  boundsClampPhase
      .add(flecs::Phase)
      .depends_on(updatePhase);

  followPhase
      .add(flecs::Phase)
      .depends_on(boundsClampPhase);

  world.entity<Rendering::Phases::Background>()
      .depends_on(followPhase);

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

  world.system<Rendering::Position, const Velocity>("Move Entities")
      .kind<Movement::Phases::Update>()
      .each([](flecs::iter &it, size_t i, Rendering::Position &position, const Velocity &velocity) {
        position.value = Vector2Add(position.value, Vector2Scale(velocity.value, it.delta_time()));
      });

  world.system<Rendering::Position, const Character::SpriteSet, const Character::AnimationController>("Clamp Player To Map Bounds")
      .kind<Movement::Phases::BoundsClamp>()
      .with<PlayerControlled>()
      .each([](flecs::iter &it, size_t, Rendering::Position &position, const Character::SpriteSet &spriteSet, const Character::AnimationController &controller) {
        auto mapBoundsEntity = it.world().singleton<Tilemap::MapBounds>();
        const auto &mapBounds = mapBoundsEntity.get<Tilemap::MapBounds>();
        const Vector2 halfExtents = GetSpriteHalfExtents(spriteSet, controller);

        position.value.x = ClampAxisToBounds(position.value.x, halfExtents.x, mapBounds.dimension.x);
        position.value.y = ClampAxisToBounds(position.value.y, halfExtents.y, mapBounds.dimension.y);
      });

  world.system<const Rendering::Position>("Follow Camera Target")
      .with<CameraFollowTag>()
      .kind<Movement::Phases::CameraFollow>()
      .each([](flecs::iter &it, size_t i, const Rendering::Position &position) {
        auto world = it.world();
        auto mainCamera = world.singleton<GameCamera::MainCamera>();
        auto &cameraState = mainCamera.get_mut<GameCamera::CameraState>();
        auto mapBoundsEntity = world.singleton<Tilemap::MapBounds>();
        const auto &mapBounds = mapBoundsEntity.get<Tilemap::MapBounds>();
        auto renderTargetSizeEntity = world.singleton<Rendering::RenderTargetSize>();
        const auto &renderTargetSize = renderTargetSizeEntity.get<Rendering::RenderTargetSize>();
        const bool snapTargetToPixel = cameraState.snapTargetToPixel;

        Vector2 target = Vector2Add(position.value, cameraState.followOffset);

        if (snapTargetToPixel) {
          target.x = roundf(target.x);
          target.y = roundf(target.y);
        }

        if (snapTargetToPixel || cameraState.followSpeed <= 0.0f) {
          cameraState.value.target = target;
        } else {
          const float deltaTime = it.delta_time();
          const float followAmount = 1.0f - expf(-cameraState.followSpeed * deltaTime);
          const float lerpAmount = followAmount > 1.0f ? 1.0f : followAmount;
          cameraState.value.target = Vector2Lerp(cameraState.value.target, target, lerpAmount);
        }

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

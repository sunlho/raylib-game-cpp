#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>

#include "Map.h"
#include "MapInternal.h"

#include "modules/Assets.h"
#include "modules/Camera.h"
#include "modules/Character/Character.h"
#include "modules/Core/Core.h"
#include "modules/Movement.h"
#include "modules/Physics.h"
#include "modules/Reflection.h"
#include "modules/Stairs/Stairs.h"
#include "modules/Tilemap/Tilemap.h"

namespace MapManager {
namespace {

using Internal::LifecycleState;

OperationKind KindOf(const Operation &operation) {
  return std::visit(
      [](const auto &value) -> OperationKind {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, MapTransition>)
          return OperationKind::MapTransition;
        if constexpr (std::is_same_v<T, MapReload>)
          return OperationKind::MapReload;
        return OperationKind::SpawnTravel;
      },
      operation);
}

void Fail(LifecycleState &state, Status &status, FailureCode code, std::string message) {
  status.phase = Phase::Failed;
  status.hint = message;
  status.failure = Failure{code, std::move(message)};
  status.progress = 0.0f;
  state.pending.reset();
  state.candidate = nullptr;
  state.preservedPlayer.reset();
}

void Succeed(LifecycleState &state, Status &status, std::string hint) {
  status.phase = Phase::Succeeded;
  status.progress = 1.0f;
  status.hint = std::move(hint);
  status.failure.reset();
  state.pending.reset();
  state.candidate = nullptr;
  state.preservedPlayer.reset();
}

bool NormalizeMapPath(const std::string &input, std::string &normalized, Failure &failure) {
  std::filesystem::path path{input};
  if (path.empty() || path.is_absolute()) {
    failure = {FailureCode::InvalidPath, "Map path must be relative to the assets directory"};
    return false;
  }
  for (const auto &part : path) {
    if (part == "..") {
      failure = {FailureCode::InvalidPath, "Map path must stay inside the assets directory"};
      return false;
    }
  }

  if (!path.has_extension())
    path += ".tmx";
  path = path.lexically_normal();
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  if (extension != ".tmx") {
    failure = {FailureCode::InvalidPath, "Map must be a .tmx file"};
    return false;
  }

  normalized = path.generic_string();
  if (!Assets::Exists(normalized)) {
    failure = {FailureCode::MapNotFound, "Map not found in assets: " + normalized};
    return false;
  }
  return true;
}

bool IsInsideMap(Vector2 position, Vector2 dimensions) {
  return std::isfinite(position.x) && std::isfinite(position.y) &&
         position.x >= 0.0f && position.y >= 0.0f &&
         position.x <= dimensions.x && position.y <= dimensions.y;
}

bool ValidateMap(const Tilemap::LoadedMap &map, Failure &failure) {
  if (!map.validationErrors.empty()) {
    failure = {FailureCode::InvalidMap, map.validationErrors.front()};
    return false;
  }
  if (!map.textureBank || map.chunks.empty() || map.tileWidth <= 0 || map.tileHeight <= 0 ||
      map.chunkPixelWidth <= 0 || map.chunkPixelHeight <= 0 ||
      !std::isfinite(map.dimensions.x) || !std::isfinite(map.dimensions.y) ||
      map.dimensions.x <= 0.0f || map.dimensions.y <= 0.0f) {
    failure = {FailureCode::InvalidMap, "Map structure or gameplay dimensions are invalid"};
    return false;
  }

  std::unordered_set<std::string> names;
  std::size_t defaultCount = 0;
  for (const auto &chunk : map.chunks) {
    for (const auto &tile : chunk.tiles) {
      const auto *tileObject = map.textureBank->getTile(tile.tileGid);
      if (!tileObject || tileObject->texturePath.empty()) {
        failure = {FailureCode::InvalidMap, "Every referenced tile must resolve to a texture"};
        return false;
      }
    }
  }
  for (const auto &spawn : map.spawnPoints) {
    if (spawn.name.empty() || !names.insert(spawn.name).second ||
        !spawn.floor.has_value() || !std::isfinite(*spawn.floor) ||
        !spawn.direction.has_value() || !IsInsideMap(spawn.position, map.dimensions)) {
      failure = {FailureCode::InvalidSpawnSet,
                 "Spawn names must be unique and every spawn must declare a valid position, floor and direction"};
      return false;
    }
    if (spawn.isDefault)
      ++defaultCount;
  }
  if (defaultCount != 1) {
    failure = {FailureCode::InvalidSpawnSet, "Map must define exactly one default spawn point"};
    return false;
  }
  return true;
}

const Tilemap::SpawnPoint *FindSpawn(const std::vector<Tilemap::SpawnPoint> &spawns, const SpawnTarget &target) {
  if (std::holds_alternative<DefaultSpawn>(target)) {
    const auto it = std::find_if(spawns.begin(), spawns.end(), [](const Tilemap::SpawnPoint &spawn) {
      return spawn.isDefault;
    });
    return it == spawns.end() ? nullptr : &*it;
  }

  const auto &name = std::get<NamedSpawn>(target).name;
  const auto it = std::find_if(spawns.begin(), spawns.end(), [&name](const Tilemap::SpawnPoint &spawn) {
    return spawn.name == name;
  });
  return it == spawns.end() ? nullptr : &*it;
}

Character::CharacterDirection ToCharacterDirection(Tilemap::SpawnDirection direction) {
  switch (direction) {
  case Tilemap::SpawnDirection::Up:
    return Character::CharacterDirection::Up;
  case Tilemap::SpawnDirection::Left:
    return Character::CharacterDirection::Left;
  case Tilemap::SpawnDirection::Right:
    return Character::CharacterDirection::Right;
  case Tilemap::SpawnDirection::Down:
  default:
    return Character::CharacterDirection::Down;
  }
}

const char *IdleAnimation(Character::CharacterDirection direction) {
  switch (direction) {
  case Character::CharacterDirection::Up:
    return "idle-N";
  case Character::CharacterDirection::Left:
    return "idle-W";
  case Character::CharacterDirection::Right:
    return "idle-E";
  case Character::CharacterDirection::Down:
  default:
    return "idle-S";
  }
}

void ApplyPlayerState(
    flecs::world &world,
    Vector2 destination,
    float floor,
    Character::CharacterDirection direction) {
  const auto player = world.lookup("Player");
  player.get_mut<Core::Position>().value = destination;
  if (player.has<Movement::Velocity>())
    player.get_mut<Movement::Velocity>().value = Vector2{};
  if (const auto *body = player.try_get<Physics::PhysicsBody>()) {
    Physics::Relocate(*body, destination, true);
    const float yOffset = (floor - 1.5f) * 10.0f;
    Physics::SetCircleCenter(*body, Vector2{0.0f, yOffset});
  }
  if (player.has<Core::PreviousPosition>())
    player.get_mut<Core::PreviousPosition>().value = destination;
  if (auto *floorState = player.try_get_mut<Stairs::FloorState>()) {
    floorState->floor = floor;
    floorState->baseFloor = floor;
    floorState->onStair = false;
    floorState->currentStair = 0;
    floorState->overlappingStairs.clear();
  }
  if (auto *character = player.try_get_mut<Character::CharacterInfo>()) {
    character->state = Character::CharacterState::Idle;
    character->direction = direction;
  }
  if (auto *controller = player.try_get_mut<Character::AnimationController>()) {
    controller->PlayAnimation(IdleAnimation(direction), true);
  }
  if (auto *renderable = player.try_get_mut<Rendering::RenderComponent>()) {
    renderable->floor = floor;
    if (const auto *sprites = player.try_get<Character::SpriteSet>()) {
      if (const auto *controller = player.try_get<Character::AnimationController>()) {
        renderable->sortY = static_cast<int>(destination.y + Character::GetSpriteHalfExtents(*sprites, *controller).y);
      }
    }
  }

  GameCamera::SnapCameraTo(world, destination);
  if (auto *render = player.try_get_mut<Core::RenderPosition>()) {
    const auto &camera = world.get<GameCamera::MainCamera>();
    render->interpolated = destination;
    render->quantized = Core::QuantizeForCamera(destination, camera.renderTarget, camera.pixelsPerWorldUnit);
  }
}

bool CapturePlayer(flecs::world &world, Internal::PreservedPlayerState &preserved) {
  const auto player = world.lookup("Player");
  if (!player.is_valid() || !player.has<Core::Position>() ||
      !player.has<Stairs::FloorState>() || !player.has<Character::CharacterInfo>()) {
    return false;
  }
  preserved.position = player.get<Core::Position>().value;
  preserved.floor = player.get<Stairs::FloorState>().floor;
  preserved.direction = static_cast<int>(player.get<Character::CharacterInfo>().direction);
  return true;
}

void EvictCachedMap(Internal::MapCacheState &cache, const std::string &path) {
  const auto found = cache.cache.find(path);
  if (found == cache.cache.end())
    return;
  cache.usageOrder.erase(found->second.lruIt);
  cache.cache.erase(found);
}

void Acquire(LifecycleState &state, Status &status, flecs::world &world) {
  Failure failure;
  const auto &operation = *state.pending;
  const auto &current = world.get<Internal::MapState>();

  if (const auto *transition = std::get_if<MapTransition>(&operation)) {
    if (!NormalizeMapPath(transition->path, state.targetPath, failure)) {
      Fail(state, status, failure.code, std::move(failure.message));
      return;
    }
    state.targetSpawn = transition->destination;
    if (state.targetPath == current.currentPath && current.mapRoot.is_valid()) {
      status.operation = OperationKind::SpawnTravel;
      status.hint = "Resolving spawn point...";
      state.step = Internal::LifecycleStep::Commit;
      return;
    }
  } else if (std::holds_alternative<MapReload>(operation)) {
    if (current.currentPath.empty() || !current.mapRoot.is_valid()) {
      Fail(state, status, FailureCode::NoCurrentMap, "No map is currently loaded");
      return;
    }
    state.targetPath = current.currentPath;
    Internal::PreservedPlayerState preserved;
    if (!CapturePlayer(world, preserved)) {
      Fail(state, status, FailureCode::PlayerUnavailable, "Player spatial state is unavailable");
      return;
    }
    state.preservedPlayer = preserved;
  } else {
    if (current.currentPath.empty() || !current.mapRoot.is_valid()) {
      Fail(state, status, FailureCode::NoCurrentMap, "No map is currently loaded");
      return;
    }
    state.targetPath = current.currentPath;
    state.targetSpawn = std::get<SpawnTravel>(operation).destination;
    state.step = Internal::LifecycleStep::Commit;
    status.hint = "Resolving spawn point...";
    return;
  }

  Internal::PreservedPlayerState playerCheck;
  if (!CapturePlayer(world, playerCheck)) {
    Fail(state, status, FailureCode::PlayerUnavailable, "Player is unavailable");
    return;
  }

  auto &cache = world.get_mut<Internal::MapCacheState>();
  if (std::holds_alternative<MapReload>(operation))
    EvictCachedMap(cache, state.targetPath);
  state.candidate = Internal::GetOrLoadMap(cache, state.targetPath);
  if (!state.candidate) {
    Fail(state, status, FailureCode::ParseFailed, "Failed to parse map: " + state.targetPath);
    return;
  }
  if (!ValidateMap(*state.candidate, failure)) {
    Fail(state, status, failure.code, std::move(failure.message));
    return;
  }

  if (state.preservedPlayer) {
    const int direction = state.preservedPlayer->direction;
    if (!IsInsideMap(state.preservedPlayer->position, state.candidate->dimensions) ||
        !std::isfinite(state.preservedPlayer->floor) || direction < 0 || direction > 3) {
      Fail(state, status, FailureCode::PreservedSpatialStateInvalid, "The preserved player position, floor or direction is invalid for the reloaded map");
      return;
    }
  } else if (!FindSpawn(state.candidate->spawnPoints, state.targetSpawn)) {
    Fail(state, status, FailureCode::SpawnNotFound, "Target spawn point was not found on map: " + state.targetPath);
    return;
  }

  state.step = Internal::LifecycleStep::Preload;
  status.progress = 0.45f;
  status.hint = "Preloading map textures...";
}

void Preload(LifecycleState &state, Status &status) {
  std::string failedTexture;
  if (!Internal::PreloadMapTextures(*state.candidate, failedTexture)) {
    Fail(state, status, FailureCode::TextureUnavailable, "Failed to load map texture: " + failedTexture);
    return;
  }
  state.step = Internal::LifecycleStep::Commit;
  status.progress = 0.8f;
  status.hint = "Committing map...";
}

void Commit(LifecycleState &state, Status &status, flecs::world &world) {
  if (world.is_deferred())
    return;

  if (state.candidate) {
    Internal::CommitLoadedMap(world, *state.candidate, state.targetPath);
  }

  if (state.preservedPlayer) {
    ApplyPlayerState(
        world,
        state.preservedPlayer->position,
        state.preservedPlayer->floor,
        static_cast<Character::CharacterDirection>(state.preservedPlayer->direction));
  } else {
    const auto &spawns =
        state.candidate
            ? state.candidate->spawnPoints
            : world.get<Internal::ActiveMapData>().spawnPoints;
    const auto *spawn = FindSpawn(spawns, state.targetSpawn);
    if (!spawn || !spawn->floor || !spawn->direction) {
      Fail(state, status, FailureCode::SpawnNotFound, "Target spawn point is unavailable");
      return;
    }
    ApplyPlayerState(world, spawn->position, *spawn->floor, ToCharacterDirection(*spawn->direction));
  }

  status.currentMapPath = world.get<Internal::MapState>().currentPath;
  Succeed(state, status, "Map operation completed");
}

} // namespace

module::module(flecs::world &world) {
  Reflection::Register<Tilemap::CollisionData>(world);
  Reflection::Register<Tilemap::SpawnPoint>(world);
  Reflection::Register<Internal::ActiveMapData>(world).add(flecs::Singleton).set<Internal::ActiveMapData>({});
  Reflection::Register<Internal::MapCacheState>(world).add(flecs::Singleton).set<Internal::MapCacheState>({});
  Reflection::Register<Internal::MapState>(world).add(flecs::Singleton).set<Internal::MapState>({});
  world.component<Internal::LifecycleState>().add(flecs::Singleton).set<Internal::LifecycleState>({});
  world.component<Status>().add(flecs::Singleton).set<Status>({});
  Internal::RegisterMapRendering(world);
}

Receipt Submit(flecs::world &world, Operation operation) {
  auto &state = world.get_mut<Internal::LifecycleState>();
  auto &status = world.get_mut<Status>();
  if (status.phase == Phase::Running)
    return {Admission::Busy, status.id};

  const OperationId id = state.nextId++;
  status = Status{
      id,
      KindOf(operation),
      Phase::Running,
      0.05f,
      "Preparing map operation...",
      world.get<Internal::MapState>().currentPath,
      std::nullopt,
  };
  state.pending = std::move(operation);
  state.step = Internal::LifecycleStep::Acquire;
  state.targetPath.clear();
  state.targetSpawn = DefaultSpawn{};
  state.candidate = nullptr;
  state.preservedPlayer.reset();
  world.modified<Status>();

  return {Admission::Accepted, id};
}

void AdvanceLifecycle(flecs::world &world) {
  if (world.is_deferred())
    return;
  auto &state = world.get_mut<Internal::LifecycleState>();
  auto &status = world.get_mut<Status>();
  if (status.phase != Phase::Running || !state.pending)
    return;
  switch (state.step) {
  case Internal::LifecycleStep::Acquire:
    Acquire(state, status, world);
    break;
  case Internal::LifecycleStep::Preload:
    Preload(state, status);
    break;
  case Internal::LifecycleStep::Commit:
    Commit(state, status, world);
    break;
  }
  world.modified<Status>();
}

} // namespace MapManager

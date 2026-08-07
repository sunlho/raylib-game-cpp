#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "flecs.h"
namespace MapManager {

using OperationId = std::uint64_t;

struct DefaultSpawn {};
struct NamedSpawn {
  std::string name;
};
using SpawnTarget = std::variant<DefaultSpawn, NamedSpawn>;

struct MapTransition {
  std::string path;
  SpawnTarget destination = DefaultSpawn{};
};
struct MapReload {};
struct SpawnTravel {
  SpawnTarget destination = DefaultSpawn{};
};
using Operation = std::variant<MapTransition, MapReload, SpawnTravel>;

enum class Admission { Accepted, Busy };
struct Receipt {
  Admission admission = Admission::Busy;
  OperationId id = 0;
};

enum class OperationKind { None, MapTransition, MapReload, SpawnTravel };
enum class Phase { Idle, Running, Succeeded, Failed };
enum class FailureCode {
  NoCurrentMap,
  PlayerUnavailable,
  InvalidPath,
  MapNotFound,
  ParseFailed,
  InvalidMap,
  TextureUnavailable,
  InvalidSpawnSet,
  SpawnNotFound,
  PreservedSpatialStateInvalid,
};

struct Failure {
  FailureCode code = FailureCode::InvalidMap;
  std::string message;
};

struct Status {
  OperationId id = 0;
  OperationKind operation = OperationKind::None;
  Phase phase = Phase::Idle;
  float progress = 0.0f;
  std::string hint;
  std::string currentMapPath;
  std::optional<Failure> failure;
};

Receipt Submit(flecs::world &world, Operation operation);

// Registered by MapManager::module as a singleton. Read it with world.get<Status>()
// or react to changes with an OnSet observer.

// Composition-root hook. Call once per frame outside systems/observers so the
// final world mutation always happens outside Flecs deferred mode.
void AdvanceLifecycle(flecs::world &world);

struct module {
  module(flecs::world &world);
};

} // namespace MapManager

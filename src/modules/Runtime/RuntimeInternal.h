#pragma once

#include <memory>

#include "Runtime.h"

namespace Runtime::Internal {

struct Pipelines {
  ecs_entity_t movementUpdate = 0;
  ecs_entity_t prePhysics = 0;
  ecs_entity_t physicsStep = 0;
  ecs_entity_t postPhysics = 0;
  ecs_entity_t fixedGameplay = 0;
  ecs_entity_t characterUpdate = 0;
  ecs_entity_t cameraFollow = 0;
  ecs_entity_t drawBackground = 0;
  ecs_entity_t drawWorld = 0;
  ecs_entity_t drawSortedWorld = 0;
};

struct FramePreparation {
  bool loadingVisible = false;
  bool exitRequested = false;
};

class FrameAdapter {
public:
  virtual ~FrameAdapter() = default;

  virtual FramePreparation PrepareFrame(flecs::world &world, float frameTime, const FrameInput &input) = 0;
  virtual void PresentFrame(
      flecs::world &world,
      const Pipelines &pipelines,
      float frameTime,
      bool debugDraw,
      ecs_entity_t loadingRevealTarget) = 0;
};

class CoordinatorAccess {
public:
  static std::unique_ptr<Coordinator> CreateHeadless(flecs::world &world, ecs_entity_t restDequeue, Settings settings);
};

} // namespace Runtime::Internal

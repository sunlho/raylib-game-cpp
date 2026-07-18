#pragma once

#include <cstdint>
#include <memory>

#include "flecs.h"

namespace Runtime {

struct Settings {
  float fixedTimeStep = 1.0f / 60.0f;
  float maxFrameTime = 0.25f;
  std::uint32_t maxFixedStepsPerFrame = 8;
};

struct FrameInput {
  float frameTime = 0.0f;
  bool exitPressed = false;
  bool pauseSimulation = false;
  bool debugDraw = false;
};

struct FrameReport {
  float acceptedFrameTime = 0.0f;
  float droppedSimulationTime = 0.0f;
  float accumulator = 0.0f;
  float peakAccumulator = 0.0f;
  std::uint32_t fixedSteps = 0;
  std::uint64_t totalFixedSteps = 0;
  double totalDroppedSimulationTime = 0.0;
  std::uint64_t longFrameCount = 0;
  bool simulationPaused = false;
  bool frameTimeClamped = false;
  bool fixedStepLimitReached = false;
  bool exitRequested = false;
};

class Coordinator;

namespace Internal {

class FrameAdapter;

class CoordinatorAccess;

} // namespace Internal

class Coordinator {
public:
  explicit Coordinator(flecs::world &world, ecs_entity_t restDequeue = 0, Settings settings = {});
  ~Coordinator();

  Coordinator(const Coordinator &) = delete;
  Coordinator &operator=(const Coordinator &) = delete;
  Coordinator(Coordinator &&) noexcept;
  Coordinator &operator=(Coordinator &&) noexcept;

  void PrepareLoadedWorld(ecs_entity_t loadingRevealTarget = 0);
  FrameReport AdvanceFrame(FrameInput input);

private:
  struct Impl;

  Coordinator(flecs::world &world, ecs_entity_t restDequeue, Settings settings, std::unique_ptr<Internal::FrameAdapter> frameAdapter);

  friend class Internal::CoordinatorAccess;

  std::unique_ptr<Impl> impl_;
};

} // namespace Runtime

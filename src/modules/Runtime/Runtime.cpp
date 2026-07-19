#include "Runtime.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "RuntimeInternal.h"
#include "RuntimePhases.h"

namespace Runtime {
namespace {

template <typename Phase>
ecs_entity_t BuildPipeline(flecs::world &world) {
  return world.scope("Pipelines")
      .pipeline<Phase>()
      .with(flecs::System)
      .with<Phase>()
      .build()
      .id();
}

Internal::Pipelines BuildPipelines(flecs::world &world) {
  return Internal::Pipelines{
      BuildPipeline<Phases::MovementUpdate>(world),
      BuildPipeline<Phases::PrePhysics>(world),
      BuildPipeline<Phases::PhysicsStep>(world),
      BuildPipeline<Phases::PostPhysics>(world),
      BuildPipeline<Phases::FixedGameplay>(world),
      BuildPipeline<Phases::CharacterUpdate>(world),
      BuildPipeline<Phases::CameraFollow>(world),
      BuildPipeline<Phases::DrawBackground>(world),
      BuildPipeline<Phases::DrawWorld>(world),
      BuildPipeline<Phases::DrawSortedWorld>(world),
  };
}

void RunPipeline(flecs::world &world, ecs_entity_t pipeline, float deltaTime) {
  ecs_run_pipeline(world.c_ptr(), pipeline, deltaTime);
}

class HeadlessFrameAdapter final : public Internal::FrameAdapter {
public:
  Internal::FramePreparation PrepareFrame(flecs::world &, float, const FrameInput &input) override {
    return Internal::FramePreparation{false, input.exitPressed};
  }

  void PresentFrame(flecs::world &, const Internal::Pipelines &, float, bool, ecs_entity_t) override {
  }
};

} // namespace

struct Coordinator::Impl {
  flecs::world &world;
  ecs_entity_t restDequeue;
  Settings settings;
  Internal::Pipelines pipelines;
  std::unique_ptr<Internal::FrameAdapter> frameAdapter;
  double accumulator = 0.0;
  double peakAccumulator = 0.0;
  std::uint64_t totalFixedSteps = 0;
  double totalDroppedSimulationTime = 0.0;
  std::uint64_t longFrameCount = 0;
  ecs_entity_t loadingRevealTarget = 0;
};

Coordinator::Coordinator(flecs::world &world, ecs_entity_t restDequeue, Settings settings, std::unique_ptr<Internal::FrameAdapter> frameAdapter) {
  if (!std::isfinite(settings.fixedTimeStep) || settings.fixedTimeStep <= 0.0f) {
    throw std::invalid_argument("Runtime fixedTimeStep must be finite and positive");
  }
  if (!std::isfinite(settings.maxFrameTime) || settings.maxFrameTime <= 0.0f) {
    throw std::invalid_argument("Runtime maxFrameTime must be finite and positive");
  }
  if (settings.maxFixedStepsPerFrame == 0) {
    throw std::invalid_argument("Runtime maxFixedStepsPerFrame must be positive");
  }
  if (!frameAdapter) {
    throw std::invalid_argument("Runtime frame adapter is required");
  }

  impl_ = std::make_unique<Impl>(Impl{
      world,
      restDequeue,
      settings,
      BuildPipelines(world),
      std::move(frameAdapter),
  });
}

Coordinator::~Coordinator() = default;
Coordinator::Coordinator(Coordinator &&) noexcept = default;
Coordinator &Coordinator::operator=(Coordinator &&) noexcept = default;

void Coordinator::PrepareLoadedWorld(ecs_entity_t loadingRevealTarget) {
  RunPipeline(impl_->world, impl_->pipelines.characterUpdate, 0.0f);
  impl_->accumulator = 0.0;
  impl_->loadingRevealTarget = loadingRevealTarget;
}

FrameReport Coordinator::AdvanceFrame(FrameInput input) {
  const float nonNegativeFrameTime = std::isfinite(input.frameTime) ? std::max(input.frameTime, 0.0f) : 0.0f;
  const float acceptedFrameTime = std::min(nonNegativeFrameTime, impl_->settings.maxFrameTime);

  FrameReport report;
  report.acceptedFrameTime = acceptedFrameTime;
  report.frameTimeClamped = nonNegativeFrameTime > acceptedFrameTime;

  const Internal::FramePreparation preparation = impl_->frameAdapter->PrepareFrame(impl_->world, acceptedFrameTime, input);
  report.exitRequested = preparation.exitRequested;
  if (report.exitRequested) {
    report.accumulator = static_cast<float>(impl_->accumulator);
    report.peakAccumulator = static_cast<float>(impl_->peakAccumulator);
    report.totalFixedSteps = impl_->totalFixedSteps;
    report.totalDroppedSimulationTime = impl_->totalDroppedSimulationTime;
    report.longFrameCount = impl_->longFrameCount;
    return report;
  }

  if (report.frameTimeClamped) {
    ++impl_->longFrameCount;
  }

  report.simulationPaused = input.pauseSimulation || preparation.loadingVisible;
  ecs_frame_begin(impl_->world.c_ptr(), acceptedFrameTime);

  if (!report.simulationPaused) {
    RunPipeline(impl_->world, impl_->pipelines.movementUpdate, acceptedFrameTime);

    impl_->accumulator += static_cast<double>(acceptedFrameTime);
    impl_->peakAccumulator = std::max(impl_->peakAccumulator, impl_->accumulator);

    const double fixedTimeStep = static_cast<double>(impl_->settings.fixedTimeStep);
    const double fixedStepEpsilon = fixedTimeStep * 1.0e-6;
    while (impl_->accumulator + fixedStepEpsilon >= fixedTimeStep && report.fixedSteps < impl_->settings.maxFixedStepsPerFrame) {
      RunPipeline(impl_->world, impl_->pipelines.prePhysics, impl_->settings.fixedTimeStep);
      RunPipeline(impl_->world, impl_->pipelines.physicsStep, impl_->settings.fixedTimeStep);
      RunPipeline(impl_->world, impl_->pipelines.postPhysics, impl_->settings.fixedTimeStep);
      RunPipeline(impl_->world, impl_->pipelines.fixedGameplay, impl_->settings.fixedTimeStep);
      RunPipeline(impl_->world, impl_->pipelines.characterUpdate, impl_->settings.fixedTimeStep);

      impl_->accumulator -= fixedTimeStep;
      if (impl_->accumulator < 0.0) {
        impl_->accumulator = 0.0;
      }
      ++report.fixedSteps;
      ++impl_->totalFixedSteps;
    }

    double droppedSimulationTime =
        static_cast<double>(nonNegativeFrameTime - acceptedFrameTime);
    if (impl_->accumulator + fixedStepEpsilon >= fixedTimeStep) {
      report.fixedStepLimitReached = true;
      const double wholeSteps = std::floor(
          (impl_->accumulator + fixedStepEpsilon) / fixedTimeStep);
      const double droppedDebt = wholeSteps * fixedTimeStep;
      droppedSimulationTime += droppedDebt;
      impl_->accumulator = std::max(0.0, impl_->accumulator - droppedDebt);
    }

    report.droppedSimulationTime = static_cast<float>(droppedSimulationTime);
    impl_->totalDroppedSimulationTime += droppedSimulationTime;

    // Camera smoothing belongs to render time, independently of fixed physics steps.
    RunPipeline(impl_->world, impl_->pipelines.cameraFollow, acceptedFrameTime);
  }

  impl_->frameAdapter->PresentFrame(
      impl_->world,
      impl_->pipelines,
      acceptedFrameTime,
      input.debugDraw,
      impl_->loadingRevealTarget);

  if (impl_->restDequeue != 0) {
    ecs_run(impl_->world.c_ptr(), impl_->restDequeue, acceptedFrameTime, nullptr);
  }
  ecs_frame_end(impl_->world.c_ptr());

  report.accumulator = static_cast<float>(impl_->accumulator);
  report.peakAccumulator = static_cast<float>(impl_->peakAccumulator);
  report.totalFixedSteps = impl_->totalFixedSteps;
  report.totalDroppedSimulationTime = impl_->totalDroppedSimulationTime;
  report.longFrameCount = impl_->longFrameCount;
  return report;
}

namespace Internal {

std::unique_ptr<Coordinator> CoordinatorAccess::CreateHeadless(flecs::world &world, ecs_entity_t restDequeue, Settings settings) {
  return std::unique_ptr<Coordinator>(new Coordinator(
      world,
      restDequeue,
      settings,
      std::make_unique<HeadlessFrameAdapter>()));
}

} // namespace Internal
} // namespace Runtime

#include "Runtime.h"

#include <memory>

#include "raylib.h"

#include "RuntimeInternal.h"
#include "modules/Camera.h"
#include "modules/Console/Console.h"
#include "modules/Debug/DebugDraw.h"
#include "modules/Physics.h"
#include "modules/Rendering.h"

namespace Runtime {
namespace {

void RunPipeline(flecs::world &world, ecs_entity_t pipeline, float deltaTime) {
  ecs_run_pipeline(world.c_ptr(), pipeline, deltaTime);
}

void UpdateLoadingRevealCenter(flecs::world &world, ecs_entity_t target) {
  if (target == 0 || !ecs_is_alive(world.c_ptr(), target)) {
    return;
  }

  flecs::entity entity(world.c_ptr(), target);
  if (!entity.has<Rendering::Position>()) {
    return;
  }

  const auto &position = entity.get<Rendering::Position>();
  const auto &mainCamera = world.get<GameCamera::MainCamera>();
  Rendering::SetLoadingRevealCenter(world, GetWorldToScreen2D(position.value, mainCamera.value));
}

class RaylibFrameAdapter final : public Internal::FrameAdapter {
public:
  Internal::FramePreparation PrepareFrame(flecs::world &world, float frameTime, const FrameInput &input) override {
    const bool consoleWasOpen = GameConsole::IsOpen(world);
    if (!Rendering::IsLoadingScreenVisible(world)) {
      GameConsole::Update(world);
    }

    if (!consoleWasOpen && input.exitPressed) {
      return Internal::FramePreparation{false, true};
    }

    Rendering::UpdateLoadingScreen(world, frameTime);
    return Internal::FramePreparation{
        Rendering::IsLoadingScreenVisible(world),
        false,
    };
  }

  void PresentFrame(
      flecs::world &world,
      const Internal::Pipelines &pipelines,
      float frameTime,
      bool debugDraw,
      ecs_entity_t loadingRevealTarget) override {
    Rendering::BeginFrame(world);
    GameCamera::Begin2D(world);
    RunPipeline(world, pipelines.drawBackground, frameTime);
    RunPipeline(world, pipelines.drawWorld, frameTime);
    RunPipeline(world, pipelines.drawSortedWorld, frameTime);

    if (debugDraw) {
      Physics::DebugDraw(world);
      DebugDraw::ProcessDrawQueue();
    }

    GameCamera::End2D(world);
    UpdateLoadingRevealCenter(world, loadingRevealTarget);
    Rendering::PresentFrame(world);
    GameConsole::Draw(world);
    Rendering::EndFrame();
  }
};

} // namespace

Coordinator::Coordinator(flecs::world &world, ecs_entity_t restDequeue, Settings settings)
    : Coordinator(world, restDequeue, settings, std::make_unique<RaylibFrameAdapter>()) {
}

} // namespace Runtime

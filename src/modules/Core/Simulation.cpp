#include "Simulation.h"

#include "Core.h"
#include "modules/Character/Character.h"
#include "modules/Utils.h"

namespace Simulation {
namespace {

struct FixedTickPipelines {
  ecs_entity_t resolveMovement = 0;
  ecs_entity_t prePhysics = 0;
  ecs_entity_t physicsStep = 0;
  ecs_entity_t postPhysics = 0;
  ecs_entity_t postPhysicsEvents = 0;
  ecs_entity_t fixedUpdate = 0;
  ecs_entity_t fixedUpdateLate = 0;
  ecs_entity_t characterUpdate = 0;
};

FixedTickPipelines BuildPipelines(flecs::world &world) {
  return {
      buildPipeline<ResolveMovement>(world).id(),
      buildPipeline<PrePhysics>(world).id(),
      buildPipeline<PhysicsStep>(world).id(),
      buildPipeline<PostPhysics>(world).id(),
      buildPipeline<PostPhysicsEvents>(world).id(),
      buildPipeline<FixedUpdate>(world).id(),
      buildPipeline<FixedUpdateLate>(world).id(),
      buildPipeline<Character::Phases::Update>(world).id()};
}

const FixedTickPipelines &GetPipelines(flecs::world &world) {
  if (!world.has<FixedTickPipelines>()) {
    world.set<FixedTickPipelines>(BuildPipelines(world));
  }
  return world.get<FixedTickPipelines>();
}

} // namespace

void RunFixedTick(flecs::world &world, float deltaTime) {
  const FixedTickPipelines &pipelines = GetPipelines(world);

  world.each([](const Core::Position &position, Core::PreviousPosition &previous) {
    previous.value = position.value;
  });

  ecs_run_pipeline(world, pipelines.resolveMovement, deltaTime);
  ecs_run_pipeline(world, pipelines.prePhysics, deltaTime);
  ecs_run_pipeline(world, pipelines.physicsStep, deltaTime);
  ecs_run_pipeline(world, pipelines.postPhysics, deltaTime);
  ecs_run_pipeline(world, pipelines.postPhysicsEvents, deltaTime);
  ecs_run_pipeline(world, pipelines.fixedUpdate, deltaTime);
  ecs_run_pipeline(world, pipelines.fixedUpdateLate, deltaTime);
  ecs_run_pipeline(world, pipelines.characterUpdate, deltaTime);
}

} // namespace Simulation

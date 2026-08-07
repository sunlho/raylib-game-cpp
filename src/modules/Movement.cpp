
#include <algorithm>
#include <cmath>

#include "Movement.h"
#include "raymath.h"

#include "Core/Simulation.h"
#include "Input.h"
#include "Reflection.h"

namespace Movement {
namespace {

struct RunState {
  float progress = 0.0f;
};

} // namespace

module::module(flecs::world &world) {
  Reflection::Register<RequestedVelocity>(world);
  Reflection::Register<PlayerMovementSettings>(world);
  world.component<RunState>();

  world.system<const PlayerMovementSettings, RequestedVelocity, RunState>("Resolve Player Movement")
      .kind<Simulation::ResolveMovement>()
      .each([](flecs::iter &it, size_t, const PlayerMovementSettings &settings, RequestedVelocity &velocity, RunState &runState) {
        const auto &intent = it.world().get<Input::PlayerControlIntent>();
        Vector2 movement = intent.movement;
        const float magnitude = Vector2Length(movement);
        if (magnitude > 1.0f) {
          movement = Vector2Scale(movement, 1.0f / magnitude);
        }

        const bool isMoving = Vector2LengthSqr(movement) > 1.0e-6f;
        const bool isRunning = isMoving && intent.run;
        if (isRunning) {
          const float accelerationTime = std::max(0.0f, settings.runAccelerationTime);
          runState.progress =
              accelerationTime > 0.0f
                  ? std::min(1.0f, runState.progress + it.delta_time() / accelerationTime)
                  : 1.0f;
        } else {
          runState.progress = 0.0f;
        }

        const float runMultiplier = std::max(1.0f, settings.runSpeedMultiplier);
        const float currentMultiplier = 1.0f + (runMultiplier - 1.0f) * std::sqrt(runState.progress);
        velocity.value = Vector2Scale(movement, std::max(0.0f, settings.walkSpeed) * currentMultiplier);
      });
}

void EnablePlayerMovement(flecs::entity player, PlayerMovementSettings settings) {
  player.set<PlayerMovementSettings>(settings)
      .set<RequestedVelocity>(RequestedVelocity{})
      .set<RunState>(RunState{});
}

} // namespace Movement

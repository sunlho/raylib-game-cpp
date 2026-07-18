#include "CharacterInternal.h"

#include "modules/Physics.h"
#include "modules/Rendering.h"

namespace Character::Internal {

void RegisterCharacterPhysics(flecs::world &world) {
  world.observer<const Rendering::Position>("Create Character Physics Observer")
      .with<PlayerTag>()
      .event(flecs::OnSet)
      .each([](flecs::entity entity, const Rendering::Position &position) {
        if (const auto *body = entity.try_get<Physics::PhysicsBody>()) {
          Physics::Relocate(*body, position.value);
          return;
        }

        Physics::CreateDynamicCircle(
            entity,
            position.value,
            Vector2{0.0f, 10.0f},
            10.0f);
      });
}

} // namespace Character::Internal

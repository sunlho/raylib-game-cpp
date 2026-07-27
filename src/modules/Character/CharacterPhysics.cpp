#include "CharacterInternal.h"

#include "modules/Core/Transform.h"
#include "modules/Physics.h"

namespace Character::Internal {

void RegisterCharacterPhysics(flecs::world &world) {
  world.observer<const Core::Position>("Create Character Physics Observer")
      .with<PlayerTag>()
      .event(flecs::OnSet)
      .each([](flecs::entity entity, const Core::Position &position) {
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

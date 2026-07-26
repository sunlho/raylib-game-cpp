#include "Core.h"

#include "../Reflection.h"

namespace Core {

module::module(flecs::world &world) {
  Reflection::Register<Position>(world);
  Reflection::Register<PreviousPosition>(world);
  Reflection::Register<RenderPosition>(world);

  Reflection::Register<LogicalViewSize>(world)
      .add(flecs::Singleton)
      .set<LogicalViewSize>({});
  Reflection::Register<RenderTargetSize>(world)
      .add(flecs::Singleton)
      .set<RenderTargetSize>({});
  Reflection::Register<WorldBounds>(world)
      .add(flecs::Singleton)
      .set<WorldBounds>({});
  Reflection::Register<OutputScale>(world)
      .add(flecs::Singleton)
      .set<OutputScale>({});
}

} // namespace Core

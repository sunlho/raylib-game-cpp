#pragma once

#include "flecs.h"

template <typename T>
auto buildPipeline(flecs::world &world) {
  return world.scope("Pipelines").pipeline<T>().with(flecs::System).with<T>().build();
}

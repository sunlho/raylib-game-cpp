#pragma once

#include "flecs.h"

template <typename T>
auto buildPipeline(flecs::world &world) {
  return world.pipeline().with(flecs::System).with<T>().build();
}

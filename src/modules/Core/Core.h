#pragma once

#include "flecs.h"

#include "Simulation.h"
#include "Transform.h"
#include "View.h"

namespace Core {

struct module {
  module(flecs::world &world);
};

} // namespace Core

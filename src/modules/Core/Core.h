#pragma once

#include "flecs.h"

#include "Transform.h"
#include "View.h"

namespace Core {

// Registers the shared components and their singletons. Import this before any
// other module: everything else assumes Position and the view singletons exist.
struct module {
  module(flecs::world &world);
};

} // namespace Core

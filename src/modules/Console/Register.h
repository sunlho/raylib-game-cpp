#pragma once

#include "flecs.h"

namespace GameConsole {

struct CommandServices {
  bool *debugDrawEnabled = nullptr;
};

// Add project commands in Register.cpp and register them from this single entry point.
void RegisterCommands(flecs::world &world, CommandServices services = {});

} // namespace GameConsole

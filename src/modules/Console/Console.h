#pragma once

#include <functional>
#include <string>
#include <vector>

#include "flecs.h"

namespace GameConsole {

struct CommandResult {
  bool success = true;
  std::string message;
};

using CommandHandler = std::function<CommandResult(flecs::world &, const std::vector<std::string> &)>;

struct CommandDefinition {
  std::string name;
  std::string usage;
  std::string description;
  CommandHandler handler;
};

bool RegisterCommand(flecs::world &world, CommandDefinition definition);
void Print(flecs::world &world, std::string message);
void PrintError(flecs::world &world, std::string message);

void Update(flecs::world &world);
void Draw(flecs::world &world);
void Shutdown();
bool IsOpen(const flecs::world &world);
void SetOpen(flecs::world &world, bool open, std::string initialInput = {});

struct module {
  module(flecs::world &world);
};

} // namespace GameConsole

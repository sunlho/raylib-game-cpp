
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <utility>

#include "raylib.h"

#include "../Camera.h"
#include "../Movement.h"
#include "../Physics.h"
#include "../Rendering.h"

#include "Console.h"
#include "ConsoleInternal.h"
#include "Register.h"

namespace GameConsole {
namespace {

bool ParseFloat(const std::string &text, float &value) {
  char *end = nullptr;
  value = std::strtof(text.c_str(), &end);
  return end != text.c_str() && *end == '\0' && std::isfinite(value);
}

std::string JoinArguments(const std::vector<std::string> &arguments) {
  std::ostringstream output;
  for (size_t index = 0; index < arguments.size(); ++index) {
    if (index > 0) {
      output << ' ';
    }
    output << arguments[index];
  }
  return output.str();
}

void TeleportPlayer(flecs::world &world, float x, float y) {
  const auto player = world.lookup("Player");
  if (!player.is_valid() || !player.has<Rendering::Position>()) {
    return;
  }

  const Vector2 destination{x, y};
  player.get_mut<Rendering::Position>().value = destination;
  if (player.has<Movement::Velocity>()) {
    player.get_mut<Movement::Velocity>().value = Vector2{0.0f, 0.0f};
  }
  if (player.has<Physics::PhysicsBody>()) {
    Physics::Relocate(player.get<Physics::PhysicsBody>(), destination, true);
  }

  world.get_mut<GameCamera::MainCamera>().value.target = destination;
}

} // namespace

void RegisterCommands(flecs::world &world, CommandServices services) {
  RegisterCommand(
      world,
      {
          "help",
          "[command]",
          "List commands or show command details",
          [](flecs::world &commandWorld, const std::vector<std::string> &arguments) {
            const auto &state = commandWorld.get<Internal::ConsoleState>();
            if (!arguments.empty()) {
              const auto *command = Internal::FindCommand(state, arguments.front());
              if (command == nullptr) {
                return CommandResult{false, "Unknown command: " + arguments.front()};
              }
              return CommandResult{true, Internal::FormatCommand(*command) + "\n" + command->description};
            }

            std::string output = "Commands:";
            for (const auto &command : state.commands) {
              output += "\n  " + Internal::FormatCommand(command) + " - " + command.description;
            }
            return CommandResult{true, std::move(output)};
          },
      });

  RegisterCommand(
      world,
      {
          "clear",
          "",
          "Clear console output",
          [](flecs::world &commandWorld, const std::vector<std::string> &) {
            Internal::ClearLines(commandWorld.get_mut<Internal::ConsoleState>());
            return CommandResult{};
          },
      });

  RegisterCommand(
      world,
      {
          "echo",
          "<text>",
          "Print text to the console",
          [](flecs::world &, const std::vector<std::string> &arguments) {
            if (arguments.empty()) {
              return CommandResult{false, "Usage: echo <text>"};
            }
            return CommandResult{true, JoinArguments(arguments)};
          },
      });

  RegisterCommand(
      world,
      {
          "fps",
          "[30-240]",
          "Show FPS or set the frame limit",
          [](flecs::world &, const std::vector<std::string> &arguments) {
            if (arguments.empty()) {
              return CommandResult{true, "Current FPS: " + std::to_string(GetFPS())};
            }
            float requested = 0.0f;
            if (!ParseFloat(arguments.front(), requested) || requested < 30.0f || requested > 240.0f) {
              return CommandResult{false, "Usage: fps [30-240]"};
            }
            const int target = static_cast<int>(std::round(requested));
            SetTargetFPS(target);
            return CommandResult{true, "Frame limit set to " + std::to_string(target)};
          },
      });

  RegisterCommand(
      world,
      {
          "resolution",
          "<0-4>",
          "Set window size (1: 1280x720, 2: 1600x900, 3: 1920x1080, 4: 2560x1440)",
          [](flecs::world &commandWorld, const std::vector<std::string> &arguments) {
            if (arguments.size() != 1) {
              return CommandResult{false, "Usage: resolution <1-4> (1: 1280x720, 2: 1600x900, 3: 1920x1080, 4: 2560x1440)"};
            }

            int width = 0;
            int height = 0;
            if (arguments.front() == "1") {
              width = 1280;
              height = 720;
            } else if (arguments.front() == "2") {
              width = 1600;
              height = 900;
            } else if (arguments.front() == "3") {
              width = 1920;
              height = 1080;
            } else if (arguments.front() == "4") {
              width = 2560;
              height = 1440;
            } else {
              return CommandResult{false, "Usage: resolution <0-4> (0: 640x360, 1: 1280x720, 2: 1600x900, 3: 1920x1080, 4: 2560x1440)"};
            }

            SetWindowSize(width, height);
            commandWorld.get_mut<Rendering::RenderTargetSize>().dimension = Vector2{
                static_cast<float>(width),
                static_cast<float>(height)};
            return CommandResult{
                true,
                "Window size set to " + std::to_string(width) + "x" + std::to_string(height)};
          },
      });

  RegisterCommand(
      world,
      {
          "camerascale",
          "[0.1-10]",
          "Show or set camera scale",
          [](flecs::world &commandWorld, const std::vector<std::string> &arguments) {
            auto &mainCamera = commandWorld.get_mut<GameCamera::MainCamera>();
            if (arguments.empty()) {
              return CommandResult{true, "Camera scale: " + std::to_string(mainCamera.value.zoom)};
            }
            float scale = 0.0f;
            if (!ParseFloat(arguments.front(), scale) || scale < 0.1f || scale > 10.0f) {
              return CommandResult{false, "Usage: cameraScale [0.1-10]"};
            }
            mainCamera.value.zoom = scale;
            return CommandResult{true, "Camera scale set to " + std::to_string(scale)};
          },
      });

  RegisterCommand(
      world,
      {
          "speed",
          "[value]",
          "Show or set player movement speed",
          [](flecs::world &commandWorld, const std::vector<std::string> &arguments) {
            const auto player = commandWorld.lookup("Player");
            if (!player.is_valid() || !player.has<Movement::MoveSpeed>()) {
              return CommandResult{false, "Player is unavailable"};
            }
            if (arguments.empty()) {
              return CommandResult{true, "Player speed: " + std::to_string(player.get<Movement::MoveSpeed>().value)};
            }
            float speed = 0.0f;
            if (!ParseFloat(arguments.front(), speed) || speed < 0.0f || speed > 1000.0f) {
              return CommandResult{false, "Usage: speed [0-1000]"};
            }
            player.get_mut<Movement::MoveSpeed>().value = speed;
            return CommandResult{true, "Player speed set to " + std::to_string(speed)};
          },
      });

  RegisterCommand(
      world,
      {
          "position",
          "",
          "Show the player world position",
          [](flecs::world &commandWorld, const std::vector<std::string> &) {
            const auto player = commandWorld.lookup("Player");
            if (!player.is_valid() || !player.has<Rendering::Position>()) {
              return CommandResult{false, "Player is unavailable"};
            }
            const Vector2 position = player.get<Rendering::Position>().value;
            std::ostringstream output;
            output << std::fixed << std::setprecision(1) << "Player position: " << position.x << ", " << position.y;
            return CommandResult{true, output.str()};
          },
      });

  RegisterCommand(
      world,
      {
          "tp",
          "<x> <y> [loading-seconds]",
          "Teleport the player after a loading delay",
          [](flecs::world &commandWorld, const std::vector<std::string> &arguments) {
            float x = 0.0f;
            float y = 0.0f;
            float loadingTime = 1.0f;
            if ((arguments.size() != 2 && arguments.size() != 3) ||
                !ParseFloat(arguments[0], x) ||
                !ParseFloat(arguments[1], y) ||
                (arguments.size() == 3 && !ParseFloat(arguments[2], loadingTime)) ||
                loadingTime < 0.0f || loadingTime > 60.0f) {
              return CommandResult{false, "Usage: tp <x> <y> [loading-seconds: 0-60]"};
            }

            const auto player = commandWorld.lookup("Player");
            if (!player.is_valid() || !player.has<Rendering::Position>()) {
              return CommandResult{false, "Player is unavailable"};
            }

            const bool started = Rendering::RunLoadingSequence(
                commandWorld,
                {{1.0f,
                  "Teleporting...",
                  [x, y](flecs::world &loadingWorld) {
                    TeleportPlayer(loadingWorld, x, y);
                  },
                  loadingTime}});
            if (!started) {
              return CommandResult{false, "Another loading sequence is already active"};
            }

            SetOpen(commandWorld, false);

            std::ostringstream output;
            output
                << std::fixed << std::setprecision(1)
                << "Teleporting to " << x << ", " << y
                << " after " << loadingTime << "s";
            return CommandResult{true, output.str()};
          },
      });

  RegisterCommand(
      world,
      {
          "debug",
          "[on|off]",
          "Show or hide physics debug drawing",
          [enabled = services.debugDrawEnabled](flecs::world &, const std::vector<std::string> &arguments) {
            if (enabled == nullptr) {
              return CommandResult{false, "Debug drawing is unavailable"};
            }
            if (!arguments.empty()) {
              if (arguments.front() == "on") {
                *enabled = true;
              } else if (arguments.front() == "off") {
                *enabled = false;
              } else {
                return CommandResult{false, "Usage: debug [on|off]"};
              }
            }
            return CommandResult{true, std::string("Debug drawing: ") + (*enabled ? "on" : "off")};
          },
      });
}

} // namespace GameConsole

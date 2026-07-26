
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

// Accepts a preset index (1-4), a bare 16:9 height like "1080" (Eastward's
// window_size setting works this way), or an explicit "1920x1080".
bool ParseWindowSize(const std::string &text, int &width, int &height) {
  const auto separator = text.find_first_of("xX*");
  if (separator != std::string::npos) {
    const long parsedWidth = std::strtol(text.substr(0, separator).c_str(), nullptr, 10);
    const long parsedHeight = std::strtol(text.substr(separator + 1).c_str(), nullptr, 10);
    if (parsedWidth < 320 || parsedHeight < 180 || parsedWidth > 16384 || parsedHeight > 16384) {
      return false;
    }
    width = static_cast<int>(parsedWidth);
    height = static_cast<int>(parsedHeight);
    return true;
  }

  char *end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0') {
    return false;
  }

  switch (value) {
  case 1:
    width = 1280;
    height = 720;
    return true;
  case 2:
    width = 1600;
    height = 900;
    return true;
  case 3:
    width = 1920;
    height = 1080;
    return true;
  case 4:
    width = 2560;
    height = 1440;
    return true;
  default:
    break;
  }

  if (value < 180 || value > 16384) {
    return false;
  }
  height = static_cast<int>(value);
  width = static_cast<int>(std::round(static_cast<double>(height) * 16.0 / 9.0));
  return true;
}

// SetWindowSize keeps the top-left corner, which can push a larger window off
// screen. Re-centre it on the current monitor instead.
void CenterWindow() {
  const int monitor = GetCurrentMonitor();
  const int monitorWidth = GetMonitorWidth(monitor);
  const int monitorHeight = GetMonitorHeight(monitor);
  if (monitorWidth <= 0 || monitorHeight <= 0) {
    return;
  }

  const Vector2 monitorPosition = GetMonitorPosition(monitor);
  SetWindowPosition(
      static_cast<int>(monitorPosition.x) + (monitorWidth - GetScreenWidth()) / 2,
      static_cast<int>(monitorPosition.y) + (monitorHeight - GetScreenHeight()) / 2);
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
          "[preset|height|WxH]",
          "Show or set window size (1: 1280x720, 2: 1600x900, 3: 1920x1080, 4: 2560x1440, or 1080 / 1920x1080)",
          [](flecs::world &, const std::vector<std::string> &arguments) {
            static constexpr const char *kUsage =
                "Usage: resolution [<1-4> | <height> | <width>x<height>]";
            if (arguments.empty()) {
              return CommandResult{
                  true,
                  "Window size: " + std::to_string(GetScreenWidth()) + "x" + std::to_string(GetScreenHeight())};
            }
            if (arguments.size() != 1) {
              return CommandResult{false, kUsage};
            }

            int width = 0;
            int height = 0;
            if (!ParseWindowSize(arguments.front(), width, height)) {
              return CommandResult{false, kUsage};
            }

            if (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) {
              return CommandResult{false, "Leave fullscreen first (F11 or 'fullscreen off')"};
            }

            SetWindowSize(width, height);
            CenterWindow();
            return CommandResult{
                true,
                "Window size set to " + std::to_string(width) + "x" + std::to_string(height)};
          },
      });

  RegisterCommand(
      world,
      {
          "scalemode",
          "[sharp|smooth|integer]",
          "Show or set how the scene buffer is scaled into the window",
          [](flecs::world &commandWorld, const std::vector<std::string> &arguments) {
            if (arguments.empty()) {
              return CommandResult{
                  true,
                  std::string{"Scale mode: "} + Rendering::ToString(Rendering::GetOutputScaleMode(commandWorld)) +
                      "\n  sharp   - PixZoom sharp-bilinear, crisp at any scale (default)" +
                      "\n  smooth  - plain bilinear" +
                      "\n  integer - nearest, whole-number scale only"};
            }
            if (arguments.size() != 1) {
              return CommandResult{false, "Usage: scalemode [sharp|smooth|integer]"};
            }

            Rendering::OutputScaleMode mode{};
            if (!Rendering::ParseOutputScaleMode(arguments.front(), mode)) {
              return CommandResult{false, "Usage: scalemode [sharp|smooth|integer]"};
            }

            Rendering::SetOutputScaleMode(commandWorld, mode);
            return CommandResult{true, std::string{"Scale mode set to "} + Rendering::ToString(mode)};
          },
      });

  RegisterCommand(
      world,
      {
          "sceneres",
          "[native|full|half]",
          "Show or set scene buffer density (native: follows window, full: zoom 2, half: zoom 1)",
          [](flecs::world &commandWorld, const std::vector<std::string> &arguments) {
            static constexpr const char *kUsage = "Usage: sceneres [native|full|half]";
            const auto describe = [](flecs::world &w) {
              const auto &size = w.get<Rendering::RenderTargetSize>().dimension;
              const auto &camera = w.get<GameCamera::MainCamera>();
              std::ostringstream text;
              text << std::fixed << std::setprecision(0);
              text << "Scene resolution: " << Rendering::ToString(Rendering::GetSceneResolution(w))
                   << " (" << size.x << "x" << size.y
                   << ", zoom " << camera.value.zoom
                   << ", grid " << (camera.pixelsPerWorldUnit > 0.0f ? 1.0f / camera.pixelsPerWorldUnit : 0.0f)
                   << " world unit)";
              return text.str();
            };

            if (arguments.empty()) {
              return CommandResult{
                  true,
                  describe(commandWorld) +
                      "\n  native - buffer follows the window; everything snaps to half an output pixel" +
                      "\n  full   - fixed 1280x720; characters snap to half an art pixel" +
                      "\n  half   - fixed 640x360; characters snap to a whole art pixel"};
            }
            if (arguments.size() != 1) {
              return CommandResult{false, kUsage};
            }

            Rendering::SceneResolution resolution{};
            if (!Rendering::ParseSceneResolution(arguments.front(), resolution)) {
              return CommandResult{false, kUsage};
            }

            Rendering::SetSceneResolution(commandWorld, resolution);
            return CommandResult{true, describe(commandWorld)};
          },
      });

  RegisterCommand(
      world,
      {
          "fullscreen",
          "[on|off]",
          "Show or toggle borderless fullscreen (same as Alt+Enter)",
          [](flecs::world &, const std::vector<std::string> &arguments) {
            const bool active = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
            if (arguments.empty()) {
              ToggleBorderlessWindowed();
              return CommandResult{true, active ? "Fullscreen off" : "Fullscreen on"};
            }
            if (arguments.size() != 1) {
              return CommandResult{false, "Usage: fullscreen [on|off]"};
            }

            const std::string &argument = arguments.front();
            bool wanted = false;
            if (argument == "on" || argument == "1" || argument == "true") {
              wanted = true;
            } else if (argument == "off" || argument == "0" || argument == "false") {
              wanted = false;
            } else {
              return CommandResult{false, "Usage: fullscreen [on|off]"};
            }

            if (wanted != active) {
              ToggleBorderlessWindowed();
            }
            return CommandResult{true, wanted ? "Fullscreen on" : "Fullscreen off"};
          },
      });

  RegisterCommand(
      world,
      {
          "videoinfo",
          "",
          "Show window, scene buffer, logical view and output scaling",
          [](flecs::world &commandWorld, const std::vector<std::string> &) {
            const auto &sceneSize = commandWorld.get<Rendering::RenderTargetSize>().dimension;
            const auto &logicalView = commandWorld.get<Rendering::LogicalViewSize>().value;
            const auto &mainCamera = commandWorld.get<GameCamera::MainCamera>();
            const auto mode = Rendering::GetOutputScaleMode(commandWorld);
            const Rectangle output = Rendering::ComputeOutputRect(
                GetScreenWidth(),
                GetScreenHeight(),
                sceneSize,
                mode == Rendering::OutputScaleMode::Integer);

            std::ostringstream report;
            report << std::fixed << std::setprecision(2);
            report << "Window:       " << GetScreenWidth() << "x" << GetScreenHeight()
                   << (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) ? " (fullscreen)" : "")
                   << "\nScene buffer: " << sceneSize.x << "x" << sceneSize.y
                   << " (" << Rendering::ToString(Rendering::GetSceneResolution(commandWorld)) << ")"
                   << "\nLogical view: " << logicalView.x << "x" << logicalView.y << " world units"
                   << "\nCamera zoom:  " << mainCamera.value.zoom
                   << "\nPPWU:         " << mainCamera.pixelsPerWorldUnit
                   << "\nScale mode:   " << Rendering::ToString(mode)
                   << "\nOutput rect:  " << output.width << "x" << output.height
                   << " at " << output.x << "," << output.y
                   << " (" << (output.height / sceneSize.y) << "x)"
                   << "\nCamera grid:  " << (mainCamera.pixelsPerWorldUnit > 0.0f ? 1.0f / mainCamera.pixelsPerWorldUnit : 0.0f)
                   << " world unit, effective "
                   << ((mainCamera.pixelsPerWorldUnit > 0.0f && output.height > 0.0f)
                           ? 1.0f / (mainCamera.pixelsPerWorldUnit * std::max(1.0f, std::round(output.height / sceneSize.y)))
                           : 0.0f)
                   << " with residual shift"
                   << "\nRender shift: " << mainCamera.renderShift.x << "," << mainCamera.renderShift.y << " output px";
            return CommandResult{true, report.str()};
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


#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <utility>

#include "raylib.h"

#include "../Camera.h"
#include "../Map/Map.h"
#include "../Movement.h"
#include "../Physics.h"
#include "../Rendering.h"
#include "../Window.h"

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
  if (!player.is_valid() || !player.has<Core::Position>()) {
    return;
  }

  const Vector2 destination{x, y};
  player.get_mut<Core::Position>().value = destination;
  if (player.has<Movement::RequestedVelocity>()) {
    player.get_mut<Movement::RequestedVelocity>().value = Vector2{0.0f, 0.0f};
  }
  if (player.has<Physics::PhysicsBody>()) {
    Physics::Relocate(player.get<Physics::PhysicsBody>(), destination, true);
  }

  // A teleport is a discontinuity, so the interpolation range has to collapse
  // onto the destination. Otherwise the next InterpolatePositions still blends
  // from the pre-teleport sample and the player slides across the whole jump.
  if (player.has<Core::PreviousPosition>()) {
    player.get_mut<Core::PreviousPosition>().value = destination;
  }

  // SnapCameraTo, not value.target: a raw write is unclamped (the camera can end
  // up showing outside the map) and UpdateRenderCamera overwrites it from
  // smoothTarget on the next render frame, which then flies over from the
  // pre-teleport position.
  GameCamera::SnapCameraTo(world, destination);

  // The loading screen suppresses InterpolatePositions/QuantizeRenderPositions
  // for as long as it is visible, and a teleport runs from inside a loading
  // step. Without writing the render position here the player stays drawn at the
  // old spot until the reveal finishes.
  if (player.has<Core::RenderPosition>()) {
    const auto &camera = world.get<GameCamera::MainCamera>();
    auto &renderPosition = player.get_mut<Core::RenderPosition>();
    renderPosition.interpolated = destination;
    renderPosition.quantized = Core::QuantizeForCamera(destination, camera.renderTarget, camera.pixelsPerWorldUnit);
  }
}

} // namespace

void RegisterCommands(flecs::world &world, CommandServices services) {
  RegisterCommand(
      world,
      {
          "map",
          "[map.tmx] [spawn]",
          "Show the current map or switch to a TMX map at a named Spawn object",
          [](flecs::world &commandWorld, const std::vector<std::string> &arguments) {
            if (arguments.empty()) {
              const auto &status = commandWorld.get<MapManager::Status>();
              if (status.phase == MapManager::Phase::Failed && status.failure) {
                return CommandResult{false, status.failure->message};
              }
              return status.currentMapPath.empty()
                         ? CommandResult{false, "No map is currently loaded"}
                         : CommandResult{true, "Current map: " + status.currentMapPath};
            }
            if (arguments.size() > 2) {
              return CommandResult{false, "Usage: map <map.tmx> [spawn]"};
            }

            MapManager::MapTransition transition{arguments.front()};
            if (arguments.size() == 2)
              transition.destination = MapManager::NamedSpawn{arguments[1]};
            const auto receipt = MapManager::Submit(commandWorld, std::move(transition));
            if (receipt.admission == MapManager::Admission::Busy)
              return CommandResult{false, "Another map operation is already active"};

            SetOpen(commandWorld, false);
            return CommandResult{true, "Map operation accepted (#" + std::to_string(receipt.id) + ")"};
          },
      });

  RegisterCommand(
      world,
      {
          "reloadmap",
          "",
          "Reload the current TMX map from disk",
          [](flecs::world &commandWorld, const std::vector<std::string> &arguments) {
            if (!arguments.empty()) {
              return CommandResult{false, "Usage: reloadmap"};
            }

            const auto receipt = MapManager::Submit(commandWorld, MapManager::MapReload{});
            if (receipt.admission == MapManager::Admission::Busy)
              return CommandResult{false, "Another map operation is already active"};

            SetOpen(commandWorld, false);
            return CommandResult{true, "Map reload accepted (#" + std::to_string(receipt.id) + ")"};
          },
      });

  RegisterCommand(
      world,
      {
          "spawn",
          "<name>",
          "Teleport the player to a named Spawn object on the current map",
          [](flecs::world &commandWorld, const std::vector<std::string> &arguments) {
            if (arguments.size() != 1) {
              return CommandResult{false, "Usage: spawn <name>"};
            }

            const std::string spawnName = arguments.front();
            const auto receipt = MapManager::Submit(
                commandWorld,
                MapManager::SpawnTravel{MapManager::NamedSpawn{spawnName}});
            if (receipt.admission == MapManager::Admission::Busy)
              return CommandResult{false, "Another map operation is already active"};

            SetOpen(commandWorld, false);
            return CommandResult{true, "Spawn travel accepted (#" + std::to_string(receipt.id) + ")"};
          },
      });

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
          "[preset|fullscreen]",
          "Show or set window size (" + GameWindow::PresetList() + ", fullscreen)",
          [](flecs::world &, const std::vector<std::string> &arguments) {
            const std::string usage =
                "Usage: resolution [<1-" + std::to_string(GameWindow::kPresetCount) +
                "> | <height> | <width>x<height> | fullscreen]\n  " + GameWindow::PresetList() +
                "\n  A preset that is not smaller than the monitor switches to fullscreen.";
            if (arguments.empty()) {
              return CommandResult{true, "Window size: " + GameWindow::DescribeCurrent() + "\n" + usage};
            }
            if (arguments.size() != 1) {
              return CommandResult{false, usage};
            }

            const std::string &argument = arguments.front();
            if (argument == "fullscreen" || argument == "full") {
              GameWindow::SetFullscreen(true);
              return CommandResult{true, "Window size: " + GameWindow::DescribeCurrent()};
            }

            int preset = 0;
            if (!GameWindow::ParsePreset(argument, preset)) {
              return CommandResult{false, "Unsupported window size.\n" + usage};
            }

            const bool wentFullscreen = GameWindow::ApplyPreset(preset);
            if (wentFullscreen) {
              return CommandResult{
                  true,
                  "Preset does not fit the monitor, switched to fullscreen (" +
                      GameWindow::DescribeCurrent() + ")"};
            }
            return CommandResult{true, "Window size set to " + GameWindow::DescribeCurrent()};
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
              const auto &size = w.get<Core::RenderTargetSize>().dimension;
              const auto &camera = w.get<GameCamera::MainCamera>();
              std::ostringstream text;
              text << std::fixed << std::setprecision(0);
              text
                  << "Scene resolution: " << Rendering::ToString(Rendering::GetSceneResolution(w))
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
            const bool active = GameWindow::IsFullscreen();
            if (arguments.empty()) {
              GameWindow::ToggleFullscreen();
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

            GameWindow::SetFullscreen(wanted);
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
            const auto &sceneSize = commandWorld.get<Core::RenderTargetSize>().dimension;
            const auto &logicalView = commandWorld.get<Core::LogicalViewSize>().value;
            const auto &mainCamera = commandWorld.get<GameCamera::MainCamera>();
            const auto mode = Rendering::GetOutputScaleMode(commandWorld);
            const Rectangle output = Rendering::ComputeOutputRect(
                GetScreenWidth(),
                GetScreenHeight(),
                sceneSize,
                mode == Rendering::OutputScaleMode::Integer);

            std::ostringstream report;
            report << std::fixed << std::setprecision(2);
            report
                << "Window:       " << GetScreenWidth() << "x" << GetScreenHeight()
                << (GameWindow::IsFullscreen() ? " (fullscreen)" : "")
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
            if (arguments.empty()) {
              const auto &mainCamera = commandWorld.get<GameCamera::MainCamera>();
              return CommandResult{true, "Camera scale: " + std::to_string(mainCamera.value.zoom)};
            }
            float scale = 0.0f;
            if (!ParseFloat(arguments.front(), scale) || scale < 0.1f || scale > 10.0f) {
              return CommandResult{false, "Usage: cameraScale [0.1-10]"};
            }
            // Never assign Camera2D::zoom here: it would leave pixelsPerWorldUnit
            // and RenderTargetSize stale, and in Native mode the mismatch is
            // undetectable by UpdateOutputGeometry, so it never recovers.
            Rendering::SetSceneScale(commandWorld, scale);
            return CommandResult{
                true,
                "Camera scale set to " + std::to_string(scale) + " (sceneres: custom)"};
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
            if (!player.is_valid() || !player.has<Movement::PlayerMovementSettings>()) {
              return CommandResult{false, "Player is unavailable"};
            }
            if (arguments.empty()) {
              return CommandResult{true, "Player speed: " + std::to_string(player.get<Movement::PlayerMovementSettings>().walkSpeed)};
            }
            float speed = 0.0f;
            if (!ParseFloat(arguments.front(), speed) || speed < 0.0f || speed > 1000.0f) {
              return CommandResult{false, "Usage: speed [0-1000]"};
            }
            player.get_mut<Movement::PlayerMovementSettings>().walkSpeed = speed;
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
            if (!player.is_valid() || !player.has<Core::Position>()) {
              return CommandResult{false, "Player is unavailable"};
            }
            const Vector2 position = player.get<Core::Position>().value;
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
            if (!player.is_valid() || !player.has<Core::Position>()) {
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

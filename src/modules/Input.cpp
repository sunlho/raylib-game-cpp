#include "Input.h"

#include <algorithm>

#include "Console/Console.h"
#include "Reflection.h"
#include "raymath.h"

namespace Input {
namespace {

constexpr int PlayerGamepad = 0;
constexpr float StickDeadzone = 0.20f;

enum class ActiveDevice {
  Keyboard,
  Gamepad,
};

struct DeviceSelectionState {
  ActiveDevice active = ActiveDevice::Keyboard;
  bool keyboardWasMoving = false;
  bool gamepadWasMoving = false;
};

struct DeviceIntent {
  Vector2 movement = {0.0f, 0.0f};
  bool run = false;
};

bool IsMoving(Vector2 movement) {
  return Vector2LengthSqr(movement) > 1.0e-6f;
}

Vector2 NormalizeDigitalDirection(Vector2 direction) {
  return IsMoving(direction) ? Vector2Normalize(direction) : Vector2{};
}

DeviceIntent SampleKeyboard() {
  const Vector2 direction = {
      static_cast<float>(IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) -
          static_cast<float>(IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)),
      static_cast<float>(IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) -
          static_cast<float>(IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))};

  return {
      NormalizeDigitalDirection(direction),
      IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)};
}

Vector2 ApplyRadialDeadzone(Vector2 value) {
  const float rawMagnitude = Vector2Length(value);
  if (rawMagnitude <= StickDeadzone) {
    return {};
  }

  const float magnitude = std::min(rawMagnitude, 1.0f);
  const float remapped = (magnitude - StickDeadzone) / (1.0f - StickDeadzone);
  return Vector2Scale(value, remapped / rawMagnitude);
}

DeviceIntent SampleGamepad() {
  if (!IsGamepadAvailable(PlayerGamepad)) {
    return {};
  }

  const Vector2 dpad = {
      static_cast<float>(IsGamepadButtonDown(PlayerGamepad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) -
          static_cast<float>(IsGamepadButtonDown(PlayerGamepad, GAMEPAD_BUTTON_LEFT_FACE_LEFT)),
      static_cast<float>(IsGamepadButtonDown(PlayerGamepad, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) -
          static_cast<float>(IsGamepadButtonDown(PlayerGamepad, GAMEPAD_BUTTON_LEFT_FACE_UP))};

  const Vector2 movement =
      IsMoving(dpad)
          ? NormalizeDigitalDirection(dpad)
          : ApplyRadialDeadzone(Vector2{
                GetGamepadAxisMovement(PlayerGamepad, GAMEPAD_AXIS_LEFT_X),
                GetGamepadAxisMovement(PlayerGamepad, GAMEPAD_AXIS_LEFT_Y)});

  return {
      movement,
      IsGamepadButtonDown(PlayerGamepad, GAMEPAD_BUTTON_LEFT_THUMB)};
}

ActiveDevice SelectActiveDevice(
    DeviceSelectionState &state,
    const DeviceIntent &keyboard,
    const DeviceIntent &gamepad) {
  const bool keyboardMoving = IsMoving(keyboard.movement);
  const bool gamepadMoving = IsMoving(gamepad.movement);
  const bool keyboardStarted = keyboardMoving && !state.keyboardWasMoving;
  const bool gamepadStarted = gamepadMoving && !state.gamepadWasMoving;

  if (keyboardStarted != gamepadStarted) {
    state.active = keyboardStarted ? ActiveDevice::Keyboard : ActiveDevice::Gamepad;
  } else if (state.active == ActiveDevice::Keyboard && !keyboardMoving && gamepadMoving) {
    state.active = ActiveDevice::Gamepad;
  } else if (state.active == ActiveDevice::Gamepad && !gamepadMoving && keyboardMoving) {
    state.active = ActiveDevice::Keyboard;
  }

  state.keyboardWasMoving = keyboardMoving;
  state.gamepadWasMoving = gamepadMoving;
  return state.active;
}

} // namespace

module::module(flecs::world &world) {
  Reflection::Register<PlayerControlIntent>(world)
      .add(flecs::Singleton)
      .set<PlayerControlIntent>({});
  world.component<DeviceSelectionState>()
      .add(flecs::Singleton)
      .set<DeviceSelectionState>({});
}

void Update(flecs::world &world) {
  const DeviceIntent keyboard = SampleKeyboard();
  const DeviceIntent gamepad = SampleGamepad();
  auto &selection = world.get_mut<DeviceSelectionState>();
  const ActiveDevice active = SelectActiveDevice(selection, keyboard, gamepad);

  PlayerControlIntent intent;
  if (!GameConsole::IsOpen(world)) {
    const DeviceIntent &selected = active == ActiveDevice::Keyboard ? keyboard : gamepad;
    intent.movement = selected.movement;
    intent.run = selected.run;
  }

  world.set<PlayerControlIntent>(intent);
}

} // namespace Input

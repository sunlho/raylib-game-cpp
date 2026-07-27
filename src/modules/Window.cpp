#include "Window.h"

#include <cstdlib>
#include <sstream>
#include <string>

#include "raylib.h"

namespace GameWindow {
namespace {

// SetWindowSize keeps the top-left corner, which can push a larger window off
// screen. Re-centre it on the current monitor instead.
void CenterOnMonitor() {
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

} // namespace

int FindPreset(int width, int height) {
  for (int index = 0; index < kPresetCount; ++index) {
    if (kPresets[index].width == width && kPresets[index].height == height) {
      return index;
    }
  }
  return -1;
}

bool ParsePreset(const std::string &text, int &index) {
  const auto separator = text.find_first_of("xX*");
  if (separator != std::string::npos) {
    const long width = std::strtol(text.substr(0, separator).c_str(), nullptr, 10);
    const long height = std::strtol(text.substr(separator + 1).c_str(), nullptr, 10);
    const int found = FindPreset(static_cast<int>(width), static_cast<int>(height));
    if (found < 0) {
      return false;
    }
    index = found;
    return true;
  }

  char *end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0') {
    return false;
  }

  if (value >= 1 && value <= kPresetCount) {
    index = static_cast<int>(value) - 1;
    return true;
  }

  for (int candidate = 0; candidate < kPresetCount; ++candidate) {
    if (kPresets[candidate].height == static_cast<int>(value)) {
      index = candidate;
      return true;
    }
  }
  return false;
}

std::string PresetList() {
  std::ostringstream text;
  for (int index = 0; index < kPresetCount; ++index) {
    if (index > 0) {
      text << ", ";
    }
    text << (index + 1) << ": " << kPresets[index].width << "x" << kPresets[index].height;
  }
  return text.str();
}

bool IsFullscreen() { return IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE); }

void SetFullscreen(bool enabled) {
  if (enabled != IsFullscreen()) {
    ToggleBorderlessWindowed();
  }
}

void ToggleFullscreen() { ToggleBorderlessWindowed(); }

bool PresetFitsMonitor(int index) {
  if (index < 0 || index >= kPresetCount) {
    return false;
  }

  const int monitor = GetCurrentMonitor();
  const int monitorWidth = GetMonitorWidth(monitor);
  const int monitorHeight = GetMonitorHeight(monitor);
  if (monitorWidth <= 0 || monitorHeight <= 0) {
    // Monitor size unknown: keep the window rather than forcing fullscreen.
    return true;
  }

  return kPresets[index].width < monitorWidth && kPresets[index].height < monitorHeight;
}

bool ApplyPreset(int index) {
  if (index < 0 || index >= kPresetCount) {
    return IsFullscreen();
  }

  if (!PresetFitsMonitor(index)) {
    SetFullscreen(true);
    return true;
  }

  SetFullscreen(false);
  SetWindowSize(kPresets[index].width, kPresets[index].height);
  CenterOnMonitor();
  return false;
}

std::string DescribeCurrent() {
  std::ostringstream text;
  if (IsFullscreen()) {
    text << "fullscreen (";
  }
  text << GetScreenWidth() << "x" << GetScreenHeight();
  if (IsFullscreen()) {
    text << ")";
  }
  return text.str();
}

} // namespace GameWindow

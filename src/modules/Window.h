#pragma once

#include <string>

namespace GameWindow {

// The window is NOT freely resizable (no FLAG_WINDOW_RESIZABLE): the player
// picks one of a few fixed 16:9 sizes, or borderless fullscreen. Any preset
// that is not strictly smaller than the monitor becomes fullscreen instead,
// so the window can never end up larger than the screen it lives on.
struct Preset {
  int width;
  int height;
};

inline constexpr Preset kPresets[] = {
    {1280, 720},
    {1600, 900},
    {1920, 1080},
    {2560, 1440},
};
inline constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
inline constexpr int kDefaultPreset = 0;

// Accepts a preset index ("1".."4"), a bare 16:9 height ("720", "1080", ...) or
// an explicit "1280x720". Writes a 0-based index into kPresets.
bool ParsePreset(const std::string &text, int &index);

// 0-based index of the preset matching this size, or -1.
int FindPreset(int width, int height);

// "1: 1280x720, 2: 1600x900, ..."
std::string PresetList();

bool IsFullscreen();
void SetFullscreen(bool enabled);
void ToggleFullscreen();

// True when the preset is strictly smaller than the current monitor, i.e. it
// can be shown as a window instead of falling back to fullscreen.
bool PresetFitsMonitor(int index);

// Apply a preset, or go fullscreen if it does not fit the monitor.
// Returns true when fullscreen was entered instead of resizing the window.
bool ApplyPreset(int index);

// Current window size as "1280x720", or "fullscreen (1920x1080)".
std::string DescribeCurrent();

} // namespace GameWindow

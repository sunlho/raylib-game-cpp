#pragma once

#include <cstdint>
#include <string>

#include "raylib.h"

namespace Debug {

class FrameStepper {
public:
  void UpdateControls(bool controlsEnabled) {
    stepRequested_ = false;
    screenshotStepRequested_ = false;
    pauseStateChanged_ = false;

    if (!controlsEnabled) {
      return;
    }

    if (IsKeyPressed(KEY_F6)) {
      paused_ = !paused_;
      pauseStateChanged_ = true;
    }

    if (IsKeyPressed(KEY_F7)) {
      if (!paused_) {
        paused_ = true;
        pauseStateChanged_ = true;
      }
      stepRequested_ = true;
    }

    if (paused_ && IsKeyPressed(KEY_F8)) {
      stepRequested_ = true;
      screenshotStepRequested_ = true;
    }
  }

  [[nodiscard]] bool IsPaused() const {
    return paused_;
  }

  [[nodiscard]] bool IsStepRequested() const {
    return stepRequested_;
  }

  [[nodiscard]] bool DidPauseStateChange() const {
    return pauseStateChanged_;
  }

  [[nodiscard]] bool DidRequestScreenshotStep() const {
    return screenshotStepRequested_;
  }

  [[nodiscard]] bool ShouldAdvanceSimulation() const {
    return !paused_ || stepRequested_;
  }

  void RecordFixedStep() {
    ++fixedStepCount_;
  }

  void DrawOverlay() const {
    if (!paused_) {
      return;
    }
    constexpr int margin = 10;
    constexpr int fontSize = 16;
    constexpr int padding = 8;

    const std::string status = "SIMULATION PAUSED  |  F7: STEP  |  F8: SCREENSHOT STEP  |  TICK: " + std::to_string(fixedStepCount_);
    const int width = MeasureText(status.c_str(), fontSize) + padding * 2;
    const Color background = Color{112, 42, 35, 225};
    const Color border = Color{255, 184, 92, 255};

    DrawRectangle(margin, margin, width, fontSize + padding * 2, background);
    DrawRectangleLines(margin, margin, width, fontSize + padding * 2, border);
    DrawText(status.c_str(), margin + padding, margin + padding, fontSize, RAYWHITE);
  }

private:
  bool paused_ = false;
  bool stepRequested_ = false;
  bool screenshotStepRequested_ = false;
  bool pauseStateChanged_ = false;
  std::uint64_t fixedStepCount_ = 0;
};

} // namespace Debug

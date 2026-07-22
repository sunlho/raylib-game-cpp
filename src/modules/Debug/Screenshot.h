#pragma once

#include <string>

namespace Debug {

class ScreenshotCapture {
public:
  void Update(float deltaTime);
  void RequestCapture();
  void CapturePending();
  void DrawNotification() const;

private:
  bool capturePending_ = false;
  bool lastCaptureSucceeded_ = false;
  float notificationTimeRemaining_ = 0.0f;
  std::string notification_;
};

} // namespace Debug

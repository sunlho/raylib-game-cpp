#include "Screenshot.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>

#include "raylib.h"

namespace Debug {
namespace {

constexpr float NOTIFICATION_DURATION = 3.0f;

std::tm GetLocalTime(std::time_t time) {
  std::tm localTime{};
#if defined(_WIN32)
  localtime_s(&localTime, &time);
#else
  localtime_r(&time, &localTime);
#endif
  return localTime;
}

std::filesystem::path MakeScreenshotPath(const std::filesystem::path &directory) {
  const auto now = std::chrono::system_clock::now();
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
  const std::tm localTime = GetLocalTime(currentTime);

  std::ostringstream filename;
  filename
      << "debug_"
      << std::put_time(&localTime, "%Y%m%d_%H%M%S")
      << '_' << std::setfill('0') << std::setw(3) << milliseconds.count();

  const std::string baseName = filename.str();
  std::filesystem::path candidate = directory / (baseName + ".png");
  std::error_code error;
  for (int suffix = 1; std::filesystem::exists(candidate, error) && !error; ++suffix) {
    candidate = directory / (baseName + '_' + std::to_string(suffix) + ".png");
  }
  return candidate;
}

} // namespace

void ScreenshotCapture::Update(float deltaTime) {
  notificationTimeRemaining_ = std::max(0.0f, notificationTimeRemaining_ - std::max(deltaTime, 0.0f));
  if (IsKeyPressed(KEY_F11)) {
    RequestCapture();
  }
}

void ScreenshotCapture::RequestCapture() {
  capturePending_ = true;
}

void ScreenshotCapture::CapturePending() {
  if (!capturePending_) {
    return;
  }
  capturePending_ = false;

  std::error_code error;
  const std::filesystem::path directory = std::filesystem::absolute("screenshots", error);
  if (error || (!std::filesystem::create_directories(directory, error) && error)) {
    lastCaptureSucceeded_ = false;
    notification_ = "Screenshot failed: cannot create screenshots directory";
    notificationTimeRemaining_ = NOTIFICATION_DURATION;
    TraceLog(LOG_ERROR, "%s", notification_.c_str());
    return;
  }

  const std::filesystem::path screenshotPath = MakeScreenshotPath(directory);
  const std::string path = screenshotPath.string();
  Image screenshot = LoadImageFromScreen();
  const bool exported = screenshot.data != nullptr && ExportImage(screenshot, path.c_str());
  if (screenshot.data != nullptr) {
    UnloadImage(screenshot);
  }

  error.clear();
  lastCaptureSucceeded_ = exported && std::filesystem::is_regular_file(screenshotPath, error) && !error;
  if (lastCaptureSucceeded_) {
    notification_ = "Screenshot saved: " + screenshotPath.filename().string();
    TraceLog(LOG_INFO, "Screenshot saved to: %s", path.c_str());
  } else {
    notification_ = "Screenshot failed: " + screenshotPath.filename().string();
    TraceLog(LOG_ERROR, "Failed to save screenshot to: %s", path.c_str());
  }
  notificationTimeRemaining_ = NOTIFICATION_DURATION;
}

void ScreenshotCapture::DrawNotification() const {
  if (notificationTimeRemaining_ <= 0.0f || notification_.empty()) {
    return;
  }

  constexpr int margin = 10;
  constexpr int padding = 8;
  constexpr int fontSize = 16;
  const int width = MeasureText(notification_.c_str(), fontSize) + padding * 2;
  const int height = fontSize + padding * 2;
  const int y = GetScreenHeight() - height - margin;
  const Color border = lastCaptureSucceeded_ ? Color{86, 196, 164, 255} : Color{235, 91, 91, 255};

  DrawRectangle(margin, y, width, height, Color{20, 24, 30, 220});
  DrawRectangleLines(margin, y, width, height, border);
  DrawText(notification_.c_str(), margin + padding, y + padding, fontSize, RAYWHITE);
}

} // namespace Debug

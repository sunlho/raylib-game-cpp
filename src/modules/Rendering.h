#pragma once

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "flecs.h"
#include "raylib.h"
#include "raymath.h"

namespace Rendering {

struct Phases {
  struct Background {};
  struct World {};
  struct SortedWorld {};
};

// Simulation truth  written by physics, read by AI/collision. Never modify
// inside a render-prep pass.
struct Position {
  Vector2 value;
};

// Simulation position captured at the START of each fixed tick, before physics.
// Used as the "from" sample for render interpolation.
struct PreviousPosition {
  Vector2 value = {0.0f, 0.0f};
};

// Per-render-frame derived positions. Never written back to physics.
//   interpolated  lerp(previous, current, alpha); continuous float.
//   quantized     camera-relative snapped value; the only position used in Draw().
struct RenderPosition {
  Vector2 interpolated = {0.0f, 0.0f};
  Vector2 quantized = {0.0f, 0.0f};
};

// Fixed logical view size in world units. Decoupled from window size.
// The "resolution" console command must NOT modify this.
struct LogicalViewSize {
  Vector2 value = {640.0f, 360.0f};
};

struct Renderable {
  virtual ~Renderable() = default;
  virtual void Draw(const Position &position) const = 0;
};

struct RenderComponent {
  std::shared_ptr<Renderable> object;
  float floor = 2.5f;
  int sortY = 0;
  bool visible = true;
};

struct SortableTag {};

// Scene render-target size  fixed at 1280x720 regardless of window size.
// Window size only affects the final blit rectangle.
struct RenderTargetSize {
  Vector2 dimension;
};

struct RenderTargetState {
  bool active = false;
};

enum class LoadingPhase {
  Loading,
  Revealing,
  Hidden,
};

struct LoadingScreen {
  LoadingPhase phase = LoadingPhase::Hidden;
  float progress = 0.0f;
  float elapsed = 0.0f;
  float revealDuration = 0.8f;
  Vector2 revealCenter = {0.0f, 0.0f};
  std::string hint = "Preparing resources...";
};

struct LoadingStep {
  float progress = 1.0f;
  std::string hint = "Loading...";
  std::function<void(flecs::world &)> task;
  float minimumDisplayTime = 0.0f;
};

//  Math helpers

// Snap value to the nearest multiple of (1/stepsPerWorldUnit).
// stepsPerWorldUnit=2    0.5 world-unit grid (one scene pixel at zoom 2).
inline float SnapToGrid(float value, float stepsPerWorldUnit) {
  if (stepsPerWorldUnit <= 0.0f)
    return value;
  return std::round(value * stepsPerWorldUnit) / stepsPerWorldUnit;
}

inline Vector2 SnapToGrid(Vector2 v, float stepsPerWorldUnit) {
  return {SnapToGrid(v.x, stepsPerWorldUnit), SnapToGrid(v.y, stepsPerWorldUnit)};
}

// Camera-relative quantization: all dynamic objects share the same renderCamera
// and pixelsPerWorldUnit so their relative pixel distances stay stable.
inline Vector2 QuantizeForCamera(Vector2 worldPos, Vector2 renderCamera, float pixelsPerWorldUnit) {
  Vector2 rel = Vector2Subtract(worldPos, renderCamera);
  rel = SnapToGrid(rel, pixelsPerWorldUnit);
  return Vector2Add(renderCamera, rel);
}

//  Free functions

bool RunLoadingSequence(flecs::world &world, std::vector<LoadingStep> steps, std::string initialHint = {});
void SetLoadingProgress(flecs::world &world, float progress, std::string hint);
void BeginLoadingReveal(flecs::world &world);
void SetLoadingRevealCenter(flecs::world &world, Vector2 center);
void UpdateLoadingScreen(flecs::world &world, float deltaTime);
bool IsLoadingScreenVisible(const flecs::world &world);
bool IsLoadingSequenceActive(const flecs::world &world);

void BeginFrame(flecs::world &world);
void PresentFrame(flecs::world &world);
void EndFrame();

// Step 1 of render preparation: lerp PreviousPosition  Position into
// RenderPosition::interpolated for every entity that has all three components.
// Call this BEFORE UpdateRenderCamera so the camera receives an interpolated target.
void InterpolatePositions(flecs::world &world, float alpha);

// Step 2 of render preparation: snap every RenderPosition::interpolated to the
// pixel grid relative to the camera's current renderTarget, storing the result
// in RenderPosition::quantized. Call this AFTER UpdateRenderCamera.
void QuantizeRenderPositions(flecs::world &world);

// Convenience wrapper: InterpolatePositions + QuantizeRenderPositions.
// Suitable when the camera is known to be already up-to-date (e.g. in tests
// or single-step debugging). In the normal game loop, prefer calling the two
// functions individually with UpdateRenderCamera in between.
void PrepareRenderFrame(flecs::world &world, float alpha);

static inline int GetSortYByLayer(int layerIndex, int posY) {
  return layerIndex * 10000 + posY;
}

struct module {
  module(flecs::world &world);
};

} // namespace Rendering

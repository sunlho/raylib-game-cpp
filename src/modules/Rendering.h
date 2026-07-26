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

// Scene render-target size, EXCLUDING the guard border. Derived from
// LogicalViewSize * camera zoom, and fixed for a given SceneResolution
// regardless of window size. Window size only affects the final blit rectangle.
struct RenderTargetSize {
  Vector2 dimension;
};

// Overscan on every side of the scene buffer, in scene pixels. The camera can
// only be positioned on whole scene pixels, so the sub-scene-pixel remainder is
// recovered by translating the final blit by whole output pixels
// (MainCamera::renderShift). That translation would expose the buffer edge, so
// the buffer is rendered one pixel larger on each side and cropped on blit.
// One pixel is enough: the discarded remainder never exceeds half a scene pixel.
inline constexpr float kSceneGuardPixels = 1.0f;

// Actual texture size of the scene buffer: scene size plus the guard border.
inline Vector2 SceneBufferSize(Vector2 sceneTargetSize) {
  return {
      sceneTargetSize.x + kSceneGuardPixels * 2.0f,
      sceneTargetSize.y + kSceneGuardPixels * 2.0f};
}

// Pixel density of the scene buffer, mirroring Eastward's visual_quality
// (full / half) switch. The visible world stays LogicalViewSize either way;
// only the sub-art-pixel precision of the camera changes.
enum class SceneResolution {
  // 1280x720 buffer, camera zoom 2 -> 1 world unit = 2 scene pixels.
  // Camera and dynamic objects snap to a 0.5 world-unit grid (half art pixel).
  Full,
  // 640x360 buffer, camera zoom 1 -> 1 world unit = 1 scene pixel, i.e. the
  // native art resolution. Everything snaps to whole world units, and 720p /
  // 1080p / 1440p output become exact 2x / 3x / 4x integer upscales.
  Half,
  // Buffer follows the window: zoom = largest integer that still fits, so the
  // scene is rendered at (close to) native output resolution and the final blit
  // is 1:1 whenever the window is an exact multiple of the logical view.
  // This is what Eastward's default "clear" scale mode does, and it is the only
  // setting where BOTH the camera and every character quantise to half an
  // OUTPUT pixel instead of half an art pixel.
  Native,
};

struct SceneSettings {
  SceneResolution resolution = SceneResolution::Native;
  // Scene pixels per world unit currently in effect (== camera zoom).
  float pixelsPerWorldUnit = 1.0f;
};

struct RenderTargetState {
  bool active = false;
};

// How the fixed scene target is scaled into the window. Mirrors Eastward's
// "Enlarge Mode" (scale_mode) setting; the scene buffer never changes size.
enum class OutputScaleMode {
  // PixZoom2 sharp-bilinear: pixels stay square and crisp at any fractional
  // scale (1.25x, 1.5x, ...). Eastward's "clear" default.
  Sharp,
  // Plain bilinear stretch. Eastward's "smooth".
  Smooth,
  // Nearest-neighbour, whole-number scale only. Pixel-perfect but leaves large
  // letterbox borders unless the window is an exact multiple of the scene buffer.
  Integer,
};

struct OutputSettings {
  OutputScaleMode mode = OutputScaleMode::Sharp;
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

// Release GPU resources owned by this module (output shader). Call before
// CloseWindow().
void Shutdown();

//  Window output

// Aspect-preserving fit rectangle for the final scene blit: the whole scene
// target stays visible and the unused area becomes letterbox/pillarbox.
// integerScaleOnly rounds the scale down to a whole number when upscaling.
Rectangle ComputeOutputRect(int windowWidth, int windowHeight, Vector2 sceneTargetSize, bool integerScaleOnly);

const char *ToString(OutputScaleMode mode);
bool ParseOutputScaleMode(const std::string &text, OutputScaleMode &mode);

//  Scene buffer density

// Switch the scene buffer between Full and Half. Resizes RenderTargetSize to
// LogicalViewSize * zoom, updates the camera's zoom and pixelsPerWorldUnit, and
// re-snaps the camera onto the new grid. Safe to call at runtime: the render
// target is recreated on the next frame.
void SetSceneResolution(flecs::world &world, SceneResolution resolution);

// Re-derives the buffer density for SceneResolution::Native after a window
// resize. No-op for the fixed Full/Half settings. Call once per frame before
// the render-prep pass so the camera quantises against the right grid.
void UpdateAutoSceneResolution(flecs::world &world);

inline SceneResolution GetSceneResolution(const flecs::world &world) {
  return world.get<SceneSettings>().resolution;
}
const char *ToString(SceneResolution resolution);
bool ParseSceneResolution(const std::string &text, SceneResolution &resolution);
inline OutputScaleMode GetOutputScaleMode(const flecs::world &world) {
  return world.get<OutputSettings>().mode;
}
inline void SetOutputScaleMode(flecs::world &world, OutputScaleMode mode) {
  world.get_mut<OutputSettings>().mode = mode;
}

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

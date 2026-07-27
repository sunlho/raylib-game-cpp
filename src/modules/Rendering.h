#pragma once

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "flecs.h"
#include "raylib.h"
#include "raymath.h"

#include "Core/Core.h"

namespace Rendering {

struct Phases {
  struct Background {};
  struct World {};
  struct SortedWorld {};
};

struct Renderable {
  virtual ~Renderable() = default;
  virtual void Draw(const Core::Position &position) const = 0;
};

struct RenderComponent {
  std::shared_ptr<Renderable> object;
  float floor = 2.5f;
  int sortY = 0;
  bool visible = true;
};

struct SortableTag {};

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
  Native,
  // An explicit density set through SetSceneScale ("camerascale"). Excluded
  // from the Native auto-resize path so the requested value actually sticks.
  Custom,
};

struct SceneSettings {
  SceneResolution resolution = SceneResolution::Native;
  // Scene pixels per world unit currently in effect (== camera zoom).
  float pixelsPerWorldUnit = 1.0f;
};

struct RenderTargetState {
  bool active = false;
};

enum class OutputScaleMode {
  // PixZoom2 sharp-bilinear: pixels stay square and crisp at any fractional
  // scale (1.25x, 1.5x, ...).
  Sharp,
  // Plain bilinear stretch.
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

// Set an explicit scene buffer density and switch SceneResolution to Custom so
// UpdateOutputGeometry stops re-deriving it from the window size.
// Always go through this instead of assigning Camera2D::zoom directly: zoom,
// pixelsPerWorldUnit and RenderTargetSize must move together or the
// quantisation grid silently desynchronises from what is actually rendered.
void SetSceneScale(flecs::world &world, float pixelsPerWorldUnit);

// Refreshes everything derived from the current window size: the buffer density
// for SceneResolution::Native (no-op for the fixed Full/Half settings) and
// Core::OutputScale. Call once per frame before the render-prep pass so the
// camera quantises against the grid the window can actually show.
void UpdateOutputGeometry(flecs::world &world);

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

#pragma once

#include "raylib.h"

namespace Core {

// Fixed logical view size in world units. Decoupled from window size.
// The "resolution" console command must NOT modify this.
struct LogicalViewSize {
  Vector2 value = {640.0f, 360.0f};
};

// Scene render-target size, EXCLUDING the guard border. Derived from
// LogicalViewSize * camera zoom, and fixed for a given scene resolution
// regardless of window size. Window size only affects the final blit rectangle.
struct RenderTargetSize {
  Vector2 dimension;
};

// Extent of the playable world in world units, written by whoever loads the
// level. Lives here rather than in MapManager so the camera can clamp itself
// without knowing that maps exist.
struct WorldBounds {
  Vector2 dimension = {0.0f, 0.0f};
};

// Output pixels per scene pixel for the current window, refreshed once per
// frame by Rendering::UpdateOutputGeometry. The camera needs this to know how
// fine a grid the screen can actually show; it deliberately does not know how
// the scale is derived (letterbox fit, integer-only mode, ...).
struct OutputScale {
  float value = 1.0f;
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

} // namespace Core

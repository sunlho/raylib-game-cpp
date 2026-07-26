#pragma once

#include <cmath>

#include "raylib.h"
#include "raymath.h"

// Core owns the data every other module agrees on: where things are, and how
// big the visible world is. It depends on nothing but raylib, so Physics,
// Movement, Character, Camera, Map and Rendering can all include it without
// pulling in each other.
namespace Core {

// Simulation truth -- written by physics, read by AI/collision. Never modify
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

// Snap value to the nearest multiple of (1/stepsPerWorldUnit).
// stepsPerWorldUnit=2  ->  0.5 world-unit grid (one scene pixel at zoom 2).
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

} // namespace Core

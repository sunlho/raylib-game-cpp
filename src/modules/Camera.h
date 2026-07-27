#pragma once

#include "flecs.h"
#include "raylib.h"

#include "Core/Core.h"

namespace GameCamera {

struct MainCamera {
  Camera2D value = {
      Vector2{0.0f, 0.0f}, // offset  set in Begin2D to scene-target centre
      Vector2{0.0f, 0.0f}, // target  overwritten each frame by renderTarget
      0.0f,                // rotation
      2.0f};               // zoom  set by Rendering::SetSceneResolution (2 = Full, 1 = Half)

  bool enabled = true;
  bool autoCenterOffset = true;

  // Additional world-space offset added to the follow target (e.g. cinematic shift).
  Vector2 followOffset = {0.0f, 0.0f};

  // Continuous float camera state. Never quantised; never written back to physics.
  Vector2 smoothTarget = {0.0f, 0.0f};

  // Final per-frame camera position snapped to the scene pixel grid ("coarse").
  // This is the only value written to Camera2D.target.
  Vector2 renderTarget = {0.0f, 0.0f};

  // Residual compensation: the part of the camera position that renderTarget
  // had to discard, expressed as a whole number of OUTPUT pixels. The final
  // blit is translated by this, so the camera effectively moves on a
  // 1/outputScale scene-pixel grid instead of a whole scene pixel. Because the
  // whole image shifts together, no relative jitter is introduced.
  Vector2 renderShift = {0.0f, 0.0f};

  // Exponential-damping speed (lambda).
  float followSpeed = 5.66f;

  // Scene pixels per world unit; always equal to value.zoom. Kept separate so
  // quantisation never depends on an accidental match. Set by
  // Rendering::SetSceneResolution: 2 -> 0.5 world-unit grid, 1 -> 1.0.
  float pixelsPerWorldUnit = 2.0f;

  // Set false to skip snap (useful for debug free-camera or cutscenes).
  bool snapToRenderGrid = true;
};

void Begin2D(flecs::world &world);
void End2D(const flecs::world &world);

// Per-render-frame camera update.
//   interpolatedPlayerPos  RenderPosition::interpolated of the follow target.
//   dt                     real frame delta time (NOT fixedTimeStep).
void UpdateRenderCamera(flecs::world &world, Vector2 interpolatedPlayerPos, float dt);

// Instantly reposition smoothTarget and renderTarget to focus. Call on
// teleport / map change / spawn to prevent long fly-overs.
void SnapCameraTo(flecs::world &world, Vector2 focus);

struct module {
  module(flecs::world &world);
};

} // namespace GameCamera

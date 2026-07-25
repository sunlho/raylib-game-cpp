#pragma once

#include "flecs.h"
#include "raylib.h"

namespace GameCamera {

// Motion state that drives FocusProxy look-ahead distance.
enum class FollowMotion {
  Idle,
  Walk,
  Attack,
  Run,
};

// First-layer focus proxy: smoothly tracks the desired look-ahead direction and
// distance so the camera leads the player before they change direction.
struct FocusProxy {
  Vector2 direction = {0.0f, 1.0f}; // normalised facing; initialised facing south
  Vector2 offset = {0.0f, 0.0f};    // direction * distance, applied to camera target
  float distance = 0.0f;
};

struct MainCamera {
  Camera2D value = {
      Vector2{0.0f, 0.0f}, // offset  set in Begin2D to scene-target centre
      Vector2{0.0f, 0.0f}, // target  overwritten each frame by renderTarget
      0.0f,                // rotation
      2.0f};               // zoom  2x makes 1 scene pixel == 0.5 world unit

  bool enabled = true;
  bool autoCenterOffset = true;

  // Additional world-space offset added to the follow target (e.g. cinematic shift).
  Vector2 followOffset = {0.0f, 0.0f};

  // Continuous float camera state. Never quantised; never written back to physics.
  Vector2 smoothTarget = {0.0f, 0.0f};

  // Final per-frame camera position snapped to the render pixel grid.
  // This is the only value written to Camera2D.target.
  Vector2 renderTarget = {0.0f, 0.0f};

  // Look-ahead proxy  updated every render frame before camera smoothing.
  FocusProxy focus{};

  // Exponential-damping speed (lambda).
  // 5.66 ydy Eastward's k=0.09 per 60 Hz reference frame converted to lambda.
  float followSpeed = 5.66f;

  // Quantisation: world units per render pixel = zoom / 1.
  // At zoom 2 this is 2.0  snaps to 0.5 world-unit grid.
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

// Instantly reposition smoothTarget and renderTarget to focus, and reset the
// FocusProxy. Call on teleport / map change / spawn to prevent long fly-overs.
void SnapCameraTo(flecs::world &world, Vector2 focus);

struct module {
  module(flecs::world &world);
};

} // namespace GameCamera

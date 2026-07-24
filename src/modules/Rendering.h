#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "flecs.h"
#include "raylib.h"

namespace Rendering {

struct Phases {
  struct Background {};
  struct World {};
  struct SortedWorld {};
};

struct Position {
  Vector2 value;
};

struct PreviousPosition {
  Vector2 value = {0.0f, 0.0f};
};

struct RenderPosition {
  Vector2 interpolated = {0.0f, 0.0f};
  Vector2 quantized = {0.0f, 0.0f};
};

struct RenderSettings {
  Vector2 logicalViewSize = {640.0f, 360.0f};
  Vector2 sceneTargetSize = {1280.0f, 720.0f};
  float pixelsPerWorldUnit = 2.0f;
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

bool RunLoadingSequence(flecs::world &world, std::vector<LoadingStep> steps, std::string initialHint = {});
void SetLoadingProgress(flecs::world &world, float progress, std::string hint);
void BeginLoadingReveal(flecs::world &world);
void SetLoadingRevealCenter(flecs::world &world, Vector2 center);
void UpdateLoadingScreen(flecs::world &world, float deltaTime);
bool IsLoadingScreenVisible(const flecs::world &world);
bool IsLoadingSequenceActive(const flecs::world &world);

void CapturePreviousPositions(flecs::world &world);
void InterpolateRenderPositions(flecs::world &world, float alpha);
void QuantizeRenderPositions(flecs::world &world, Vector2 renderCamera, float stepsPerWorldUnit);
void ResetRenderPosition(flecs::entity entity, Vector2 position);

void BeginFrame(flecs::world &world);
void PresentFrame(flecs::world &world);
void EndFrame();

static inline int GetSortYByLayer(int layerIndex, int posY) {
  return layerIndex * 10000 + posY;
}

struct module {
  module(flecs::world &world);
};

} // namespace Rendering

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "flecs.h"
#include "raylib.h"

namespace Rendering {

struct Position {
  Vector2 value;
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

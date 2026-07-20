#include <algorithm>
#include <cmath>
#include <deque>
#include <utility>

#include "Camera.h"
#include "raymath.h"
#include "Reflection.h"
#include "Rendering.h"
#include "Runtime/RuntimePhases.h"

namespace Rendering {
namespace {

struct LoadingSequenceState {
  std::deque<LoadingStep> steps;
  bool active = false;
  bool showingInitialHint = false;
  bool presented = false;
  float stepElapsed = 0.0f;
};

struct PixelScaleShader {
  Shader value = {};
  int sourceSizeLocation = -1;
  int scaleLocation = -1;
  bool valid = false;
};

constexpr const char *PixelScaleFragmentShader = R"glsl(
#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 sourceSize;
uniform float scale;

out vec4 finalColor;

void main() {
  vec2 pixelPosition = fragTexCoord * sourceSize;
  vec2 pixelFraction = fract(pixelPosition);
  vec2 sampleOffset =
      clamp(pixelFraction * scale, vec2(0.0), vec2(0.5)) +
      clamp((pixelFraction - vec2(1.0)) * scale + vec2(0.5), vec2(0.0), vec2(0.5));
  vec2 sampleUv = (floor(pixelPosition) + sampleOffset) / sourceSize;

  finalColor = texture(texture0, sampleUv) * colDiffuse * fragColor;
}
)glsl";

// Overscan prevents subpixel composition from exposing a black edge.
constexpr int RenderTargetPadding = 1;

void DrawScaledRenderTarget(
    const RenderTexture2D &renderTarget,
    const Vector2 &targetSize,
    const RenderTargetState &state,
    const PixelScaleShader &pixelScaleShader) {
  const int screenWidth = GetScreenWidth();
  const int screenHeight = GetScreenHeight();
  const int targetWidth = static_cast<int>(targetSize.x);
  const int targetHeight = static_cast<int>(targetSize.y);

  if (screenWidth <= 0 || screenHeight <= 0 || targetWidth <= 0 || targetHeight <= 0) {
    return;
  }

  const int scaleX = (screenWidth + targetWidth - 1) / targetWidth;
  const int scaleY = (screenHeight + targetHeight - 1) / targetHeight;
  const int scale = std::max(1, std::max(scaleX, scaleY));
  const int destinationWidth = targetWidth * scale;
  const int destinationHeight = targetHeight * scale;
  const int offsetX = (screenWidth - destinationWidth) / 2;
  const int offsetY = (screenHeight - destinationHeight) / 2;
  const int scaledPadding = state.padding * scale;

  const Rectangle source = {0.0f, 0.0f, static_cast<float>(renderTarget.texture.width), -static_cast<float>(renderTarget.texture.height)};
  const Rectangle destination = {
      static_cast<float>(offsetX - scaledPadding),
      static_cast<float>(offsetY - scaledPadding),
      static_cast<float>(destinationWidth + scaledPadding * 2),
      static_cast<float>(destinationHeight + scaledPadding * 2)};

  const Camera2D screenCamera = {
      Vector2{0.0f, 0.0f},
      Vector2Scale(state.cameraSubpixelOffset, static_cast<float>(scale)),
      0.0f,
      1.0f};

  const Vector2 sourceSize = {
      static_cast<float>(renderTarget.texture.width),
      static_cast<float>(renderTarget.texture.height)};
  const float shaderScale = static_cast<float>(scale);
  if (pixelScaleShader.valid) {
    SetShaderValue(pixelScaleShader.value, pixelScaleShader.sourceSizeLocation, &sourceSize, SHADER_UNIFORM_VEC2);
    SetShaderValue(pixelScaleShader.value, pixelScaleShader.scaleLocation, &shaderScale, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(pixelScaleShader.value);
  }

  BeginMode2D(screenCamera);
  DrawTexturePro(renderTarget.texture, source, destination, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
  EndMode2D();

  if (pixelScaleShader.valid) {
    EndShaderMode();
  }
}

bool EnsureRenderTarget(RenderTexture2D &renderTarget, const Vector2 &size, int padding, bool usePixelScaleShader) {
  const int width = static_cast<int>(size.x) + padding * 2;
  const int height = static_cast<int>(size.y) + padding * 2;

  if (width <= 0 || height <= 0) {
    return false;
  }

  if (renderTarget.id != 0) {
    const bool sizeMatches = renderTarget.texture.width == width && renderTarget.texture.height == height;
    if (!sizeMatches) {
      UnloadRenderTexture(renderTarget);
      renderTarget = {0};
    }
  }

  if (renderTarget.id == 0) {
    renderTarget = LoadRenderTexture(width, height);
    if (renderTarget.id == 0) {
      return false;
    }

    SetTextureFilter(
        renderTarget.texture,
        usePixelScaleShader ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
  }

  return true;
}

void DrawCenteredText(const char *text, int centerX, int y, int fontSize, Color color) {
  DrawText(text, centerX - MeasureText(text, fontSize) / 2, y, fontSize, color);
}

void DrawLoadingOverlay(const LoadingScreen &loadingScreen, LoadingSequenceState &sequence, const Vector2 &targetSize) {
  const int width = static_cast<int>(targetSize.x);
  const int height = static_cast<int>(targetSize.y);
  if (width <= 0 || height <= 0 || loadingScreen.phase == LoadingPhase::Hidden) {
    return;
  }

  const Color maskColor = Color{10, 12, 16, 255};
  if (loadingScreen.phase == LoadingPhase::Revealing) {
    const Vector2 center = loadingScreen.revealCenter;
    const float farthestX = std::max(center.x, targetSize.x - center.x);
    const float farthestY = std::max(center.y, targetSize.y - center.y);
    const float fullRadius = std::sqrt(farthestX * farthestX + farthestY * farthestY) + 2.0f;
    const float duration = std::max(loadingScreen.revealDuration, 0.01f);
    const float progress = std::clamp(loadingScreen.elapsed / duration, 0.0f, 1.0f);
    const float remaining = 1.0f - progress;
    const float easedRemaining = remaining * remaining * (3.0f - 2.0f * remaining);
    DrawCircleV(center, fullRadius * easedRemaining, maskColor);
    return;
  }

  DrawRectangle(0, 0, width, height, maskColor);

  const Vector2 spinnerCenter = {targetSize.x * 0.5f, targetSize.y * 0.5f - 24.0f};
  const float angle = std::fmod(loadingScreen.elapsed * 220.0f, 360.0f);
  DrawRing(spinnerCenter, 11.0f, 14.0f, angle, angle + 275.0f, 32, Color{86, 196, 164, 255});
  DrawCenteredText("LOADING", width / 2, static_cast<int>(spinnerCenter.y + 25.0f), 20, RAYWHITE);
  DrawCenteredText(loadingScreen.hint.c_str(), width / 2, static_cast<int>(spinnerCenter.y + 50.0f), 10, Color{166, 174, 188, 255});

  const int barWidth = 180;
  const int barHeight = 3;
  const int barX = (width - barWidth) / 2;
  const int barY = static_cast<int>(spinnerCenter.y + 70.0f);
  DrawRectangle(barX, barY, barWidth, barHeight, Color{42, 47, 56, 255});
  DrawRectangle(barX, barY, static_cast<int>(barWidth * std::clamp(loadingScreen.progress, 0.0f, 1.0f)), barHeight, Color{86, 196, 164, 255});
  sequence.presented = true;
}

} // namespace

bool RunLoadingSequence(flecs::world &world, std::vector<LoadingStep> steps, std::string initialHint) {
  auto &sequence = world.get_mut<LoadingSequenceState>();
  if (sequence.active || steps.empty()) {
    return false;
  }

  sequence.steps.clear();
  for (auto &step : steps) {
    sequence.steps.push_back(std::move(step));
  }
  sequence.active = true;
  sequence.showingInitialHint = !initialHint.empty();
  sequence.presented = false;
  sequence.stepElapsed = 0.0f;

  auto &loadingScreen = world.get_mut<LoadingScreen>();
  loadingScreen.phase = LoadingPhase::Loading;
  loadingScreen.progress = 0.0f;
  loadingScreen.elapsed = 0.0f;
  loadingScreen.hint = sequence.showingInitialHint ? std::move(initialHint) : sequence.steps.front().hint;
  return true;
}

void SetLoadingProgress(flecs::world &world, float progress, std::string hint) {
  auto &loadingScreen = world.get_mut<LoadingScreen>();
  if (loadingScreen.phase != LoadingPhase::Loading) {
    loadingScreen.elapsed = 0.0f;
  }
  loadingScreen.phase = LoadingPhase::Loading;
  loadingScreen.progress = std::clamp(progress, 0.0f, 1.0f);
  loadingScreen.hint = std::move(hint);
}

void BeginLoadingReveal(flecs::world &world) {
  auto &sequence = world.get_mut<LoadingSequenceState>();
  sequence.steps.clear();
  sequence.active = false;
  sequence.showingInitialHint = false;
  sequence.presented = false;
  sequence.stepElapsed = 0.0f;

  auto &loadingScreen = world.get_mut<LoadingScreen>();
  loadingScreen.phase = LoadingPhase::Revealing;
  loadingScreen.progress = 1.0f;
  loadingScreen.elapsed = 0.0f;
}

void SetLoadingRevealCenter(flecs::world &world, Vector2 center) {
  world.get_mut<LoadingScreen>().revealCenter = center;
}

void UpdateLoadingScreen(flecs::world &world, float deltaTime) {
  auto &loadingScreen = world.get_mut<LoadingScreen>();
  if (loadingScreen.phase == LoadingPhase::Hidden) {
    return;
  }

  loadingScreen.elapsed += std::max(deltaTime, 0.0f);
  auto &sequence = world.get_mut<LoadingSequenceState>();
  if (loadingScreen.phase == LoadingPhase::Loading && sequence.active && sequence.presented) {
    if (sequence.showingInitialHint) {
      sequence.showingInitialHint = false;
      loadingScreen.hint = sequence.steps.front().hint;
      sequence.presented = false;
      sequence.stepElapsed = 0.0f;
      return;
    }

    sequence.stepElapsed += std::max(deltaTime, 0.0f);
    if (sequence.stepElapsed < std::max(sequence.steps.front().minimumDisplayTime, 0.0f)) {
      return;
    }

    LoadingStep step = std::move(sequence.steps.front());
    sequence.steps.pop_front();
    const bool isLastStep = sequence.steps.empty();
    const std::string nextHint = isLastStep ? std::string{} : sequence.steps.front().hint;
    sequence.presented = false;
    sequence.stepElapsed = 0.0f;

    if (step.task) {
      step.task(world);
    }

    auto &updatedLoadingScreen = world.get_mut<LoadingScreen>();
    updatedLoadingScreen.progress = std::clamp(step.progress, 0.0f, 1.0f);
    if (isLastStep) {
      BeginLoadingReveal(world);
    } else {
      updatedLoadingScreen.hint = nextHint;
    }
  }

  auto &currentLoadingScreen = world.get_mut<LoadingScreen>();
  if (currentLoadingScreen.phase == LoadingPhase::Revealing && currentLoadingScreen.elapsed >= currentLoadingScreen.revealDuration) {
    currentLoadingScreen.phase = LoadingPhase::Hidden;
  }
}

bool IsLoadingScreenVisible(const flecs::world &world) {
  return world.get<LoadingScreen>().phase != LoadingPhase::Hidden;
}

bool IsLoadingSequenceActive(const flecs::world &world) {
  return world.get<LoadingSequenceState>().active;
}

void BeginFrame(flecs::world &world) {
  auto &renderTarget = world.get_mut<RenderTexture2D>();
  const auto &renderTargetSize = world.get<RenderTargetSize>();
  auto &renderTargetState = world.get_mut<RenderTargetState>();
  const auto &mainCamera = world.get<GameCamera::MainCamera>();
  const auto &pixelScaleShader = world.get<PixelScaleShader>();

  BeginDrawing();
  renderTargetState.padding = mainCamera.enabled ? RenderTargetPadding : 0;
  renderTargetState.cameraSubpixelOffset = Vector2{0.0f, 0.0f};
  renderTargetState.active = EnsureRenderTarget(
      renderTarget,
      renderTargetSize.dimension,
      renderTargetState.padding,
      pixelScaleShader.valid);
  if (renderTargetState.active) {
    BeginTextureMode(renderTarget);
  }
  ClearBackground(BLACK);
}

void PresentFrame(flecs::world &world) {
  const auto &renderTargetState = world.get<RenderTargetState>();
  const auto &renderTargetSize = world.get<RenderTargetSize>();
  const auto &pixelScaleShader = world.get<PixelScaleShader>();

  const float padding = renderTargetState.active
      ? static_cast<float>(renderTargetState.padding)
      : 0.0f;
  const Camera2D overlayCamera = {
      Vector2{padding, padding},
      Vector2{0.0f, 0.0f},
      0.0f,
      1.0f};
  BeginMode2D(overlayCamera);
  DrawLoadingOverlay(
      world.get<LoadingScreen>(),
      world.get_mut<LoadingSequenceState>(),
      renderTargetSize.dimension);
  EndMode2D();

  if (renderTargetState.active) {
    EndTextureMode();
    DrawScaledRenderTarget(
        world.get<RenderTexture2D>(),
        renderTargetSize.dimension,
        renderTargetState,
        pixelScaleShader);
  }

  DrawFPS(GetScreenWidth() - 100, 10);
}

void EndFrame() {
  EndDrawing();
}

module::module(flecs::world &world) {
  PixelScaleShader pixelScaleShader;
  pixelScaleShader.value = LoadShaderFromMemory(nullptr, PixelScaleFragmentShader);
  pixelScaleShader.sourceSizeLocation = GetShaderLocation(pixelScaleShader.value, "sourceSize");
  pixelScaleShader.scaleLocation = GetShaderLocation(pixelScaleShader.value, "scale");
  pixelScaleShader.valid = pixelScaleShader.sourceSizeLocation >= 0 && pixelScaleShader.scaleLocation >= 0;

  world.component<PixelScaleShader>()
      .add(flecs::Singleton)
      .set<PixelScaleShader>(pixelScaleShader);
  world.system<PixelScaleShader>("Unload Pixel Scale Shader")
      .kind(flecs::OnRemove)
      .each([](PixelScaleShader &shader) {
        if (shader.value.id != 0) {
          UnloadShader(shader.value);
          shader.value = {};
        }
      });

  Reflection::Register<Position>(world);
  Reflection::Register<Rectangle>(world);
  Reflection::Register<RenderComponent>(world);
  Reflection::Register<RenderTargetSize>(world)
      .add(flecs::Singleton)
      .set<RenderTargetSize>({});
  Reflection::Register<RenderTargetState>(world)
      .add(flecs::Singleton)
      .set<RenderTargetState>({});
  Reflection::Register<LoadingScreen>(world)
      .add(flecs::Singleton)
      .set<LoadingScreen>({});
  world.component<LoadingSequenceState>()
      .add(flecs::Singleton)
      .set<LoadingSequenceState>({});
  Reflection::Register<RenderTexture2D>(world)
      .add(flecs::Singleton)
      .set<RenderTexture2D>({});

  world.system<const Position, const RenderComponent>("Draw Renderables")
      .kind<Runtime::Phases::DrawWorld>()
      .without<SortableTag>()
      .each([](const Position &p, const RenderComponent &renderable) {
        if (!renderable.visible || !renderable.object) {
          return;
        }

        renderable.object->Draw(p);
      });
}

} // namespace Rendering

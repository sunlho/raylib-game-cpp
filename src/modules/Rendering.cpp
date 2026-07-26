#include <algorithm>
#include <cmath>
#include <deque>
#include <utility>

#include "Camera.h"
#include "Reflection.h"
#include "Rendering.h"

namespace Rendering {
namespace {

struct LoadingSequenceState {
  std::deque<LoadingStep> steps;
  bool active = false;
  bool showingInitialHint = false;
  bool presented = false;
  float stepElapsed = 0.0f;
};

// Port of Eastward's shader/screen/PixZoom2.shader_script (sharp-bilinear).
// The sampled UV is snapped to the source texel centre across the flat interior
// of each texel and only ramps across a one-output-pixel-wide band at texel
// borders, so a fractional scale such as 1.5x stays crisp instead of either
// blurring (plain bilinear) or producing unevenly sized pixels (plain nearest).
// Requires bilinear filtering on the source texture; "scale" is the output
// scale factor (destinationHeight / sourceHeight).
constexpr const char *kPixZoomFragmentShader = R"(#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 srcSize;
uniform float scale;

out vec4 finalColor;

void main() {
    vec2 pixPos = fragTexCoord * srcSize;
    vec2 p = fract(pixPos);
    float ix = clamp(p.x * scale, 0.0, 0.5) + clamp((p.x - 1.0) * scale + 0.5, 0.0, 0.5);
    float iy = clamp(p.y * scale, 0.0, 0.5) + clamp((p.y - 1.0) * scale + 0.5, 0.0, 0.5);
    vec2 uv = (floor(pixPos) + vec2(ix, iy)) / srcSize;

    finalColor = texture(texture0, uv) * colDiffuse * fragColor;
}
)";

// GPU-side output state. Not a Flecs singleton because it mirrors process-wide
// GL state rather than world data.
struct OutputState {
  Shader shader{};
  int srcSizeLocation = -1;
  int scaleLocation = -1;
  bool shaderLoaded = false;
  bool shaderFailed = false;
  unsigned int filteredTextureId = 0;
  int appliedFilter = -1;
};

OutputState &GetOutputState() {
  static OutputState state;
  return state;
}

bool EnsureOutputShader(OutputState &state) {
  if (state.shaderLoaded) {
    return true;
  }
  if (state.shaderFailed) {
    return false;
  }

  state.shader = LoadShaderFromMemory(nullptr, kPixZoomFragmentShader);
  if (!IsShaderValid(state.shader)) {
    UnloadShader(state.shader);
    state.shader = Shader{};
    state.shaderFailed = true;
    TraceLog(LOG_WARNING, "RENDERING: PixZoom output shader failed to compile; falling back to bilinear");
    return false;
  }

  state.srcSizeLocation = GetShaderLocation(state.shader, "srcSize");
  state.scaleLocation = GetShaderLocation(state.shader, "scale");
  state.shaderLoaded = true;
  return true;
}

// SetTextureFilter touches GL state, so only re-apply it when it actually
// changes (i.e. on mode switches, not every frame).
void ApplyOutputFilter(OutputState &state, const Texture2D &texture, bool bilinear) {
  const int wanted = bilinear ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT;
  if (state.filteredTextureId == texture.id && state.appliedFilter == wanted) {
    return;
  }

  SetTextureFilter(texture, wanted);
  // Clamp so the blend band at the outermost texels cannot wrap around.
  SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
  state.filteredTextureId = texture.id;
  state.appliedFilter = wanted;
}

void DrawScaledRenderTarget(const RenderTexture2D &renderTarget, const Vector2 &targetSize, OutputScaleMode mode, Vector2 renderShift) {
  const int screenWidth = GetScreenWidth();
  const int screenHeight = GetScreenHeight();
  if (screenWidth <= 0 || screenHeight <= 0 || targetSize.x <= 0.0f || targetSize.y <= 0.0f) {
    return;
  }

  // The visible rectangle: the scene area only, guard border excluded.
  const Rectangle destination = ComputeOutputRect(screenWidth, screenHeight, targetSize, mode == OutputScaleMode::Integer);
  if (destination.width <= 0.0f || destination.height <= 0.0f) {
    return;
  }

  // The whole buffer (scene + guard) is drawn one guard-width larger on each
  // side and translated by renderShift; the scissor crops it back to the scene
  // rectangle, so the shift can never expose the buffer edge.
  const float outputScale = destination.height / targetSize.y;
  const float guard = Core::kSceneGuardPixels * outputScale;
  const Rectangle expanded = {
      destination.x - guard + renderShift.x,
      destination.y - guard + renderShift.y,
      destination.width + guard * 2.0f,
      destination.height + guard * 2.0f};

  const Rectangle source = {0.0f, 0.0f, static_cast<float>(renderTarget.texture.width), -static_cast<float>(renderTarget.texture.height)};

  auto &state = GetOutputState();
  ApplyOutputFilter(state, renderTarget.texture, mode != OutputScaleMode::Integer);

  BeginScissorMode(
      static_cast<int>(destination.x),
      static_cast<int>(destination.y),
      static_cast<int>(destination.width),
      static_cast<int>(destination.height));

  if (mode == OutputScaleMode::Sharp && EnsureOutputShader(state)) {
    const Vector2 sourceSize = {
        static_cast<float>(renderTarget.texture.width),
        static_cast<float>(renderTarget.texture.height)};
    SetShaderValue(state.shader, state.srcSizeLocation, &sourceSize, SHADER_UNIFORM_VEC2);
    SetShaderValue(state.shader, state.scaleLocation, &outputScale, SHADER_UNIFORM_FLOAT);

    BeginShaderMode(state.shader);
    DrawTexturePro(renderTarget.texture, source, expanded, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    EndShaderMode();
  } else {
    DrawTexturePro(renderTarget.texture, source, expanded, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
  }

  EndScissorMode();
}

bool EnsureRenderTarget(RenderTexture2D &renderTarget, const Vector2 &size) {
  const int width = static_cast<int>(size.x);
  const int height = static_cast<int>(size.y);

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

    SetTextureFilter(renderTarget.texture, TEXTURE_FILTER_POINT);
  }

  return true;
}

void DrawCenteredText(const char *text, int centerX, int y, int fontSize, Color color) {
  DrawText(text, centerX - MeasureText(text, fontSize) / 2, y, fontSize, color);
}

// Mask + circular reveal. Drawn inside the scene buffer because the reveal
// centre comes from the camera transform, so it must share that coordinate
// space. Purely geometric, therefore independent of the buffer size.
void DrawLoadingMask(const LoadingScreen &loadingScreen, const Vector2 &targetSize) {
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
}

// Spinner, labels and progress bar. Drawn in window coordinates after the scene
// blit: they are system UI, so their on-screen size must not depend on whether
// the scene buffer is 1280x720 or 640x360.
void DrawLoadingWidgets(const LoadingScreen &loadingScreen, LoadingSequenceState &sequence) {
  if (loadingScreen.phase != LoadingPhase::Loading) {
    return;
  }

  const int width = GetScreenWidth();
  const int height = GetScreenHeight();
  if (width <= 0 || height <= 0) {
    return;
  }

  // Sizes below were authored against a 720p window; keep the apparent size
  // constant as the window grows.
  const float scale = std::max(static_cast<float>(height) / 720.0f, 1.0f);
  const Vector2 spinnerCenter = {width * 0.5f, height * 0.5f - 24.0f * scale};
  const float angle = std::fmod(loadingScreen.elapsed * 220.0f, 360.0f);
  DrawRing(spinnerCenter, 11.0f * scale, 14.0f * scale, angle, angle + 275.0f, 32, Color{86, 196, 164, 255});
  DrawCenteredText("LOADING", width / 2, static_cast<int>(spinnerCenter.y + 25.0f * scale), static_cast<int>(20.0f * scale), RAYWHITE);
  DrawCenteredText(loadingScreen.hint.c_str(), width / 2, static_cast<int>(spinnerCenter.y + 50.0f * scale), static_cast<int>(10.0f * scale), Color{166, 174, 188, 255});

  const int barWidth = static_cast<int>(180.0f * scale);
  const int barHeight = std::max(1, static_cast<int>(3.0f * scale));
  const int barX = (width - barWidth) / 2;
  const int barY = static_cast<int>(spinnerCenter.y + 70.0f * scale);
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
  const auto &renderTargetSize = world.get<Core::RenderTargetSize>();
  auto &renderTargetState = world.get_mut<RenderTargetState>();

  BeginDrawing();
  // Clear the window first: when the scene rect does not cover the whole window
  // (letterbox / pillarbox) the border would otherwise keep stale pixels.
  ClearBackground(BLACK);

  // The texture is one guard pixel larger than the scene on every side.
  renderTargetState.active = EnsureRenderTarget(renderTarget, Core::SceneBufferSize(renderTargetSize.dimension));
  if (renderTargetState.active) {
    BeginTextureMode(renderTarget);
    ClearBackground(BLACK);
  }
}

void PresentFrame(flecs::world &world) {
  const auto &renderTargetState = world.get<RenderTargetState>();
  const auto &renderTargetSize = world.get<Core::RenderTargetSize>();

  // Covers the guard border too, so the mask never leaves a bright fringe when
  // the blit is shifted.
  DrawLoadingMask(world.get<LoadingScreen>(), Core::SceneBufferSize(renderTargetSize.dimension));

  if (renderTargetState.active) {
    EndTextureMode();
    DrawScaledRenderTarget(
        world.get<RenderTexture2D>(),
        renderTargetSize.dimension,
        world.get<OutputSettings>().mode,
        world.get<GameCamera::MainCamera>().renderShift);
  }

  DrawLoadingWidgets(world.get<LoadingScreen>(), world.get_mut<LoadingSequenceState>());
  DrawFPS(GetScreenWidth() - 100, 10);
}

void EndFrame() {
  EndDrawing();
}

void Shutdown() {
  auto &state = GetOutputState();
  if (state.shaderLoaded) {
    UnloadShader(state.shader);
    state.shader = Shader{};
    state.shaderLoaded = false;
  }
}

Rectangle ComputeOutputRect(int windowWidth, int windowHeight, Vector2 sceneTargetSize, bool integerScaleOnly) {
  if (windowWidth <= 0 || windowHeight <= 0 || sceneTargetSize.x <= 0.0f || sceneTargetSize.y <= 0.0f) {
    return Rectangle{0.0f, 0.0f, 0.0f, 0.0f};
  }

  float scale = std::min(
      static_cast<float>(windowWidth) / sceneTargetSize.x,
      static_cast<float>(windowHeight) / sceneTargetSize.y);

  // Only snap down when upscaling; below 1x a whole-number scale would be 0 (or
  // a cropped 1x), so keep the fractional fit and let the filter handle it.
  if (integerScaleOnly && scale >= 1.0f) {
    scale = std::floor(scale);
  }

  const float width = std::round(sceneTargetSize.x * scale);
  const float height = std::round(sceneTargetSize.y * scale);
  return Rectangle{
      std::floor((static_cast<float>(windowWidth) - width) * 0.5f),
      std::floor((static_cast<float>(windowHeight) - height) * 0.5f),
      width,
      height};
}

const char *ToString(OutputScaleMode mode) {
  switch (mode) {
  case OutputScaleMode::Sharp:
    return "sharp";
  case OutputScaleMode::Smooth:
    return "smooth";
  case OutputScaleMode::Integer:
    return "integer";
  }
  return "sharp";
}

namespace {

// Largest integer zoom whose buffer still fits inside the window, so the final
// blit only ever upscales (never downsamples pixel art).
float NativeSceneScale(const Vector2 &logicalView) {
  if (logicalView.x <= 0.0f || logicalView.y <= 0.0f) {
    return 1.0f;
  }
  const float fit = std::min(
      static_cast<float>(GetScreenWidth()) / logicalView.x,
      static_cast<float>(GetScreenHeight()) / logicalView.y);
  return std::clamp(std::floor(fit), 1.0f, 8.0f);
}

// Output pixels per scene pixel for the current window and scene buffer.
// Published as Core::OutputScale so the camera can quantise against the finest
// grid the screen can show without knowing how the fit is computed.
void RefreshOutputScale(flecs::world &world) {
  const Vector2 sceneSize = world.get<Core::RenderTargetSize>().dimension;
  const bool integerOnly = world.get<OutputSettings>().mode == OutputScaleMode::Integer;
  const Rectangle rect = ComputeOutputRect(GetScreenWidth(), GetScreenHeight(), sceneSize, integerOnly);

  world.get_mut<Core::OutputScale>().value =
      (sceneSize.y > 0.0f && rect.height > 0.0f) ? rect.height / sceneSize.y : 1.0f;
}

void ApplySceneScale(flecs::world &world, float pixelsPerWorldUnit) {
  auto &settings = world.get_mut<SceneSettings>();
  settings.pixelsPerWorldUnit = pixelsPerWorldUnit;

  // The visible world is the invariant; the buffer is derived from it.
  const Vector2 logicalView = world.get<Core::LogicalViewSize>().value;
  world.get_mut<Core::RenderTargetSize>().dimension = Vector2Scale(logicalView, pixelsPerWorldUnit);

  auto &camera = world.get_mut<GameCamera::MainCamera>();
  camera.value.zoom = pixelsPerWorldUnit;
  camera.pixelsPerWorldUnit = pixelsPerWorldUnit;

  // Re-snap onto the new grid so the first frame after a switch is aligned;
  // smoothTarget keeps full float precision and is deliberately untouched.
  camera.renderTarget = camera.snapToRenderGrid
                            ? Core::SnapToGrid(camera.smoothTarget, camera.pixelsPerWorldUnit)
                            : camera.smoothTarget;
  camera.renderShift = {0.0f, 0.0f};
  camera.value.target = camera.renderTarget;

  // The buffer just changed size, so the blit scale did too.
  RefreshOutputScale(world);
}

float ResolutionScale(flecs::world &world, SceneResolution resolution) {
  switch (resolution) {
  case SceneResolution::Half:
    return 1.0f;
  case SceneResolution::Full:
    return 2.0f;
  case SceneResolution::Native:
    return NativeSceneScale(world.get<Core::LogicalViewSize>().value);
  case SceneResolution::Custom:
    return world.get<SceneSettings>().pixelsPerWorldUnit;
  }
  return 1.0f;
}

} // namespace

void SetSceneResolution(flecs::world &world, SceneResolution resolution) {
  world.get_mut<SceneSettings>().resolution = resolution;
  ApplySceneScale(world, ResolutionScale(world, resolution));
}

void SetSceneScale(flecs::world &world, float pixelsPerWorldUnit) {
  if (!(pixelsPerWorldUnit > 0.0f)) {
    return;
  }

  world.get_mut<SceneSettings>().resolution = SceneResolution::Custom;
  ApplySceneScale(world, pixelsPerWorldUnit);
}

void UpdateOutputGeometry(flecs::world &world) {
  const auto &settings = world.get<SceneSettings>();
  if (settings.resolution == SceneResolution::Native) {
    const float desired = NativeSceneScale(world.get<Core::LogicalViewSize>().value);
    if (desired != settings.pixelsPerWorldUnit) {
      // ApplySceneScale refreshes the output scale itself.
      ApplySceneScale(world, desired);
      return;
    }
  }

  // The scene buffer can stay the same size while the blit scale changes: at
  // 1280x720 and 1600x900 the native zoom is 2 either way, but one scene pixel
  // covers 2.0 output pixels in the first case and 2.5 in the second. So this
  // has to run every frame, not only when the buffer is resized.
  RefreshOutputScale(world);
}

const char *ToString(SceneResolution resolution) {
  switch (resolution) {
  case SceneResolution::Half:
    return "half";
  case SceneResolution::Full:
    return "full";
  case SceneResolution::Native:
    return "native";
  case SceneResolution::Custom:
    return "custom";
  }
  return "native";
}

bool ParseSceneResolution(const std::string &text, SceneResolution &resolution) {
  if (text == "half" || text == "640x360") {
    resolution = SceneResolution::Half;
    return true;
  }
  if (text == "full" || text == "1280x720") {
    resolution = SceneResolution::Full;
    return true;
  }
  if (text == "native" || text == "clear" || text == "auto") {
    resolution = SceneResolution::Native;
    return true;
  }
  return false;
}

bool ParseOutputScaleMode(const std::string &text, OutputScaleMode &mode) {
  if (text == "sharp" || text == "clear") {
    mode = OutputScaleMode::Sharp;
    return true;
  }
  if (text == "smooth") {
    mode = OutputScaleMode::Smooth;
    return true;
  }
  if (text == "integer" || text == "pixel") {
    mode = OutputScaleMode::Integer;
    return true;
  }
  return false;
}

module::module(flecs::world &world) {
  // Position, RenderPosition and the view singletons belong to Core::module,
  // which must already be imported.
  Reflection::Register<Rectangle>(world);
  Reflection::Register<RenderComponent>(world);
  Reflection::Register<RenderTargetState>(world)
      .add(flecs::Singleton)
      .set<RenderTargetState>({});
  world.component<OutputSettings>()
      .add(flecs::Singleton)
      .set<OutputSettings>({});
  world.component<SceneSettings>()
      .add(flecs::Singleton)
      .set<SceneSettings>({});
  Reflection::Register<LoadingScreen>(world)
      .add(flecs::Singleton)
      .set<LoadingScreen>({});
  world.component<LoadingSequenceState>()
      .add(flecs::Singleton)
      .set<LoadingSequenceState>({});
  Reflection::Register<RenderTexture2D>(world)
      .add(flecs::Singleton)
      .set<RenderTexture2D>({});

  world.system<const Core::Position, const RenderComponent>("Draw Renderables")
      .kind<Phases::World>()
      .without<SortableTag>()
      .each([](flecs::entity e, const Core::Position &p, const RenderComponent &renderable) {
        if (!renderable.visible || !renderable.object) {
          return;
        }
        // Use quantised render position when available; fall back to sim position.
        const Core::RenderPosition *rp = e.try_get<Core::RenderPosition>();
        const Core::Position drawPos = rp ? Core::Position{rp->quantized} : p;
        renderable.object->Draw(drawPos);
      });
}

void PrepareRenderFrame(flecs::world &world, float alpha) {
  InterpolatePositions(world, alpha);
  QuantizeRenderPositions(world);
}

void InterpolatePositions(flecs::world &world, float alpha) {
  world.each([alpha](const Core::Position &current, const Core::PreviousPosition &previous, Core::RenderPosition &rp) {
    rp.interpolated = Vector2Lerp(previous.value, current.value, alpha);
  });
}

void QuantizeRenderPositions(flecs::world &world) {
  const auto &camera = world.get<GameCamera::MainCamera>();
  const Vector2 renderCam = camera.renderTarget;
  const float ppwu = camera.pixelsPerWorldUnit;

  world.each([renderCam, ppwu](Core::RenderPosition &rp) {
    rp.quantized = Core::QuantizeForCamera(rp.interpolated, renderCam, ppwu);
  });
}

} // namespace Rendering

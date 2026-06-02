#include <algorithm>
#include <array>

#include "Reflection.h"
#include "Rendering.h"

namespace {
void DrawScaledRenderTarget(const RenderTexture2D &renderTarget, const Vector2 &targetSize) {
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

  const Rectangle source = {0.0f, 0.0f, static_cast<float>(renderTarget.texture.width), -static_cast<float>(renderTarget.texture.height)};
  const Rectangle destination = {
      static_cast<float>(offsetX),
      static_cast<float>(offsetY),
      static_cast<float>(destinationWidth),
      static_cast<float>(destinationHeight)};

  DrawTexturePro(renderTarget.texture, source, destination, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
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
} // namespace

void Rendering::Import(flecs::world &world) {

  Reflection::Register<Position>(world);
  Reflection::Register<RenderComponent>(world);

  Reflection::Register<WindowTitle>(world);
  Reflection::Register<WindowSize>(world);
  Reflection::Register<RenderTargetSize>(world);
  Reflection::Register<RenderTargetState>(world);
  Reflection::Register<WindowFPS>(world);

  std::array Phases = {
      world.entity<Phases::PreDraw>(),
      world.entity<Phases::Background>(),
      world.entity<Phases::Draw>(),
      world.entity<Phases::PostDraw>()};

  flecs::entity_t PriorPhase = flecs::OnStore;

  for (auto &Phase : Phases) {
    Phase.add(flecs::Phase).depends_on(PriorPhase);
    PriorPhase = Phase;
  }

  world.observer<const WindowTitle>("Update Window Title")
      .event(flecs::OnSet)
      .each([](const WindowTitle &title) {
        if (IsWindowReady()) {
          SetWindowTitle(title.value.c_str());
        }
      });
  world.observer<WindowSize>("Update Window Size")
      .event(flecs::OnSet)
      .each([](const WindowSize &size) {
        if (IsWindowReady()) {
          SetWindowSize(size.dimension.x, size.dimension.y);
        }
      });
  world.observer<WindowFPS>("Update Target FPS")
      .event(flecs::OnSet)
      .each([](const WindowFPS &fps) {
        if (IsWindowReady()) {
          SetTargetFPS(fps.target);
        }
      });
  world.system<const WindowTitle, const WindowSize, const WindowFPS>("InitializeWindow")
      .kind(flecs::OnStart)
      .each([](const WindowTitle &title, const WindowSize &size, const WindowFPS &fps) {
        InitWindow(size.dimension.x, size.dimension.y, title.value.c_str());
        SetTargetFPS(fps.target);
      });

  world.system("BeginDrawing")
      .kind<Phases::PreDraw>()
      .run([](flecs::iter &it) {
        auto &renderTarget = it.world().get_mut<RenderTexture2D>();
        const auto &renderTargetSize = it.world().get<RenderTargetSize>();
        auto &renderTargetState = it.world().get_mut<RenderTargetState>();

        BeginDrawing();

        renderTargetState.active = EnsureRenderTarget(renderTarget, renderTargetSize.dimension);
        if (renderTargetState.active) {
          BeginTextureMode(renderTarget);
        }

        ClearBackground(BLACK);
      });
  world.system("EndDraw")
      .kind<Phases::PostDraw>()
      .run([](flecs::iter &it) {
        const auto &renderTargetState = it.world().get<RenderTargetState>();

        if (renderTargetState.active) {
          const auto &renderTarget = it.world().get<RenderTexture2D>();
          EndTextureMode();

          const auto &renderTargetSize = it.world().get<RenderTargetSize>();
          DrawScaledRenderTarget(renderTarget, renderTargetSize.dimension);
        }

        DrawFPS(GetScreenWidth() - 100, 10);
        EndDrawing();
      });

  world.system<const Position, const RenderComponent>("Draw Renderables")
      .kind<Phases::Draw>()
      .each([](const Position &p, const RenderComponent &renderable) {
        if (!renderable.visible || !renderable.object) {
          return;
        }

        renderable.object->Draw(p);
      });
}

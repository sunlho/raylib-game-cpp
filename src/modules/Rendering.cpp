#include <algorithm>
#include <array>

#include "Reflection.h"
#include "Rendering.h"

namespace {
void DrawScaledRenderTarget(const RenderTexture2D &renderTarget, const Vector2 &targetSize) {
  const Rectangle source = {0.0f, 0.0f, static_cast<float>(renderTarget.texture.width), -static_cast<float>(renderTarget.texture.height)};
  const Rectangle destination = {0.0f, 0.0f, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};

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
        auto renderTargetEntity = it.world().singleton<RenderTexture2D>();
        auto &renderTarget = renderTargetEntity.get_mut<RenderTexture2D>();
        auto renderTargetSizeEntity = it.world().singleton<RenderTargetSize>();
        const auto &renderTargetSize = renderTargetSizeEntity.get<RenderTargetSize>();
        auto renderTargetStateEntity = it.world().singleton<RenderTargetState>();
        auto &renderTargetState = renderTargetStateEntity.get_mut<RenderTargetState>();

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
        auto renderTargetStateEntity = it.world().singleton<RenderTargetState>();
        const auto &renderTargetState = renderTargetStateEntity.get<RenderTargetState>();

        if (renderTargetState.active) {
          auto renderTargetEntity = it.world().singleton<RenderTexture2D>();
          const auto &renderTarget = renderTargetEntity.get<RenderTexture2D>();
          EndTextureMode();

          auto renderTargetSizeEntity = it.world().singleton<RenderTargetSize>();
          const auto &renderTargetSize = renderTargetSizeEntity.get<RenderTargetSize>();
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

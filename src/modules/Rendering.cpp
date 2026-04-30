#include <array>

#include "Reflection.h"
#include "Rendering.h"

void Rendering::Import(flecs::world &world) {

  Reflection::Register<Position>(world);
  Reflection::Register<RenderComponent>(world);

  Reflection::Register<WindowTitle>(world);
  Reflection::Register<WindowSize>(world);
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
        BeginDrawing();
        ClearBackground(BLACK);
      });
  world.system("EndDraw")
      .kind<Phases::PostDraw>()
      .run([](flecs::iter &it) {
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

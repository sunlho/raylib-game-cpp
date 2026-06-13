
#include "CharacterInternal.h"
#include "modules/Rendering.h"

namespace Character {
namespace {

class CharacterRenderable final : public Rendering::Renderable {
public:
  CharacterRenderable() {
  }

  void Draw(flecs::entity entity, const Rendering::Position &position) const override {
    auto spriteSet = entity.get<SpriteSet>();
    auto controller = entity.get<AnimationController>();

    if (!spriteSet.loaded) {
      return;
    }

    const auto *clip = controller.GetCurrentAnimation();
    if (!clip) {
      return;
    }

    auto *entry = spriteSet.FindEntry(clip->name);
    if (!entry) {
      return;
    }

    auto &animation = entry->animation;
    if (animation.texture.id == 0 || animation.frameCount <= 0) {
      return;
    }

    int frame = controller.currentFrame;
    if (frame < 0) {
      frame = 0;
    }
    if (frame >= animation.frameCount) {
      frame = animation.frameCount - 1;
    }

    if (animation.bytesPerFrame > 0 &&
        frame != animation.lastFrame &&
        static_cast<std::size_t>(animation.bytesPerFrame) * (static_cast<std::size_t>(frame) + 1) <= animation.pixels.size()) {
      UpdateTexture(animation.texture, animation.pixels.data() + static_cast<std::size_t>(animation.bytesPerFrame) * frame);

      animation.lastFrame = frame;
    }

    Rectangle src = {
        0.0f,
        0.0f,
        static_cast<float>(animation.width),
        static_cast<float>(animation.height)};
    Rectangle dest = {
        position.value.x,
        position.value.y,
        static_cast<float>(animation.width) * spriteSet.scale,
        static_cast<float>(animation.height) * spriteSet.scale};
    dest.x = roundf(dest.x);
    dest.y = roundf(dest.y);
    Vector2 origin = spriteSet.useCenterOrigin ? Vector2{roundf(dest.width * 0.5f), roundf(dest.height * 0.5f)} : spriteSet.origin;

    DrawTexturePro(animation.texture, src, dest, origin, 0.0f, WHITE);
  }
};

} // namespace

void RegisterCharacterRendering(flecs::world &world) {
  world.observer<SpriteSet, const AnimationController, const Rendering::Position>("Create Character Renderable Observer")
      .event(flecs::OnSet)
      .each([](flecs::entity entity, SpriteSet &spriteSet, const AnimationController &controller, const Rendering::Position &position) {
        auto renderable = std::make_shared<CharacterRenderable>();
        Rendering::RenderComponent renderComponent;
        renderComponent.object = renderable;
        renderComponent.visible = true;

        const Vector2 halfExtents = GetSpriteHalfExtents(spriteSet, controller);

        renderComponent.sortY = Rendering::GetSortYByLayer(3, position.value.y + 15.0f);

        entity.add<Rendering::RenderComponent>().set(renderComponent);
        entity.add<Rendering::RenderSortTag>();
      });
}

} // namespace Character

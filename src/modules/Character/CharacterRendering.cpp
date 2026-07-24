
#include "CharacterInternal.h"

#include "modules/Camera.h"
#include "modules/Debug/DebugDraw.h"
#include "modules/Movement.h"
#include "modules/Rendering.h"

namespace Character::Internal {

CharacterRenderable::CharacterRenderable(flecs::entity entity) : entity_(entity) {
}

void CharacterRenderable::Draw(const Rendering::Position &position) const {
  auto spriteSet = entity_.get<SpriteSet>();
  auto controller = entity_.get<AnimationController>();

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

  DebugDraw::DrawData drawData;
  drawData.type = DebugDraw::DrawType::Text;
  drawData.pos = position.value;
  drawData.text = "Character Position: (" + std::to_string(static_cast<int>(position.value.x * 10000) / 10000.0f) + ", " + std::to_string(static_cast<int>(position.value.y * 10000) / 10000.0f) + ")";
  DebugDraw::EnqueueDraw(drawData);

  Rectangle src = {0.0f, 0.0f, static_cast<float>(animation.width), static_cast<float>(animation.height)};
  Vector2 renderPosition = position.value;
  if (const auto *render = entity_.try_get<Rendering::RenderPosition>()) {
    renderPosition = render->quantized;
  }

  Rectangle dest = {
      renderPosition.x,
      renderPosition.y,
      static_cast<float>(animation.width) * spriteSet.scale,
      static_cast<float>(animation.height) * spriteSet.scale};
  Vector2 origin = spriteSet.useCenterOrigin ? Vector2{roundf(dest.width * 0.5f), roundf(dest.height * 0.5f)} : spriteSet.origin;

  DrawTexturePro(animation.texture, src, dest, origin, 0.0f, WHITE);
}

void RegisterCharacterRendering(flecs::world &world) {
  world.observer<SpriteSet, const AnimationController, const Rendering::Position>("Create Character Renderable Observer")
      .event(flecs::OnSet)
      .each([](flecs::entity entity, SpriteSet &spriteSet, const AnimationController &controller, const Rendering::Position &position) {
        CharacterRenderable renderable(entity);
        auto renderablePtr = std::make_shared<CharacterRenderable>(entity);
        Rendering::RenderComponent renderComponent;
        renderComponent.object = renderablePtr;
        renderComponent.visible = true;

        const Vector2 halfExtents = GetSpriteHalfExtents(spriteSet, controller);

        renderComponent.sortY = position.value.y + halfExtents.y;

        entity.add<Rendering::RenderComponent>().set(renderComponent);
        entity.add<Rendering::SortableTag>();
      });
}

} // namespace Character::Internal

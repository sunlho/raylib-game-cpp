
#include "CharacterInternal.h"

#include "modules/Camera.h"
#include "modules/Movement.h"
#include "modules/Rendering.h"

namespace Character::Internal {

CharacterRenderable::CharacterRenderable(flecs::entity entity) : entity_(entity) {
}

void CharacterRenderable::Draw(const Core::Position &position) const {
  // Bind by pointer, never by value: get<T>() returns const T&, so `auto` would
  // deep-copy the whole SpriteSet (every decoded animation frame of every clip)
  // on every draw call. try_get_mut is required because animation.lastFrame is
  // the upload cache below and has to survive across frames.
  auto *spriteSet = entity_.try_get_mut<SpriteSet>();
  const auto *controller = entity_.try_get<AnimationController>();
  if (!spriteSet || !controller || !spriteSet->loaded) {
    return;
  }

  const auto *clip = controller->GetCurrentAnimation();
  if (!clip) {
    return;
  }

  auto *entry = spriteSet->FindEntry(clip->name);
  if (!entry) {
    return;
  }

  auto &animation = entry->animation;
  if (animation.texture.id == 0 || animation.frameCount <= 0) {
    return;
  }

  int frame = controller->currentFrame;
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
  Vector2 renderPosition = position.value;
  // Use the camera-relative quantized render position when available.
  // This replaces the old camera-follow special case and makes all dynamic
  // entities use the same quantisation rule.
  const Core::RenderPosition *rp = entity_.try_get<Core::RenderPosition>();
  if (rp) {
    renderPosition = rp->quantized;
  }

  Rectangle dest = {
      renderPosition.x,
      renderPosition.y,
      static_cast<float>(animation.width) * spriteSet->scale,
      static_cast<float>(animation.height) * spriteSet->scale};
  // No roundf() here: quantisation was already applied by QuantizeForCamera
  // in Rendering::PrepareRenderFrame. Double-rounding would break the 0.5-unit grid.
  Vector2 origin = spriteSet->useCenterOrigin ? Vector2{roundf(dest.width * 0.5f), roundf(dest.height * 0.5f)} : spriteSet->origin;

  DrawTexturePro(animation.texture, src, dest, origin, 0.0f, WHITE);
}

void RegisterCharacterRendering(flecs::world &world) {
  world.observer<SpriteSet, const AnimationController, const Core::Position>("Create Character Renderable Observer")
      .event(flecs::OnSet)
      .each([](flecs::entity entity, SpriteSet &spriteSet, const AnimationController &controller, const Core::Position &position) {
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

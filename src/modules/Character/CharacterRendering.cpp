#include <cmath>
#include <utility>

#include "CharacterInternal.h"

namespace Character::Internal {

CharacterRenderable::CharacterRenderable(
    flecs::entity entity,
    std::weak_ptr<CatalogState> catalog)
    : entity_(entity), catalog_(std::move(catalog)) {
}

void CharacterRenderable::Draw(const Core::Position &position) const {
  const auto catalog = catalog_.lock();
  if (!catalog || catalog->closed) {
    return;
  }

  const auto *playback = entity_.try_get<PlaybackState>();
  if (!playback) {
    return;
  }

  const auto *appearance = FindAppearance(*catalog, playback->appearanceId);
  const auto *animation = CurrentAnimation(*catalog, *playback);
  if (!appearance || !animation || animation->loaded.frames.empty()) {
    return;
  }

  Vector2 renderPosition = position.value;
  if (const auto *rp = entity_.try_get<Core::RenderPosition>()) {
    renderPosition = rp->quantized;
  }

  const auto &geometry = appearance->intent.geometry;
  Rectangle destination = {
      renderPosition.x,
      renderPosition.y,
      static_cast<float>(animation->loaded.width) * geometry.scale,
      static_cast<float>(animation->loaded.height) * geometry.scale};
  const Vector2 origin =
      geometry.useCenterOrigin
          ? Vector2{std::round(destination.width * 0.5f), std::round(destination.height * 0.5f)}
          : Vector2{geometry.originX, geometry.originY};

  catalog->backend->DrawFrame(animation->loaded, playback->frame, destination, origin);
}

} // namespace Character::Internal

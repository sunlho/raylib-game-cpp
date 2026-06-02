#pragma once

#include <string>

#include "flecs.h"

namespace MapManage {

struct MapPath {
  std::string value;
};

struct MapState {
  flecs::entity mapRoot = {};
};

void Import(flecs::world &world);
void SetMapPath(flecs::world &world, const std::string &path);

} // namespace MapManage

#pragma once

#include <string>

#include "flecs.h"

namespace MapManage {

struct MapPath {
  std::string value;
};

struct MapState {
  flecs::entity mapRoot = {};
  std::string currentPath;
};

void SetMapPath(flecs::world &world, const std::string &path);

struct module {
  module(flecs::world &world);
};

} // namespace MapManage

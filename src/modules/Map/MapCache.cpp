#include <string>
#include <tuple>
#include <utility>

#include "MapInternal.h"

#include "modules/Tilemap/Tilemap.h"

namespace MapManager {

Tilemap::LoadedMap *GetOrLoadMap(MapCacheState &cacheState, const std::string &path) {
  auto it = cacheState.cache.find(path);
  if (it != cacheState.cache.end()) {
    cacheState.hitCount++;
    cacheState.usageOrder.erase(it->second.lruIt);
    cacheState.usageOrder.push_front(path);
    it->second.lruIt = cacheState.usageOrder.begin();
    return &it->second.loadedMap;
  }

  cacheState.missCount++;
  Tilemap::LoadedMap loadedMap;
  if (!Tilemap::LoadFromPath(path, loadedMap)) {
    return nullptr;
  }

  cacheState.usageOrder.push_front(path);
  auto insertIt = cacheState.cache.emplace(std::piecewise_construct, std::forward_as_tuple(path), std::forward_as_tuple()).first;
  insertIt->second.loadedMap = std::move(loadedMap);
  insertIt->second.lruIt = cacheState.usageOrder.begin();

  if (cacheState.cache.size() > cacheState.maxSize) {
    const std::string evictPath = cacheState.usageOrder.back();
    cacheState.cache.erase(evictPath);
    cacheState.usageOrder.pop_back();
  }

  return &insertIt->second.loadedMap;
}

} // namespace MapManager

#include "CoordinateMatrixCache.h"
#include "CoordinateMatrix.h"
#include <macgyver/Cache.h>

namespace Fmi
{
namespace
{
// About 50 grids if 2 way conversions are stored
const int default_cache_size = 100;

using GlobalCache = Cache::Cache<std::size_t, std::shared_ptr<CoordinateMatrix>>;
GlobalCache g_coordinateMatrixCache{default_cache_size};

}  // namespace

namespace CoordinateMatrixCache
{
std::shared_ptr<CoordinateMatrix> Find(std::size_t theHash)
{
  const auto& obj = g_coordinateMatrixCache.find(theHash);
  if (!obj) return {};
  return *obj;
}
void Insert(std::size_t theHash, std::shared_ptr<CoordinateMatrix> theMatrix)
{
  g_coordinateMatrixCache.insert(theHash, theMatrix);
}

void SetCacheSize(std::size_t newMaxSize) { g_coordinateMatrixCache.resize(newMaxSize); }

}  // namespace CoordinateMatrixCache
}  // namespace Fmi

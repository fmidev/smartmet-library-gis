#include "CoordinateMatrixCache.h"

#include "CoordinateMatrix.h"

#include <macgyver/Cache.h>

namespace Fmi
{
namespace
{
// About 50 grids if 2 way BilinearCoordinateTransfromations are cached
const int default_cache_size = 100;

using GlobalCache = Cache::Cache<std::size_t, std::shared_ptr<CoordinateMatrix>>;
GlobalCache g_coordinateMatrixCache{default_cache_size};

}  // namespace

namespace CoordinateMatrixCache
{
// Return cached matrix or empty shared_ptr
std::shared_ptr<CoordinateMatrix> Find(std::size_t theHash)
{
  const auto& obj = g_coordinateMatrixCache.find(theHash);
  if (!obj) return {};
  return *obj;
}

// Insert new matrix into the cache
void Insert(std::size_t theHash, std::shared_ptr<CoordinateMatrix> theMatrix)
{
  g_coordinateMatrixCache.insert(theHash, theMatrix);
}

// Resize the cache from the default
void SetCacheSize(std::size_t newMaxSize) { g_coordinateMatrixCache.resize(newMaxSize); }

}  // namespace CoordinateMatrixCache
}  // namespace Fmi

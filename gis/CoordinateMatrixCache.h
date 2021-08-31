#pragma once
#include <memory>
#include <macgyver/Cache.h>

namespace Fmi
{
class CoordinateMatrix;

namespace CoordinateMatrixCache
{
std::shared_ptr<CoordinateMatrix> Find(std::size_t theHash);
void Insert(std::size_t theHash, const std::shared_ptr<CoordinateMatrix>& theMatrix);

void SetCacheSize(std::size_t newMaxSize);

// Get cache statistics
const Cache::CacheStats& getCacheStats();
 
}  // namespace CoordinateMatrixCache
}  // namespace Fmi

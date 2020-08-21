#pragma once
#include <memory>

namespace Fmi
{
class CoordinateMatrix;

namespace CoordinateMatrixCache
{
std::shared_ptr<CoordinateMatrix> Find(std::size_t theHash);
void Insert(std::size_t theHash, std::shared_ptr<CoordinateMatrix> theMatrix);

void SetCacheSize(std::size_t newMaxSize);

}  // namespace CoordinateMatrixCache
}  // namespace Fmi

#include <macgyver/Cache.h>
#include <memory>
#include <string>

class OGRSpatialReference;

namespace Fmi
{
namespace OGRSpatialReferenceFactory
{
std::shared_ptr<OGRSpatialReference> Create(const std::string& theDesc);
std::shared_ptr<OGRSpatialReference> Create(int epsg);
void SetCacheSize(std::size_t newMaxSize);
// Get cache statistics
Cache::CacheStats getCacheStats();

}  // namespace OGRSpatialReferenceFactory
}  // namespace Fmi

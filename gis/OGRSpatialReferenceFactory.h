#include <macgyver/Cache.h>
#include <memory>
#include <mutex>
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

// Global lock serialising ALL GDAL/PROJ operations on the shared, cached
// OGRSpatialReference objects handed out by Create(). Those objects are not
// thread-safe: GDAL lazily (re)builds internal PROJ/WKT state on first use even
// from logically-const methods (refreshProjObj / refreshRootFromProjObj), and
// PROJ objects are not safe for concurrent use. Every consumer performing a
// GDAL/PROJ operation on a Create()-returned OGRSpatialReference must hold this
// lock for the duration of that operation. Only cold-cache paths take it; warm
// lookups elsewhere stay lock-free.
//
// Lock order is strictly one-way: this mutex, then PROJ's internal locks. It is
// a plain non-recursive std::mutex, so nothing holding it may call back into
// Create() (which takes it on a cold miss).
std::mutex& mutex();

}  // namespace OGRSpatialReferenceFactory
}  // namespace Fmi

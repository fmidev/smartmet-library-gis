#include <memory>
#include <string>

class OGRSpatialReference;

namespace Fmi
{
namespace OGRSpatialReferenceFactory
{
std::shared_ptr<OGRSpatialReference> Create(std::string theDesc);
std::shared_ptr<OGRSpatialReference> Create(int epsg);
void SetCacheSize(size_t newMaxSize);
};  // namespace OGRSpatialReferenceFactory
}  // namespace Fmi



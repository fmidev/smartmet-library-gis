#include "CoordinateTransformation.h"
#include "SpatialReference.h"
#include <gdal_version.h>
#include <limits>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>

// So far we've had no data with axes swapped since we force axis orientation in all constructors.
// This might change when using external sources for spatial references, then we'll need to put this
// flag on.

#define CHECK_AXES 0

#if 0
namespace
{
// Reduce to [-180,180] inclusive
double normalize_lon(double lon)
{
  auto tmp = fmod(lon, 360.0);
  if (tmp > 180.0) return tmp - 360.0;
  if (tmp < -180.0) return tmp + 360.0;
  return tmp;
}

// Reduce to [-90,90] inclusive
double normalize_lat(double lat)
{
  if (lat < -90) return -90;
  if (lat > 90) return 90;
  return lat;
}
}  // namespace
#endif

namespace Fmi
{
class CoordinateTransformation::Impl
{
 public:
  ~Impl() = default;
  Impl() = delete;

  Impl(const SpatialReference& theSource, const SpatialReference& theTarget)
      : m_transformation(OGRCreateCoordinateTransformation(theSource.get(), theTarget.get())),
        m_swapInput(theSource.isAxisSwapped()),
        m_swapOutput(theTarget.isAxisSwapped())
  {
    if (m_transformation == nullptr)
      throw std::runtime_error("Failed to create the requested coordinate transformation");
  }

  std::shared_ptr<OGRCoordinateTransformation> m_transformation;
  bool m_swapInput = false;   // swap xy before calling GDAL?
  bool m_swapOutput = false;  // swap xy after calling GDAL?
};

CoordinateTransformation::CoordinateTransformation(const SpatialReference& theSource,
                                                   const SpatialReference& theTarget)
    : impl(new Impl(theSource, theTarget))
{
}

bool CoordinateTransformation::transform(double& x, double& y) const
{
#if CHECK_AXES
  if (impl->m_swapInput) std::swap(x, y);
#endif

  bool ok = (impl->m_transformation->Transform(1, &x, &y) != 0);

  if (!ok)
  {
    x = std::numeric_limits<double>::quiet_NaN();
    y = x;
    return false;
  }

  // if (impl->m_swapOutput) std::swap(x, y);
  return true;
}

bool CoordinateTransformation::transform(std::vector<double>& x, std::vector<double>& y) const
{
  if (x.size() != y.size())
    throw std::runtime_error("X- and Y-coordinate vector sizes do not match");

  if (x.empty())
    throw std::runtime_error(
        "Cannot do coordinate transformation for empty X- and Y-coordinate vectors");

#if CHECK_AXES
  if (impl->m_swapInput) std::swap(x, y);
#endif

  int n = static_cast<int>(x.size());
  std::vector<int> flags(n, 0);

  bool ok = (impl->m_transformation->Transform(n, &x[0], &y[0], nullptr, &flags[0]) != 0);

#if CHECK_AXES
  if (impl->m_swapOutput) std::swap(x, y);
#endif

  for (std::size_t i = 0; i < flags.size(); i++)
  {
    if (flags[i] == 0)
    {
      x[i] = std::numeric_limits<double>::quiet_NaN();
      y[i] = x[i];
    }
  }

  return ok;
}

bool CoordinateTransformation::transform(OGRGeometry& geom) const
{
#if CHECK_AXES
  if (impl->m_swapInput) geom.swapXY();
#endif

  bool ok = (geom.transform(impl->m_transformation.get()) != OGRERR_NONE);

#if CHECK_AXES
  if (ok && impl->m_swapOutput) geom.swapXY();
#endif

  return ok;
}

const OGRSpatialReference& CoordinateTransformation::getSourceCS() const
{
  return *impl->m_transformation->GetSourceCS();
}

const OGRSpatialReference& CoordinateTransformation::getTargetCS() const
{
  return *impl->m_transformation->GetTargetCS();
}

const OGRCoordinateTransformation& CoordinateTransformation::operator*() const
{
  return *impl->m_transformation;
}

OGRCoordinateTransformation* CoordinateTransformation::get() const
{
  return impl->m_transformation.get();
}
}  // namespace Fmi

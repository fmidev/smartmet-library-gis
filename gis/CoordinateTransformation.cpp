#include "CoordinateTransformation.h"
#include "SpatialReference.h"
#include <gdal_version.h>
#include <limits>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>

namespace Fmi
{
bool is_axis_swapped(const OGRSpatialReference& sr)
{
#if GDAL_VERSION_MAJOR > 1

  if (sr.GetAxisMappingStrategy() == OAMS_TRADITIONAL_GIS_ORDER) return false;

  if (sr.GetAxisMappingStrategy() == OAMS_CUSTOM)
    return false;  // we do not attempt to figure it out

  // OAMS_AUTHORITY_COMPLIANT
  return (sr.EPSGTreatsAsLatLong() || sr.EPSGTreatsAsNorthingEasting());
#else
  // GDAL1 does not seem to obey EPSGA flags at all
  return false;
#endif
}

CoordinateTransformation::CoordinateTransformation(const SpatialReference& theSource,
                                                   const SpatialReference& theTarget)
    : m_transformation(OGRCreateCoordinateTransformation(theSource.get(), theTarget.get())),
      m_swapInput(theSource.isAxisSwapped()),
      m_swapOutput(theTarget.isAxisSwapped())
{
}

CoordinateTransformation::CoordinateTransformation(const OGRSpatialReference& theSource,
                                                   const OGRSpatialReference& theTarget)
    : m_transformation(OGRCreateCoordinateTransformation(&theSource, &theTarget)),
      m_swapInput(is_axis_swapped(theSource)),
      m_swapOutput(is_axis_swapped(theTarget))
{
}

bool CoordinateTransformation::transform(double& x, double& y) const
{
  // if (m_swapInput) std::swap(x, y);

  bool ok = (m_transformation->Transform(1, &x, &y) != 0);

  if (!ok)
  {
    x = std::numeric_limits<double>::quiet_NaN();
    y = x;
    return false;
  }

  // if (m_swapOutput) std::swap(x, y);
  return true;
}

bool CoordinateTransformation::transform(std::vector<double>& x, std::vector<double>& y) const
{
  if (x.size() != y.size())
    throw std::runtime_error("X- and Y-coordinate vector sizes do not match");

  if (x.empty())
    throw std::runtime_error(
        "Cannot do coordinate transformation for empty X- and Y-coordinate vectors");

  // if (m_swapInput) std::swap(x, y);

  int n = static_cast<int>(x.size());
  std::vector<int> flags(n, 0);

  bool ok = (m_transformation->Transform(n, &x[0], &y[0], nullptr, &flags[0]) != 0);

  // if (m_swapOutput) std::swap(x, y);

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
  // if (m_swapInput) geom.swapXY();

  bool ok = (geom.transform(m_transformation.get()) != OGRERR_NONE);

  // if (ok && m_swapOutput) geom.swapXY();

  return ok;
}

const OGRSpatialReference& CoordinateTransformation::getSourceCS() const
{
  return *m_transformation->GetSourceCS();
}

const OGRSpatialReference& CoordinateTransformation::getTargetCS() const
{
  return *m_transformation->GetTargetCS();
}

const OGRCoordinateTransformation& CoordinateTransformation::operator*() const
{
  return *m_transformation;
}

OGRCoordinateTransformation* CoordinateTransformation::get() const
{
  return m_transformation.get();
}
}  // namespace Fmi

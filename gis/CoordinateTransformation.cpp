#include "CoordinateTransformation.h"
#include "SpatialReference.h"
#include <gdal_version.h>
#include <limits>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>

namespace Fmi
{
CoordinateTransformation::CoordinateTransformation(const SpatialReference& theSource,
                                                   const SpatialReference& theTarget)
    : m_transformation(OGRCreateCoordinateTransformation(theSource.get(), theTarget.get())),
      m_swapInput(theSource.isAxisSwapped()),
      m_swapOutput(theTarget.isAxisSwapped())
{
  if (m_transformation == nullptr)
    throw std::runtime_error("Failed to create the requested coordinate transformation");
}

CoordinateTransformation::CoordinateTransformation(const SpatialReference& theSource,
                                                   const SpatialReference& theTarget,
                                                   double theWest,
                                                   double theSouth,
                                                   double theEast,
                                                   double theNorth)
    : m_swapInput(theSource.isAxisSwapped()), m_swapOutput(theTarget.isAxisSwapped())
{
  OGRCoordinateTransformationOptions opts;
  opts.SetAreaOfInterest(theWest, theSouth, theEast, theNorth);
  m_transformation.reset(OGRCreateCoordinateTransformation(theSource.get(), theTarget.get(), opts));
  if (m_transformation == nullptr)
    throw std::runtime_error("Failed to create the requested coordinate transformation");
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
